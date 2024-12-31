#include <hexascope_node.hpp>
#include <thread_pool.hpp>
#include <boost/pool/object_pool.hpp>
#include <modalities_util.hpp>
#include <thalamus/async.hpp>
#include <boost/qvm/all.hpp>
#include <opencv2/core/quaternion.hpp>
#include <opencv2/core/matx.hpp>

namespace thalamus {
  using namespace std::chrono_literals;

  struct HexascopeNode::Impl {
    boost::asio::io_context& io_context;
    ObservableDictPtr state;
    ObservableListPtr hexa_to_camera_state;
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

    Impl(ObservableDictPtr state, boost::asio::io_context& io_context, HexascopeNode* outer, NodeGraph* graph)
      : io_context(io_context)
      , state(state)
      , outer(outer)
      , graph(graph)
      , pool(graph->get_thread_pool())
      , timer(io_context)
      , orient_timer(io_context)
      , port(io_context) {
      using namespace std::placeholders;
      state_connection = state->recursive_changed.connect(std::bind(&Impl::on_change, this, _1, _2, _3, _4));
      this->state->recap(std::bind(&Impl::on_change, this, state.get(), _1, _2, _3));
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
        port.set_option(boost::asio::serial_port::baud_rate(115200));
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

      std::string to_string() {
        std::stringstream stream;
        stream << (ccw_jog ? "1" : "0");
        stream << (cw_jog ? "1" : "0");
        stream << (ccw_move ? "1" : "0");
        stream << (cw_move ? "1" : "0");
        stream << (ccw_soft_limit ? "1" : "0");
        stream << (cw_soft_limit ? "1" : "0");
        stream << (ccw_limit ? "1" : "0");
        stream << (cw_limit ? "1" : "0");
        stream << (homing ? "1" : "0");
        stream << (homed ? "1" : "0");
        stream << (interlock ? "1" : "0");
        stream << " " << step_counts;
        stream << " " << encoder_counts;
        return stream.str();
      }
    };
     
    boost::asio::awaitable<void> go_home() {
      co_await move(0, 0, 0, 0, 0, 0);
    }
    boost::asio::awaitable<void> rotate(float theta, float phi, float psi) {
      auto current_pose = co_await read_hexa_pose_raw();
      co_await move(current_pose[0], current_pose[1], current_pose[2], theta, phi, psi);
    }

    boost::asio::awaitable<void> move(float x, float y, float z) {
      auto current_pose = co_await read_hexa_pose_raw();
      co_await move(x, y, z, current_pose[3], current_pose[4], current_pose[5]);
    }

    boost::asio::awaitable<void> home() {
      std::vector<std::tuple<float, float, float, float, float, float>> points = {
        { 0, 0, 0, 0, 0, 0},
        {10, 0, 0, 0, 0, 0},
        { 0,10, 0, 0, 0, 0},
        { 0, 0,10, 0, 0, 0},
      };
      unsigned char buffer[20];

      THALAMUS_LOG(info) << "Homing";
      boost::asio::steady_timer timer(io_context);
      while(true) {
        for(auto point : points) {
          co_await move(std::get<0>(point), std::get<1>(point), std::get<2>(point), std::get<3>(point), std::get<4>(point), std::get<5>(point));
          timer.expires_after(60s);
          co_await timer.async_wait(boost::asio::use_awaitable);
          Leg leg;
          auto homed = true;
          std::stringstream status;
          for(auto i = 0;i < 6;++i) {
            buffer[0] = 0x80;
            buffer[1] = 0x04;
            buffer[2] = 0x06;
            buffer[3] = 0x00;
            buffer[4] = 0x20 + i + 1;
            buffer[5] = 0x10;

            co_await boost::asio::async_write(port, boost::asio::buffer(buffer, 6), boost::asio::use_awaitable);
            co_await boost::asio::async_read(port, boost::asio::buffer(buffer, 20), boost::asio::use_awaitable);
            leg.load_leg_response(buffer);
            homed = homed && leg.homed;
            status << leg.to_string() << "\n";
          }
          THALAMUS_LOG(info) << "homing status\n" << status.str();
          if(homed) {
            THALAMUS_LOG(info) << "Homed";
            co_return;
          }
        }
      }
    }

    bool reading = false;
    boost::asio::awaitable<void> move(float x, float y, float z, float theta, float phi, float psi) {
      co_await move_inner(x, y, z, theta, phi, psi, false);
    }
    boost::asio::awaitable<void> move_with_rehoming(float x, float y, float z, float theta, float phi, float psi) {
      co_await move_inner(x, y, z, theta, phi, psi, true);
    }

    boost::asio::awaitable<void> move_inner(float x, float y, float z, float theta, float phi, float psi, bool rehoming) {
      open_port();
      auto message_size = 38;
      //Allocate float array then reinterpret as a byte array so the bytes are float aligned
      float floats[2 //header (8 bytes)
                   + 6 //orientation (6 floats)
                   + 2 //tail (need 6 bytes so allocate 2 floats, which gets 8 bytes)
                   ];
      auto buffer = reinterpret_cast<unsigned char*>(floats);
      std::fill(buffer, buffer + sizeof(floats), 0);

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
      THALAMUS_LOG(info) << "Moving to " << floats[2] << " " << floats[3] << " " << floats[4]<< " "
                                         << floats[5] << " " << floats[6] << " " << floats[7] << " ";
      co_await boost::asio::async_write(port, boost::asio::buffer(buffer, message_size), boost::asio::use_awaitable);

      //if(reading) {
      //  co_return;
      //}
      //reading = true;
      //Finally f([&] {
      //  reading = false;
      //});

      Leg leg;
      boost::asio::steady_timer timer(io_context);
      bool moving = true;
      while(moving) {
        timer.expires_after(1s);
        co_await timer.async_wait(boost::asio::use_awaitable);
        moving = false;
        std::stringstream status;
        for(auto i = 0;i < 6;++i) {
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
          status << leg.to_string() << "\n";
          //if(!leg.homed) {
          //  if(rehoming) {
          //    co_await home();
          //  } else {
          //    THALAMUS_LOG(info) << "homing lost\n" << status.str();
          //    co_return;
          //  }
          //}
        }
        THALAMUS_LOG(info) << "Waiting for stop\n" << status.str();
      }
      THALAMUS_LOG(info) << "stopped";
      co_return;
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
      accumulator.accumulating = false;
      if(accumulator.position.empty()) {
        co_return std::nullopt;
      }

      boost::qvm::vec<float, 3> position_mean;
      for(auto& pos : accumulator.position) {
        position_mean += pos;
      }
      position_mean /= accumulator.position.size();

      float min_distance = std::numeric_limits<float>::max();
      size_t median = std::numeric_limits<size_t>::max();

      for(auto i = 0ull;i < accumulator.position.size();++i) {
        float distance = boost::qvm::mag_sqr(accumulator.position[i] - position_mean);
        if(distance < min_distance) {
          min_distance = distance;
          median = i;
        }
      }

      co_return Pose {accumulator.position[median], accumulator.rotation[median]};
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

    boost::asio::awaitable<std::array<float, 6>> read_hexa_pose_raw() {
      open_port();
      //Allocate float array then reinterpret as a byte array so the bytes are float aligned
      float floats[2 //header (8 bytes)
                   + 6 //orientation (6 floats)
                   ];
      auto buffer = reinterpret_cast<unsigned char*>(floats);
      std::fill(buffer, buffer + sizeof(floats), 0);
      buffer[0] = 0xA1;
      buffer[1] = 0x40;
      buffer[2] = 0x06;
      buffer[3] = 0x00;
      buffer[4] = 0x29;
      buffer[5] = 0x01;
      //THALAMUS_LOG(info) << "sizeof(buffer) " << sizeof(buffer);
      co_await boost::asio::async_write(port, boost::asio::buffer(buffer, 6), boost::asio::use_awaitable);
      co_await boost::asio::async_read(port, boost::asio::buffer(buffer, 32), boost::asio::use_awaitable);

      co_return std::array<float, 6>{floats[2], floats[3], floats[4], floats[5], floats[6], floats[7]};
    }

    boost::asio::awaitable<Pose> read_hexa_pose() {
      //Allocate float array then reinterpret as a byte array so the bytes are float aligned
      auto raw = co_await read_hexa_pose_raw();

      boost::qvm::vec<float, 3> position{raw[0], raw[1], raw[2]};
      position /= 1000;

      cv::Vec3f euler_angles{raw[3], raw[4], raw[5]};
      euler_angles *= -M_PI/180;
      auto cv_quaternion = cv::Quat<float>::createFromEulerAngles(euler_angles, cv::QuatEnum::EulerAnglesType::EXT_XYZ);
      boost::qvm::quat<float> boost_quaternion;
      boost::qvm::S(boost_quaternion) = cv_quaternion.w;
      boost::qvm::X(boost_quaternion) = cv_quaternion.x;
      boost::qvm::Y(boost_quaternion) = cv_quaternion.y;
      boost::qvm::Z(boost_quaternion) = cv_quaternion.z;

      co_return Pose { position, boost_quaternion };
    }

    boost::qvm::mat<float, 4, 4> camera_to_hexa;
    boost::qvm::mat<float, 4, 4> hexa_to_camera;

    const float CALIBRATION_STEP_SIZE = 40;

    boost::qvm::mat<float, 4, 4> quat_to_mat(const boost::qvm::quat<float>& arg) {
      cv::Quat<float> quat(boost::qvm::S(arg),
                           boost::qvm::X(arg),
                           boost::qvm::Y(arg),
                           boost::qvm::Z(arg));
      auto mat = quat.toRotMat4x4();
      boost::qvm::mat<float, 4, 4> qvm = {{
        {mat(0, 0), mat(0, 1), mat(0, 2), mat(0, 3)},
        {mat(1, 0), mat(1, 1), mat(1, 2), mat(1, 3)},
        {mat(2, 0), mat(2, 1), mat(2, 2), mat(2, 3)},
        {mat(3, 0), mat(3, 1), mat(3, 2), mat(3, 3)}
      }};
      return qvm;
    }

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
      auto camera_position = hexa_to_camera*boost::qvm::XYZ1(hexa_pose.position);

      auto hexa_rot_qvm = quat_to_mat(hexa_pose.rotation);

      auto camera_rot = hexa_to_camera*hexa_rot_qvm;

      auto field_z_axis = field_pose.rotation * boost::qvm::vec<float,3>{1, 0, 0};
      auto camera_z_axis = camera_rot * boost::qvm::vec<float,4>{1, 0, 0, 1};
      auto z_dot = boost::qvm::dot(boost::qvm::XYZ(camera_z_axis), field_z_axis);
      if(z_dot < 0) {
        z_dot *= -1;
        field_z_axis *= -1;
      }
      auto rotation_axis = boost::qvm::cross(boost::qvm::XYZ(camera_z_axis), field_z_axis);
      auto rotation_angle = acos(z_dot);
      auto camera_rotation_vector = rotation_angle*rotation_axis;
      auto hexa_rotation_vector = camera_to_hexa*boost::qvm::XYZ1(camera_rotation_vector);

      cv::Vec3f rvec{boost::qvm::X(hexa_rotation_vector), boost::qvm::Y(hexa_rotation_vector), boost::qvm::Z(hexa_rotation_vector)};
      auto quaternion = cv::Quat<float>::createFromRvec(rvec);
      auto eulerAngles = quaternion.toEulerAngles(cv::QuatEnum::EulerAnglesType::EXT_XYZ);
      eulerAngles *= -180/M_PI;

      co_await rotate(eulerAngles[0], eulerAngles[1], eulerAngles[2]);

      auto objective_to_field = field_pose.position - boost::qvm::XYZ(camera_position);
      
      auto camera_x_axis = camera_rot * boost::qvm::vec<float,4>{1, 0, 0, 1};
      auto camera_y_axis = camera_rot * boost::qvm::vec<float,4>{0, 1, 0, 1};

      auto x = boost::qvm::dot(objective_to_field, boost::qvm::XYZ(camera_x_axis));
      auto y = boost::qvm::dot(objective_to_field, boost::qvm::XYZ(camera_y_axis));

      co_await move_objective(x, y, 0);
    }
     
    boost::asio::awaitable<void> calibrate() {
      busy = true;
      Finally f([&]() {
        busy = false;
      });
      
      try {
        co_await move_with_rehoming(0, 0, 0, 0, 0, 0);
        auto camera_origin_pose_opt = co_await measure_objective_pose(1s);
        if(!camera_origin_pose_opt) {
          THALAMUS_LOG(warning) << "No position samples received";
          co_return;
        }
        auto camera_origin_pose = *camera_origin_pose_opt;
        THALAMUS_LOG(info) << "camera_origin_pose " << boost::qvm::X(camera_origin_pose.position) << " " << boost::qvm::Y(camera_origin_pose.position) << " " << boost::qvm::Z(camera_origin_pose.position);
        auto hexa_origin_pose = co_await read_hexa_pose();
        THALAMUS_LOG(info) << "hexa_origin_pose " << boost::qvm::X(hexa_origin_pose.position) << " " << boost::qvm::Y(hexa_origin_pose.position) << " " << boost::qvm::Z(hexa_origin_pose.position);

        co_await move_with_rehoming(CALIBRATION_STEP_SIZE, 0, 0, 0, 0, 0);
        auto camera_x_pose_opt = co_await measure_objective_pose(1s);
        if(!camera_x_pose_opt) {
          THALAMUS_LOG(warning) << "No position samples received";
          co_return;
        }
        auto camera_x_pose = *camera_x_pose_opt;
        THALAMUS_LOG(info) << "camera_x_pose " << boost::qvm::X(camera_x_pose.position) << " " << boost::qvm::Y(camera_x_pose.position) << " " << boost::qvm::Z(camera_x_pose.position);
        auto hexa_x_pose = co_await read_hexa_pose();
        THALAMUS_LOG(info) << "hexa_x_pose " << boost::qvm::X(hexa_x_pose.position) << " " << boost::qvm::Y(hexa_x_pose.position) << " " << boost::qvm::Z(hexa_x_pose.position);

        co_await move_with_rehoming(0, CALIBRATION_STEP_SIZE, 0, 0, 0, 0);
        auto camera_y_pose_opt = co_await measure_objective_pose(1s);
        if(!camera_y_pose_opt) {
          THALAMUS_LOG(warning) << "No position samples received";
          co_return;
        }
        auto camera_y_pose = *camera_y_pose_opt;
        THALAMUS_LOG(info) << "camera_y_pose " << boost::qvm::X(camera_y_pose.position) << " " << boost::qvm::Y(camera_y_pose.position) << " " << boost::qvm::Z(camera_y_pose.position);
        auto hexa_y_pose = co_await read_hexa_pose();
        THALAMUS_LOG(info) << "hexa_y_pose " << boost::qvm::X(hexa_y_pose.position) << " " << boost::qvm::Y(hexa_y_pose.position) << " " << boost::qvm::Z(hexa_y_pose.position);

        co_await move_with_rehoming(0, 0, CALIBRATION_STEP_SIZE, 0, 0, 0);
        auto camera_z_pose_opt = co_await measure_objective_pose(1s);
        if(!camera_z_pose_opt) {
          THALAMUS_LOG(warning) << "No position samples received";
          co_return;
        }
        auto camera_z_pose = *camera_z_pose_opt;
        THALAMUS_LOG(info) << "camera_z_pose " << boost::qvm::X(camera_z_pose.position) << " " << boost::qvm::Y(camera_z_pose.position) << " " << boost::qvm::Z(camera_z_pose.position);
        auto hexa_z_pose = co_await read_hexa_pose();
        THALAMUS_LOG(info) << "hexa_z_pose " << boost::qvm::X(hexa_z_pose.position) << " " << boost::qvm::Y(hexa_z_pose.position) << " " << boost::qvm::Z(hexa_z_pose.position);

        boost::qvm::mat<float, 4, 4> hexa = {{
          {boost::qvm::X(hexa_origin_pose.position), boost::qvm::X(hexa_x_pose.position), boost::qvm::X(hexa_y_pose.position), boost::qvm::X(hexa_z_pose.position)},
          {boost::qvm::Y(hexa_origin_pose.position), boost::qvm::Y(hexa_x_pose.position), boost::qvm::Y(hexa_y_pose.position), boost::qvm::Y(hexa_z_pose.position)},
          {boost::qvm::Z(hexa_origin_pose.position), boost::qvm::Z(hexa_x_pose.position), boost::qvm::Z(hexa_y_pose.position), boost::qvm::Z(hexa_z_pose.position)},
          {1, 1, 1, 1}
        }};

        boost::qvm::mat<float, 4, 4> camera = {{
          {boost::qvm::X(camera_origin_pose.position), boost::qvm::X(camera_x_pose.position), boost::qvm::X(camera_y_pose.position), boost::qvm::X(camera_z_pose.position)},
          {boost::qvm::Y(camera_origin_pose.position), boost::qvm::Y(camera_x_pose.position), boost::qvm::Y(camera_y_pose.position), boost::qvm::Y(camera_z_pose.position)},
          {boost::qvm::Z(camera_origin_pose.position), boost::qvm::Z(camera_x_pose.position), boost::qvm::Z(camera_y_pose.position), boost::qvm::Z(camera_z_pose.position)},
          {1, 1, 1, 1}
        }};

        camera_to_hexa = hexa*boost::qvm::inverse(camera);
        hexa_to_camera = boost::qvm::inverse(camera_to_hexa);

        THALAMUS_LOG(info) << "camera_to_hexa";
        THALAMUS_LOG(info) << "  " << boost::qvm::A<0,0>(camera_to_hexa) << " " << boost::qvm::A<0,1>(camera_to_hexa) << " " << boost::qvm::A<0,2>(camera_to_hexa) << " " << boost::qvm::A<0,3>(camera_to_hexa);
        THALAMUS_LOG(info) << "  " << boost::qvm::A<1,0>(camera_to_hexa) << " " << boost::qvm::A<1,1>(camera_to_hexa) << " " << boost::qvm::A<1,2>(camera_to_hexa) << " " << boost::qvm::A<1,3>(camera_to_hexa);
        THALAMUS_LOG(info) << "  " << boost::qvm::A<2,0>(camera_to_hexa) << " " << boost::qvm::A<2,1>(camera_to_hexa) << " " << boost::qvm::A<2,2>(camera_to_hexa) << " " << boost::qvm::A<2,3>(camera_to_hexa);
        THALAMUS_LOG(info) << "  " << boost::qvm::A<3,0>(camera_to_hexa) << " " << boost::qvm::A<3,1>(camera_to_hexa) << " " << boost::qvm::A<3,2>(camera_to_hexa) << " " << boost::qvm::A<3,3>(camera_to_hexa);
        THALAMUS_LOG(info) << "hexa_to_camera";
        THALAMUS_LOG(info) << "  " << boost::qvm::A<0,0>(hexa_to_camera) << " " << boost::qvm::A<0,1>(hexa_to_camera) << " " << boost::qvm::A<0,2>(hexa_to_camera) << " " << boost::qvm::A<0,3>(hexa_to_camera);
        THALAMUS_LOG(info) << "  " << boost::qvm::A<1,0>(hexa_to_camera) << " " << boost::qvm::A<1,1>(hexa_to_camera) << " " << boost::qvm::A<1,2>(hexa_to_camera) << " " << boost::qvm::A<1,3>(hexa_to_camera);
        THALAMUS_LOG(info) << "  " << boost::qvm::A<2,0>(hexa_to_camera) << " " << boost::qvm::A<2,1>(hexa_to_camera) << " " << boost::qvm::A<2,2>(hexa_to_camera) << " " << boost::qvm::A<2,3>(hexa_to_camera);
        THALAMUS_LOG(info) << "  " << boost::qvm::A<3,0>(hexa_to_camera) << " " << boost::qvm::A<3,1>(hexa_to_camera) << " " << boost::qvm::A<3,2>(hexa_to_camera) << " " << boost::qvm::A<3,3>(hexa_to_camera);
        
        ObservableListPtr state_value = std::make_shared<ObservableList>();
        for(auto a : mat_accessors) {
          state_value->push_back(a(hexa_to_camera));
        }
        
        (*state)["hexa_to_camera"].assign(state_value, [] {
          THALAMUS_LOG(info) << "hexa_to_camera commited to state";
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
      auto camera_position = hexa_to_camera*boost::qvm::XYZ1(hexa_pose.position);

      boost::qvm::mat<float, 4, 4> hexa_rot_qvm = quat_to_mat(hexa_pose.rotation);

      auto camera_rot = hexa_to_camera*hexa_rot_qvm;
      auto camera_offset = camera_rot*boost::qvm::vec<float, 4>{x, y, z, 1};

      auto new_camera_position = boost::qvm::XYZ(camera_position) + boost::qvm::XYZ(camera_offset);

      auto new_hexa_position = camera_to_hexa*boost::qvm::XYZ1(new_camera_position);
      co_await move(1000*boost::qvm::X(new_hexa_position), 1000*boost::qvm::Y(new_hexa_position), 1000*boost::qvm::Z(new_hexa_position));
    }

    boost::asio::serial_port port;

    std::array<std::function<float&(boost::qvm::mat<float, 4, 4>&)>, 16> mat_accessors = {
      [](boost::qvm::mat<float, 4, 4>& m) -> float& { return boost::qvm::A<0,0>(m);},
      [](boost::qvm::mat<float, 4, 4>& m) -> float& { return boost::qvm::A<0,1>(m);},
      [](boost::qvm::mat<float, 4, 4>& m) -> float& { return boost::qvm::A<0,2>(m);},
      [](boost::qvm::mat<float, 4, 4>& m) -> float& { return boost::qvm::A<0,3>(m);},

      [](boost::qvm::mat<float, 4, 4>& m) -> float& { return boost::qvm::A<1,0>(m);},
      [](boost::qvm::mat<float, 4, 4>& m) -> float& { return boost::qvm::A<1,1>(m);},
      [](boost::qvm::mat<float, 4, 4>& m) -> float& { return boost::qvm::A<1,2>(m);},
      [](boost::qvm::mat<float, 4, 4>& m) -> float& { return boost::qvm::A<1,3>(m);},

      [](boost::qvm::mat<float, 4, 4>& m) -> float& { return boost::qvm::A<2,0>(m);},
      [](boost::qvm::mat<float, 4, 4>& m) -> float& { return boost::qvm::A<2,1>(m);},
      [](boost::qvm::mat<float, 4, 4>& m) -> float& { return boost::qvm::A<2,2>(m);},
      [](boost::qvm::mat<float, 4, 4>& m) -> float& { return boost::qvm::A<2,3>(m);},

      [](boost::qvm::mat<float, 4, 4>& m) -> float& { return boost::qvm::A<3,0>(m);},
      [](boost::qvm::mat<float, 4, 4>& m) -> float& { return boost::qvm::A<3,1>(m);},
      [](boost::qvm::mat<float, 4, 4>& m) -> float& { return boost::qvm::A<3,2>(m);},
      [](boost::qvm::mat<float, 4, 4>& m) -> float& { return boost::qvm::A<3,3>(m);},
    };

    bool locked = false;

    void on_change(ObservableCollection* source, ObservableCollection::Action, const ObservableCollection::Key& k, const ObservableCollection::Value& v) {
      if(locked) {
        return;
      }
      
      if(source == hexa_to_camera_state.get()) {
        auto key_int = std::get<long long>(k);
        auto value_float = std::get<double>(v);
        mat_accessors[key_int](hexa_to_camera) = value_float;
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
          source_connection = shared_node->ready.connect(std::bind(&Impl::on_data, this, _1));
          source_node = motion_capture_node;
        });
        //is_running = std::get<bool>(v);
        //timer.expires_after(16ms);
        //timer.async_wait(std::bind(&Impl::on_timer, this, _1));
      } else if(key_str == "Objective Pose") {
        objective_accumulator.pose = std::get<long long>(v);
      } else if(key_str == "Field Pose") {
        field_accumulator.pose = std::get<long long>(v);
      } else if(key_str == "hexa_to_camera") {
        hexa_to_camera_state = std::get<ObservableListPtr>(v);
        hexa_to_camera_state->recap(std::bind(&Impl::on_change, this, hexa_to_camera_state.get(), _1, _2, _3));
      }
    }
  };

  HexascopeNode::HexascopeNode(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph* graph)
    : impl(new Impl(state, io_context, this, graph)) {}

  HexascopeNode::~HexascopeNode() {}

  std::string HexascopeNode::type_name() {
    return "HEXASCOPE";
  }

  //std::chrono::nanoseconds HexascopeNode::time() const {
  //  return 0ns;
  //}

  bool HexascopeNode::prepare() {
    return true;
  }

  //For safety we reduce complexity of the hexascope node by requiring a lock request before allowing control.
  //Before locking the hexascope will refuse all movement commands and after locking it will ignore all observable state updates.
  //This reduces the complexity of hexascope control and shields it from any errors in the state synchronization logic while still
  //allowing configuration through the usual observable bridge API.
  boost::json::value HexascopeNode::process(const boost::json::value& request_value) {
    auto request = request_value.as_object();

    boost::json::object response;
    boost::json::object error;
    error["code"] = 0;
    error["message"] = "";
    response["error"] = error;

    auto type = request["type"].as_string();
    if(type == "lock") {
      impl->camera_to_hexa = boost::qvm::inverse(impl->hexa_to_camera);
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
    } else if(type == "get_hexa_to_camera") {
      boost::json::array value;
      for(auto a : impl->mat_accessors) {
        auto val = a(impl->hexa_to_camera);
        value.push_back(val);
        THALAMUS_LOG(info) << val;
      }
      response["hexa_to_camera"] = value;
    } else if(type == "align") {
      boost::asio::co_spawn(impl->io_context, impl->align(), boost::asio::detached);
    } else if(type == "move_objective") {
      auto value = request["value"].as_array();
      auto x = value[0].as_double();
      auto y = value[1].as_double();
      auto z = value[2].as_double();
      boost::asio::co_spawn(impl->io_context, impl->move_objective(x, y, z), boost::asio::detached);
    } else if(type == "move_hexa") {
      auto value = request["value"].as_array();
      auto x = value[0].as_double();
      auto y = value[1].as_double();
      auto z = value[2].as_double();
      auto x2 = value[3].as_double();
      auto y2 = value[4].as_double();
      auto z2 = value[5].as_double();
      boost::asio::co_spawn(impl->io_context, impl->move(x, y, z, x2, y2, z2), boost::asio::detached);
    }
    return response;
  }

  size_t HexascopeNode::modalities() const { return infer_modalities<HexascopeNode>(); }
}
