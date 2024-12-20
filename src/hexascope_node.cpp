#include <hexascope_node.hpp>
#include <thread_pool.hpp>
#include <boost/pool/object_pool.hpp>
#include <modalities_util.hpp>
#include <thalamus/async.hpp>

namespace thalamus {
  using namespace std::chrono_literals;

  struct HexascopeNode::Impl {
    boost::asio::io_context& io_context;
    ObservableDictPtr state;
    ObservableDictPtr hexa_to_objective_state;
    boost::signals2::scoped_connection state_connection;
    boost::signals2::scoped_connection source_connection;
    boost::signals2::scoped_connection get_node_connection;
    HexascopeNode* outer;
    MotionCaptureNode* source_node;
    NodeGraph* graph;

    ThreadPool& pool;
    boost::asio::steady_timer timer;
    boost::asio::steady_timer orient_timer;
    long long objective_pose = 0;
    long long field_pose = 1;
    bool running = false;
    std::vector<std::pair<std::chrono::nanoseconds, boost::qvm::quat<float>>> objective_rotations;
    std::vector<std::pair<std::chrono::nanoseconds, boost::qvm::quat<float>>> field_rotations;
    std::vector<std::pair<std::chrono::nanoseconds, boost::qvm::vec<float, 3>>> objective_translations;
    std::vector<std::pair<std::chrono::nanoseconds, boost::qvm::vec<float, 3>>> field_translations;
    std::optional<boost::qvm::quat<float>> objective_rotation;
    std::optional<boost::qvm::quat<float>> field_rotation;
    double error = 100;
    double last_error = 100;
    bool busy = false;

    struct PortGuard {
      boost::asio::serial_port& port;
      const bool was_open = false;
      PortGuard(boost::asio::serial_port& port) : port(port), was_open(port.is_open()) {
        if(!was_open) {
          port.open("/dev/ttyACM0");
        }
      }
      ~PortGuard() {
        if(!was_open) {
          port.close();
        }
      }
    };

    enum class State {
      IDLE,
      SEARCHING_THETA_POS,
      SEARCHING_THETA_NEG,
      SEARCHING_PHI_POS,
      SEARCHING_PHI_NEG,
      SEARCHING_PSI_POS,
      SEARCHING_PSI_NEG
    } state;

    Impl(ObservableDictPtr state, boost::asio::io_context& io_context, HexascopeNode* outer, NodeGraph* graph)
      : io_context(io_context)
      , state(state)
      , outer(outer)
      , graph(graph)
      , pool(graph->get_thread_pool())
      , timer(io_context)
      , orient_timer(io_context)
      , state(IDLE)
      , port(io_context) {
      port.set_option(boost::asio::serial_port::baud_rate(115200));
      using namespace std::placeholders;
      state_connection = state->recursive_changed.connect(std::bind(&Impl::on_change, this, _1, _2, _3. _4));
      this->state->recap(std::bind(&Impl::on_change, this, state_connection.get(), _1, _2, _3));
    }

    ~Impl() {
      (*state)["Running"].assign(false, [&] {});
    }

    alignas(float) unsigned char response[32];
    std::list<std::function<void()>> jobs;
    bool working = false;

    struct Accumulator {
      long long pose = 0;
      bool accumulating = false;
      std::vector<boost::qvm::vec<float, 3>> position;
      std::vector<boost::qvm::quat<float>> rotation;
    };

    Accumulator objective_accumulator;
    Accumulator field_accumulator;
    std::array<Accumulator*, 2> accumulators {&objective_accumulator, &field_accumulator};

    void open_port() {
      if(!port.is_open()) {
        port.open("/dev/ttyACM0");
      }
    }

    void on_data(Node*) {
      if(!source_node->has_motion_data()) {
        return;
      }

      for(auto a : accumulators) {
        if(a->accumulating) {
          for(auto& segment : source_node->segments()) {
            if(a->pose == segment.segment_id) {
              a->position.push_back(segment.position);
              a->rotation.push_back(segment.rotation);
            }
          }
        }
      }
    }

    struct Leg {
      int step_counts;
      int encoder_counts;
      bool ccw_jog;
      bool cw_jog;
      bool ccw_move;
      bool cw_move;
      bool ccw_soft_limit;
      bool cw_soft_limit;
      bool ccw_limit;
      bool cw_limit;
      bool motor_conn;
      bool homing;
      bool homed;
      bool interlock;
      void load_leg_response(unsigned char* buffer) {
        buffer += 8;
        //read little endian integer
        step_counts = buffer[0]; 
        step_counts += buffer[1] << 8; 
        step_counts += buffer[2] << 16; 
        step_counts += buffer[3] << 24; 
        
        buffer += 4;
        //read little endian integer
        encoder_counts = buffer[0]; 
        encoder_counts = buffer[1] << 8; 
        encoder_counts = buffer[2] << 16; 
        encoder_counts = buffer[3] << 24; 

        buffer += 4;
        ccw_jog = buffer[0] & 0x80;
        cw_jog = buffer[0] & 0x40;
        ccw_move = buffer[0] & 0x20;
        cw_move = buffer[0] & 0x10;
        ccw_soft_limit = buffer[0] & 0x08;
        cw_soft_limit = buffer[0] & 0x04;
        ccw_limit = buffer[0] & 0x02;
        cw_limit = buffer[0] & 0x01;

        homing = buffer[1] & 0x02;
        homed = buffer[1] & 0x04;
        interlock = buffer[1] & 0x08;
      }
    };
     
    boost::asio::awaitable<void> go_home() {
      co_await move(0, 0, 0, 0, 0, 0);
    }

    boost::asio::awaitable<void> move(float x, float y, float z) {
      auto current_pose = co_await read_hexa_pose_raw();
      co_await move(x, y, z, current_pose[3], current_pose[4], current_pose[5]);
    }

    boost::asio::awaitable<void> move(float x, float y, float z, float theta, float phi, float psi) {
      open_port();
      const goto_message_size = 38;
      //Allocate float array then reinterpret as a byte array so the bytes are float aligned
      float floats[2 //header (8 bytes)
                   + 6 //orientation (6 floats)
                   + 2 //tail (need 6 bytes so allocate 2 floats, which gets 8 bytes)
                   ];
      auto buffer = reinterpret_cast<unsigned char*>(buffer);
      std::fill(std::begin(buffer), std::begin(buffer) + sizeof(floats), 0);

      buffer[0] = 0xA0;
      buffer[1] = 0x40;
      buffer[2] = 0x20;
      buffer[3] = 0x00;
      buffer[4] = 0xA9;
      buffer[5] = 0x01;
      buffer[6] = 0x00;
      buffer[7] = 0x00;
      floats[2] = x;
      floats[3] = y;
      floats[4] = z;
      floats[5] = theta;
      floats[6] = phi;
      floats[7] = psi;
      co_await boost::asio::async_write(port, boost::asio::buffer(buffer, message_size), boost::asio::use_awaitable);

      Leg leg;
      boost::asio::steady_timer timer(io_context);
      bool moving = true
      while(moving) {
        timer.expires_after(1s);
        co_await timer.async_wait(boost::asio::use_awaitable);
        moving = false;
        for(auto i = 0;i < 6 && !moving;++i) {
          buffer[0] = 0x80;
          buffer[1] = 0x04;
          buffer[2] = 0x06;
          buffer[3] = 0x00;
          buffer[4] = 0x20 + i + 1;
          buffer[5] = 0x10;

          co_await boost::asio::async_write(port, boost::asio::buffer(buffer, 6), boost::asio::use_awaitable);
          co_await boost::asio::async_read(port, boost::asio::buffer(buffer, 20), boost::asio::use_awaitable);
          leg.load_leg_response(buffer);
          moving = moving || leg.ccw_move || leg.cw_move;
        }
      }
      return;
    }

    struct Pose {
      boost::qvm::vec<float, 3> position;
      boost::qvm::quat<float> rotation;
    };

    template<typename T>
    boost::asio::awaitable<std::optional<Pose>> measure_pose(T duration, Accumulator& accumulator) {
      accumulator.accumulating = true;
      accumulator.position.clear();
      accumulator.rotation.clear();
      Finally f([&]() {
        accumulator.accumulating = false;
        accumulator.position.clear();
        accumulator.rotation.clear();
      });

      boost::asio::steady_timer timer(io_context);
      timer.expires_after(duration);
      co_await timer.async_wait(boost::asio::use_awaitable);
      accumulating = false;
      if(objective_position_accum.empty()) {
        return {};
      }

      boost::qvm::vec<float, 3> position_mean;
      for(auto& pos : objective_position_accum) {
        position_mean += pos;
      }
      position_mean /= objective_position_accum.size();

      float min_distance = std::numeric_limits<float>::max();
      size_t median = std::numeric_limits<size_t>::max();

      for(auto i = 0ull;i < objective_position_accum.size();++i) {
        float distance = boost::qvm::mag_sqr(objective_position_accum[i] - position_mean);
        if(distance < min_distance) {
          min_distance = distance;
          median = i
        }
      }

      return Pose {objective_position_accum[median], objective_rotation_accum[median]};
    }

    template<typename T>
    boost::asio::awaitable<std::optional<Pose>> measure_objective_pose(T duration) {
      auto result = co_await measure_pose(duration, objective_accumulator);
      co_return result;
    }

    template<typename T>
    boost::asio::awaitable<std::optional<Pose>> measure_field_pose(T duration) {
      auto result = co_await measure_pose(duration, field_accumulator);
      co_return result;
    }

    struct HexaPose {
      float x = 0;
      float y = 0;
      float z = 0;
      float theta = 0;
      float phi = 0;
      float psi = 0;
      bool ready = false;
    };

    template<typename T>
    boost::asio::awaitable<std::array<float, 6>> read_hexa_pose_raw() {
      open_port();
      //Allocate float array then reinterpret as a byte array so the bytes are float aligned
      float floats[2 //header (8 bytes)
                   + 6 //orientation (6 floats)
                   ];
      auto buffer = reinterpret_cast<unsigned char*>(buffer);
      std::fill(std::begin(buffer), std::begin(buffer) + sizeof(floats), 0);
      buffer[0] = 0xA1;
      buffer[1] = 0x40;
      buffer[2] = 0x06;
      buffer[3] = 0x00;
      buffer[4] = 0x29;
      buffer[5] = 0x01;
      co_await boost::asio::async_write(port, boost::asio::buffer(buffer, 6), boost::asio::use_awaitable);
      co_await boost::asio::async_read(port, boost::asio::buffer(buffer, sizeof(buffer)), boost::asio::use_awaitable);

      co_return {floats[2], floats[3], floats[4], floats[5], floats[6], floats[7]};
    }

    template<typename T>
    boost::asio::awaitable<Pose> read_hexa_pose() {
      //Allocate float array then reinterpret as a byte array so the bytes are float aligned
      auto raw = co_await read_hexa_pose_raw();

      boost::qvm::vec<float, 3> position{raw[0], raw[1], raw[2]};
      position /= 1000;

      cv::Vec3f euler_angles{floats[3], floats[4], floats[5]};
      euler_angles *= -M_PI/180;
      auto cv_quaternion = cv::Quat<float>::createFromEulerAngles(euler_angles, cv::QuatEnum::EulerAnglesType::EXT_XYZ);
      boost::qvm::quat<float> boost_quaternion;
      boost::qvm::S(boost_quaternion) = cv_quaternion.w;
      boost::qvm::X(boost_quaternion) = cv_quaternion.x;
      boost::qvm::Y(boost_quaternion) = cv_quaternion.y;
      boost::qvm::Z(boost_quaternion) = cv_quaternion.z;

      return Pose { position, boost_quaternion };
    }

    boost::qvm::mat<float, 4, 4> objective_to_hexa;
    boost::qvm::mat<float, 4, 4> hexa_to_objective;

    const float CALIBRATION_STEP_SIZE = 20;

    boost::asio::awaitable<void> align() {
      busy = true;
      Finally f([&]() {
        busy = false;
      });

      auto field_pose_opt = co_await measure_field_pose(1s);
      if(!field_pose_opt) {
        THALAMUS_LOG(warning) << "No position samples received";
        co_return;
      }
      auto field_pose = *field_pose_opt;

      auto hexa_pose = co_await read_hexa_pose();
      auto objective_position = hexa_to_objective*hexa_pose.position;

      cv::Quat<float> hexa_rot_quat(boost::qvm::S(hexa_pose.rotation),
                                    boost::qvm::X(hexa_pose.rotation),
                                    boost::qvm::Y(hexa_pose.rotation),
                                    boost::qvm::Z(hexa_pose.rotation));
      auto hexa_rot_mat = hexa_rot_quat.toRotMat4x4();
      boost::qvm::mat<float, 4, 4> hexa_rot_qvm = {
        {hexa_rot_mat(0, 0), hexa_rot_mat(0, 1), hexa_rot_mat(0, 2), hexa_rot_mat(0, 3)},
        {hexa_rot_mat(1, 0), hexa_rot_mat(1, 1), hexa_rot_mat(1, 2), hexa_rot_mat(1, 3)},
        {hexa_rot_mat(2, 0), hexa_rot_mat(2, 1), hexa_rot_mat(2, 2), hexa_rot_mat(2, 3)},
        {hexa_rot_mat(3, 0), hexa_rot_mat(3, 1), hexa_rot_mat(3, 2), hexa_rot_mat(3, 3)}
      };

      auto objective_rot = hexa_to_objective*hexa_rot_qvm;

      objective_to_field = field_pose.position - objective_position
      auto objective_to_field_in_objective_coord = boost::qvm::inverse(objective_rot)*objective_to_field;
      auto x = boost::qvm::X(objective_to_field_in_objective_coord);
      auto y = boost::qvm::Y(objective_to_field_in_objective_coord);
      co_await move_objective(0, x, y);
    }
     
    boost::asio::awaitable<void> calibrate() {
      busy = true;
      Finally f([&]() {
        busy = false;
      });
      
      try {
        co_await move(0, 0, 0, 0, 0, 0);
        auto objective_origin_pose_opt = co_await measure_objective_pose(1s);
        if(!objective_origin_pose_opt) {
          THALAMUS_LOG(warning) << "No position samples received";
          co_return;
        }
        auto objective_origin_pose = *objective_origin_pose_opt;
        THALAMUS_LOG(info) << "objective_origin_pose " << boost::qvm::X(objective_origin_pose.position) << " " << boost::qvm::Y(objective_origin_pose.position) << " " << boost::qvm::Z(objective_origin_pose.position);
        auto hexa_origin_pose = co_await read_hexa_pose();
        THALAMUS_LOG(info) << "hexa_origin_pose " << boost::qvm::X(hexa_origin_pose.position) << " " << boost::qvm::Y(hexa_origin_pose.position) << " " << boost::qvm::Z(hexa_origin_pose.position);

        co_await move(CALIBRATION_STEP_SIZE, 0, 0, 0, 0, 0);
        auto objective_x_pose_opt = co_await measure_objective_pose(1s);
        if(!objective_x_pose_opt) {
          THALAMUS_LOG(warning) << "No position samples received";
          co_return;
        }
        auto objective_x_pose = *objective_x_pose_opt;
        THALAMUS_LOG(info) << "objective_x_pose " << boost::qvm::X(objective_x_pose.position) << " " << boost::qvm::Y(objective_x_pose.position) << " " << boost::qvm::Z(objective_x_pose.position);
        auto hexa_x_pose = co_await read_hexa_pose();
        THALAMUS_LOG(info) << "hexa_x_pose " << boost::qvm::X(hexa_x_pose.position) << " " << boost::qvm::Y(hexa_x_pose.position) << " " << boost::qvm::Z(hexa_x_pose.position);

        co_await move(0, CALIBRATION_STEP_SIZE, 0, 0, 0, 0);
        auto objective_y_pose_opt = co_await measure_objective_pose(1s);
        if(!objective_y_pose_opt) {
          THALAMUS_LOG(warning) << "No position samples received";
          co_return;
        }
        auto objective_y_pose = *objective_y_pose_opt;
        THALAMUS_LOG(info) << "objective_y_pose " << boost::qvm::X(objective_y_pose.position) << " " << boost::qvm::Y(objective_y_pose.position) << " " << boost::qvm::Z(objective_y_pose.position);
        auto hexa_y_pose = co_await read_hexa_pose();
        THALAMUS_LOG(info) << "hexa_y_pose " << boost::qvm::X(hexa_y_pose.position) << " " << boost::qvm::Y(hexa_y_pose.position) << " " << boost::qvm::Z(hexa_y_pose.position);

        co_await move(0, 0, CALIBRATION_STEP_SIZE, 0, 0, 0);
        auto objective_z_pose_opt = co_await measure_objective_pose(1s);
        if(!objective_z_pose_opt) {
          THALAMUS_LOG(warning) << "No position samples received";
          co_return;
        }
        auto objective_z_pose = *objective_z_pose_opt;
        THALAMUS_LOG(info) << "objective_z_pose " << boost::qvm::X(objective_z_pose.position) << " " << boost::qvm::Y(objective_z_pose.position) << " " << boost::qvm::Z(objective_z_pose.position);
        auto hexa_z_pose = co_await read_hexa_pose();
        THALAMUS_LOG(info) << "hexa_z_pose " << boost::qvm::X(hexa_z_pose.position) << " " << boost::qvm::Y(hexa_z_pose.position) << " " << boost::qvm::Z(hexa_z_pose.position);

        boost::qvm::mat<float, 4, 4> hexa = {
          {boost::qvm::X(hexa_origin_pose.position), boost::qvm::X(hexa_x_pose.position), boost::qvm::X(hexa_y_pose.position), boost::qvm::X(hexa_z_pose.position)},
          {boost::qvm::Y(hexa_origin_pose.position), boost::qvm::Y(hexa_x_pose.position), boost::qvm::Y(hexa_y_pose.position), boost::qvm::Y(hexa_z_pose.position)},
          {boost::qvm::Z(hexa_origin_pose.position), boost::qvm::Z(hexa_x_pose.position), boost::qvm::Z(hexa_y_pose.position), boost::qvm::Z(hexa_z_pose.position)},
          {1, 1, 1, 1}
        };

        boost::qvm::mat<float, 4, 4> objective = {
          {boost::qvm::X(origin_origin_pose.position), boost::qvm::X(origin_x_pose.position), boost::qvm::X(origin_y_pose.position), boost::qvm::X(origin_z_pose.position)},
          {boost::qvm::Y(origin_origin_pose.position), boost::qvm::Y(origin_x_pose.position), boost::qvm::Y(origin_y_pose.position), boost::qvm::Y(origin_z_pose.position)},
          {boost::qvm::Z(origin_origin_pose.position), boost::qvm::Z(origin_x_pose.position), boost::qvm::Z(origin_y_pose.position), boost::qvm::Z(origin_z_pose.position)},
          {1, 1, 1, 1}
        };

        objective_to_hexa = hexa*boost::qvm::inverse(objective);
        hexa_to_objective = boost::qvm::inverse(objective_to_hexa);

        THALAMUS_LOG(info) << "objective_to_hexa";
        THALAMUS_LOG(info) << "  " << boost::qvm::R<0,0>(objective_to_hexa) << " " << boost::qvm::R<0,1>(objective_to_hexa) << " " << boost::qvm::R<0,2>(objective_to_hexa) << " " << boost::qvm::R<0,3>(objective_to_hexa);
        THALAMUS_LOG(info) << "  " << boost::qvm::R<1,0>(objective_to_hexa) << " " << boost::qvm::R<1,1>(objective_to_hexa) << " " << boost::qvm::R<1,2>(objective_to_hexa) << " " << boost::qvm::R<1,3>(objective_to_hexa);
        THALAMUS_LOG(info) << "  " << boost::qvm::R<2,0>(objective_to_hexa) << " " << boost::qvm::R<2,1>(objective_to_hexa) << " " << boost::qvm::R<2,2>(objective_to_hexa) << " " << boost::qvm::R<2,3>(objective_to_hexa);
        THALAMUS_LOG(info) << "  " << boost::qvm::R<3,0>(objective_to_hexa) << " " << boost::qvm::R<3,1>(objective_to_hexa) << " " << boost::qvm::R<3,2>(objective_to_hexa) << " " << boost::qvm::R<3,3>(objective_to_hexa);
        THALAMUS_LOG(info) << "hexa_to_objective";
        THALAMUS_LOG(info) << "  " << boost::qvm::R<0,0>(hexa_to_objective) << " " << boost::qvm::R<0,1>(hexa_to_objective) << " " << boost::qvm::R<0,2>(hexa_to_objective) << " " << boost::qvm::R<0,3>(hexa_to_objective);
        THALAMUS_LOG(info) << "  " << boost::qvm::R<1,0>(hexa_to_objective) << " " << boost::qvm::R<1,1>(hexa_to_objective) << " " << boost::qvm::R<1,2>(hexa_to_objective) << " " << boost::qvm::R<1,3>(hexa_to_objective);
        THALAMUS_LOG(info) << "  " << boost::qvm::R<2,0>(hexa_to_objective) << " " << boost::qvm::R<2,1>(hexa_to_objective) << " " << boost::qvm::R<2,2>(hexa_to_objective) << " " << boost::qvm::R<2,3>(hexa_to_objective);
        THALAMUS_LOG(info) << "  " << boost::qvm::R<3,0>(hexa_to_objective) << " " << boost::qvm::R<3,1>(hexa_to_objective) << " " << boost::qvm::R<3,2>(hexa_to_objective) << " " << boost::qvm::R<3,3>(hexa_to_objective);
        
        ObservableListPtr state_value = std::make_shared<ObservableList>();
        for(auto a : mat_accessors) {
          state_value->push_back(a(hexa_to_objective));
        }
        
        (*state)["hexa_to_objective"].assign(state_value, [] {
          THALAMUS_LOG(info) << "hexa_to_objective commited to state"
        });
      } catch(std::exception& e) {
        THALAMUS_ASSERT(false, "Calibration failed: %s", e.what());
      }
    }

    boost::asio::awaitable<void> move_objective(float x, float y, float z) {
      busy = true;
      Finally f([&]() {
        busy = false;
      });
      auto hexa_pose = co_await read_hexa_pose();
      auto objective_position = hexa_to_objective*hexa_pose.position;

      cv::Quat<float> hexa_rot_quat(boost::qvm::S(hexa_pose.rotation),
                                    boost::qvm::X(hexa_pose.rotation),
                                    boost::qvm::Y(hexa_pose.rotation),
                                    boost::qvm::Z(hexa_pose.rotation));
      auto hexa_rot_mat = hexa_rot_quat.toRotMat4x4();
      boost::qvm::mat<float, 4, 4> hexa_rot_qvm = {
        {hexa_rot_mat(0, 0), hexa_rot_mat(0, 1), hexa_rot_mat(0, 2), hexa_rot_mat(0, 3)},
        {hexa_rot_mat(1, 0), hexa_rot_mat(1, 1), hexa_rot_mat(1, 2), hexa_rot_mat(1, 3)},
        {hexa_rot_mat(2, 0), hexa_rot_mat(2, 1), hexa_rot_mat(2, 2), hexa_rot_mat(2, 3)},
        {hexa_rot_mat(3, 0), hexa_rot_mat(3, 1), hexa_rot_mat(3, 2), hexa_rot_mat(3, 3)}
      };

      auto objective_rot = hexa_to_objective*hexa_rot_qvm;

      auto new_objective_position = objective_position + boost::qvm::vec<float, 3>(x, y, z);

      auto new_hexa_position = objective_to_hexa*new_objective_position;
      co_await move(boost::qvm::X(new_hexa_position), boost::qvm::Y(new_hexa_position), boost::qvm::Z(new_hexa_position));
    }

    boost::asio::serial_port port;

    std::array<std::function<float&(boost::qvm::mat<float, 4, 4>&)>, 16> mat_accessors = {
      [](boost::qvm::mat<float, 4, 4>& m) { return boost::qvm::A<0,0>(m)},
      [](boost::qvm::mat<float, 4, 4>& m) { return boost::qvm::A<0,1>(m)},
      [](boost::qvm::mat<float, 4, 4>& m) { return boost::qvm::A<0,2>(m)},
      [](boost::qvm::mat<float, 4, 4>& m) { return boost::qvm::A<0,3>(m)},

      [](boost::qvm::mat<float, 4, 4>& m) { return boost::qvm::A<1,0>(m)},
      [](boost::qvm::mat<float, 4, 4>& m) { return boost::qvm::A<1,1>(m)},
      [](boost::qvm::mat<float, 4, 4>& m) { return boost::qvm::A<1,2>(m)},
      [](boost::qvm::mat<float, 4, 4>& m) { return boost::qvm::A<1,3>(m)},

      [](boost::qvm::mat<float, 4, 4>& m) { return boost::qvm::A<2,0>(m)},
      [](boost::qvm::mat<float, 4, 4>& m) { return boost::qvm::A<2,1>(m)},
      [](boost::qvm::mat<float, 4, 4>& m) { return boost::qvm::A<2,2>(m)},
      [](boost::qvm::mat<float, 4, 4>& m) { return boost::qvm::A<2,3>(m)},

      [](boost::qvm::mat<float, 4, 4>& m) { return boost::qvm::A<3,0>(m)},
      [](boost::qvm::mat<float, 4, 4>& m) { return boost::qvm::A<3,1>(m)},
      [](boost::qvm::mat<float, 4, 4>& m) { return boost::qvm::A<3,2>(m)},
      [](boost::qvm::mat<float, 4, 4>& m) { return boost::qvm::A<3,3>(m)},
    }

    bool locked = false;

    void on_change(ObservableCollection* source, ObservableCollection::Action, const ObservableCollection::Key& k, const ObservableCollection::Value& v) {
      if(locked) {
        return;
      }
      
      if(source == hexa_to_objective_state.get()) {
        auto key_int = std::get<long long>(k);
        auto value_float = std::get<double>(v);
        mat_accessors[key_int](hexa_to_objective) = value_float;
      }
      
      if(source != state.get()) {
        return;
      }

      auto key_str = std::get<std::string>(k);
      if(key_str == "Running") {
        running = std::get<bool>(v);
        if(running) {
          //port.close();
          //port.open("/dev/ttyACM0");
          //timer.expires_after(16ms);
          //timer.async_wait(std::bind(&Impl::on_timer, this, _1));
        } else {
          //port.close();
          //timer.cancel();
        }
      } else if(key_str == "Motion Tracking Node") {
        auto value_str = std::get<std::string>(v);
        get_node_connection = graph->get_node_scoped(value_str, [&] (std::weak_ptr<Node> weak_node) {
          auto shared_node = weak_node.lock();
          if(!shared_node) {
            return;
          }
          auto motion_capture_node = node_cast<MotionCaptureNode*>(shared_node.get());
          if(!motion_capture_node) {
            return;
          }
          source_connection = motion_capture_node->ready.connect(std::bind(&Impl::on_data, this, _1));
          source_node = motion_capture_node;
        });
        //is_running = std::get<bool>(v);
        //timer.expires_after(16ms);
        //timer.async_wait(std::bind(&Impl::on_timer, this, _1));
      } else if(key_str == "Objective Pose") {
        objective_accumulator.pose = std::get<long long>(v);
      } else if(key_str == "Field Pose") {
        field_accumulator.pose = std::get<long long>(v);
      } else if(key_str == "hexa_to_objective") {
        hexa_to_objective_state = std::get<ObservableListPtr>(v);
        hexa_to_objective_state.recap(std::bind(&Impl::on_change, this, hexa_to_objective_state.get(), _1, _2, _3));
      }
    }
  };

  HexascopeNode::HexascopeNode(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph* graph)
    : impl(new Impl(state, io_context, this, graph)) {}

  HexascopeNode::~HexascopeNode() {}

  std::string HexascopeNode::type_name() {
    return "HEXASCOPE";
  }

  std::chrono::nanoseconds HexascopeNode::time() const {
    return 0ns;
  }

  bool HexascopeNode::prepare() {
    return true;
  }

  //For safety we reduce complexity of the hexascope node by requiring a lock request before allowing control.
  //Before locking the hexascope will refuse all movement commands and after locking it will ignore all observable state updates.
  //This reduces the complexity of hexascope control and shields it from any errors in the state synchronization logic while still
  //allowing configuration through the usual observable bridge API.
  boost::json::value HexascopeNode::process(const boost::json::value& request) {
    boost::json::object response;
    boost::json::object error;
    error["code"] = 0;
    error["message"] = "";
    response["error"] = error;

    auto type = request["type"].as_string();
    if(type == "lock") {
      impl->objective_to_hexa = boost::qvm::inverse(impl->hexa_to_objective);
      impl->locked = true;
      return response;
    }

    if(!impl->locked) {
      auto message = "Hexascope is not locked, operation refused.";
      THALAMUS_LOG(warning) << message;
      error["code"] = -1;
      error["message"] = message;
      return response;
    }

    if(impl->busy) {
      auto message = "Hexascope is busy, operation refused.";
      THALAMUS_LOG(warning) << message;
      error["code"] = -2;
      error["message"] = message;
      return response;
    }

    if(type == "calibrate") {
      boost::asio::co_spawn(impl->io_context, impl->calibrate(), boost::asio::detached);
    } else if(type == "get_hexa_to_objective") {
      boost::json::array value;
      for(auto a : impl->mat_accessors) {
        value.push_back(a(impl->hexa_to_objective));
      }
      response["hexa_to_objective"] = value;
    } else if(type == "align") {
      auto value = request["value"].as_array();
      boost::asio::co_spawn(impl->io_context, impl->align(), boost::asio::detached);
    } else if(type == "move_objective") {
      auto value = request["value"].as_array();
      auto x = value[0].as_float();
      auto y = value[1].as_float();
      auto z = value[2].as_float();
      boost::asio::co_spawn(impl->io_context, impl->move_objective(x, y, z), boost::asio::detached);
    }
    return response;
  }

  size_t HexascopeNode::modalities() const { return infer_modalities<HexascopeNode>(); }
}
