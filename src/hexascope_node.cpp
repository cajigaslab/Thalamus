#include <hexascope_node.hpp>
#include <thread_pool.h>
#include <boost/pool/object_pool.hpp>
#include <modalities_util.h>

namespace thalamus {
  using namespace std::chrono_literals;

  struct HexascopeNode::Impl {
    boost::asio::io_context& io_context;
    ObservableDictPtr state;
    boost::signals2::scoped_connection state_connection;
    boost::signals2::scoped_connection source_connection;
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

    enum class State {
      IDLE,
      SEARCHING_THETA_POS,
      SEARCHING_THETA_NEG,
      SEARCHING_PHI_POS,
      SEARCHING_PHI_NEG,
      SEARCHING_PSI_POS,
      SEARCHING_PSI_NEG
    } state;

    Impl(ObservableDictPtr state, boost::asio::io_context& io_context, ChessBoardNode* outer, NodeGraph* graph)
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
      state_connection = state->changed.connect(std::bind(&Impl::on_change, this, _1, _2, _3));
      this->state->recap(std::bind(&Impl::on_change, this, _1, _2, _3));
    }

    ~Impl() {
      (*state)["Running"].assign(false, [&] {});
    }

    alignas(float) unsigned char response[32];
    std::list<std::function<void()>> jobs;
    bool working = false;

    struct GetPose {
      Impl impl;
      std::function<void()> callback;
      alignas(float) unsigned char buffer[32];

      void on_read(boost::system::error_code& e, size_t transferred) {
        if(e) {
          THALAMUS_LOG(error) << e.what();
          (*impl->state)["Running"].assign(false);
          return;
        }

        auto floats = reinterpret_cast<float*>(buffer);
        impl->hexa.x = floats[2];
        impl->hexa.y = floats[3];
        impl->hexa.z = floats[4];
        impl->hexa.theta = floats[5];
        impl->hexa.phi = floats[6];
        impl->hexa.psi = floats[7];
        impl->hexa.ready = true;

        if(callback) {
          callback();
        }
        impl->next_job();
      }

      void on_write(boost::system::error_code& e, size_t transferred) {
        if(e) {
          THALAMUS_LOG(error) << e.what();
          (*impl->state)["Running"].assign(false);
          return;
        }

        boost::asio::async_read(impl->port, boost::asio::buffer(buffer, 32), std::bind(&GetPose::on_read, this, _1, _2));
      }

      void operator()() {
        buffer[0] = 0xa1;
        buffer[1] = 0x40;
        buffer[2] = 0x06;
        buffer[3] = 0x00;
        buffer[4] = 0x29;
        buffer[5] = 0x01;
        boost::asio::async_write(impl->port, boost::asio::buffer(buffer, 6), std::bind(&GetPose::on_write, this, _1, _2));
      }
    }

    void next_job() {
      jobs.pop_front()
      if(!jobs.empty()) {
        jobs.front()();
        return
      }
      working = false;
    }

    void submit_job(std::function<void()> job) {
      jobs.push_back(job);
      if(!working) {
        jobs.front()();
        working = true;
      }
    }

    struct Hexascope {
      float x = 0;
      float y = 0;
      float z = 0;
      float theta = 0;
      float phi = 0;
      float psi = 0;
      bool ready = false;
    } hexa;

    void on_timer(const boost::system::error_code& e) {
      if(e) {
        THALAMUS_LOG(error) << e.what();
        (*state)["Running"].assign(false);
        return;
      }
      submit_job(GetPose{this});
    }

    void on_data(Node*) {
      if(!source_node->has_motion_data()) {
        return;
      }
      auto i = std::remove_if(objective_translations.begin(), objective_translations.end(), [&] (auto& arg) {
        return source_node->time() - arg.first > 1s;
      });
      objective_translations.erase(i, objective_translations.end());
      auto i = std::remove_if(field_translations.begin(), field_translations.end(), [&] (auto& arg) {
        return source_node->time() - arg.first > 1s;
      });
      field_translations.erase(i, field_translations.end());

      auto mean_objective_translation = std::accumulate(objective_translations.begin(), objective_translations.end(), [](auto& a, auto &b) {
        return a + b.second;
      }, boost:qvm::vec<float, 3>{0,0,0});
      mean_objective_translation /= objective_translations.size();

      auto mean_field_translation = std::accumulate(field_translations.begin(), field_translations.end(), [](auto& a, auto &b) {
        return a + b.second;
      }, boost:qvm::vec<float, 3>{0,0,0});
      mean_field_translation /= field_translations.size();

      std::optional<boost:qvm::vec<float, 3>> new_objective_translation;
      std::optional<boost:qvm::vec<float, 3>> new_field_translation;
      std::optional<boost:qvm::quat<float>> new_objective_rotation;
      std::optional<boost:qvm::quat<float>> new_field_rotation;
      for(auto& segment : source_node->segments()) {
        if(segment.segment_id == objective_pose) {
          new_objective_translation = segment.position;
          new_objective_rotation = segment.rotation;
        } else if (segment.segment_id == field_pose) {
          new_field_translation = segment.position;
          new_field_rotation = segment.rotation;
        }
      }

      if(new_objective_rotation && boost:qvm::vec<float, 3>::mag(*new_objective_translation - mean_objective_tranlsation) > .01) {
        objective_rotation = new_objective_rotation;
        objective_translations.emplace_back(source_node->time(), *new_objective_translation);
      }
      if(new_field_rotation && boost:qvm::vec<float, 3>::mag(*new_field_translation - mean_field_tranlsation) > .01) {
        field_rotation = new_field_rotation;
        field_translations.emplace_back(source_node->time(), *new_field_translation);
      }

      if(objective_rotation && field_rotation) {
        auto diff = (*field_rotation)*boost::qvm::inverse(*objective_rotation)
        error = boost::qvm::S(diff);
      }
    }

    void calibrate_rotation() {
      
    }

    void orient() {
      if(!running) {
        THALAMUS_LOG(warning) << "Not connected to hexascope";
        return;
      }

      if(!hexa.ready) {
        submit_job(GetPose{this, std::bind(&Impl::orient, this)});
        return;
      }

      switch(state) {
        case State::IDLE:
          submit_job(SetPose{this, hexa.x, hexa.y, hexa.z, hexa.theta + 20, hexa.phi, hexa.psi});
          state = State::SEARCHING_THETA_POS;
          break;
        case State::SEARCHING_THETA_POS:
          if(error < last_error) {
            break;
          }
          submit_job(SetPose{this, hexa.x, hexa.y, hexa.z, hexa.theta - 20, hexa.phi, hexa.psi});
          state = State::SEARCHING_THETA_NEG;
          break;
        case State::SEARCHING_THETA_NEG:
          if(error < last_error) {
            break;
          }
          submit_job(SetPose{this, hexa.x, hexa.y, hexa.z, hexa.theta, hexa.phi+20, hexa.psi});
          state = State::SEARCHING_PHI_POS;
          break;
        case State::SEARCHING_PHI_POS:
          if(error < last_error) {
            break;
          }
          submit_job(SetPose{this, hexa.x, hexa.y, hexa.z, hexa.theta, hexa.phi-20, hexa.psi});
          state = State::SEARCHING_PHI_NEG;
          break;
        case State::SEARCHING_PHI_NEG:
          if(error < last_error) {
            break;
          }
          submit_job(SetPose{this, hexa.x, hexa.y, hexa.z, hexa.theta, hexa.phi, hexa.psi+20});
          state = State::SEARCHING_PSI_POS;
          break;
        case State::SEARCHING_PSI_POS:
          if(error < last_error) {
            break;
          }
          submit_job(SetPose{this, hexa.x, hexa.y, hexa.z, hexa.theta, hexa.phi, hexa.psi-20});
          state = State::SEARCHING_PSI_NEG;
          break;
        case State::SEARCHING_PSI_NEG:
          if(error < last_error) {
            break;
          }
          submit_job(Stop{this});
          state = State::IDLE;
          return;
      }

      last_error = error;
      orient_timer.expires_after(16ms);
      orient_timer.async_await(std::bind(&Impl::orient, this));
    }

    void ascend(int mm) {
      submit_job(MoveFocus{this, hexa.x + mm, hexa.y, hexa.z});
    }

    boost::asio::serial_port port;

    void on_change(ObservableCollection::Action, const ObservableCollection::Key& k, const ObservableCollection::Value& v) {
      auto key_str = std::get<std::string>(k);
      if(key_str == "Running") {
        running = std::get<bool>(v);
        if(running) {
          port.close();
          port.open("/dev/ttyACM0");
          timer.expires_after(16ms);
          timer.async_wait(std::bind(&Impl::on_timer, this, _1));
        } else {
          port.close();
          timer.cancel();
          hexa.ready = false;
        }
      } else if(key_str == "Motion Tracking Node") {
        auto value_str = std::get<std::string>(v);
        graph->get_node_scoped(value_str, [&] (std::weak_ptr<Node> weak_node) {
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
        is_running = std::get<bool>(v);
        timer.expires_after(16ms);
        timer.async_wait(std::bind(&Impl::on_timer, this, _1));
      } else if(key_str == "Objective Pose") {
        objective_pose = std::get<long long>(v);
      } else if(key_str == "Field Pose") {
        field_pose = std::get<long long>(v);
      }
    }
  };

  HexascopeNode::HexascopeNode(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph* graph)
    : impl(new Impl(state, io_context, this, graph)) {}

  HexascopeNode::~HexascopeNode() {}

  std::string HexascopeNode::type_name() {
    return "HEXASCOPE";
  }

  std::chrono::nanoseconds ChessBoardNode::time() const {
    return 0ns;
  }

  bool HexascopeNode::prepare() {
    return true;
  }

  boost::json::value ChessBoardNode::process(const boost::json::value& request) {
    auto type = request["type"].as_string();
    if(type == "orient") {
      impl->orient();
    } else if(type == "ascend") {
      impl->ascend(request["mm"].as_int());
    } else if(type == "descend") {
      impl->descend(request["mm"].as_int());
    }
  }

  size_t ChessBoardNode::modalities() const { return 0; }
}
