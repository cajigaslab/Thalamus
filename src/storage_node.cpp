#include <storage_node.h>
#include <image_node.h>
#include <text_node.h>
#include <fstream>
#include <util.hpp>
#include <absl/strings/str_format.h>
#include <absl/time/time.h>
#include <boost/qvm/vec_access.hpp>
#include <boost/qvm/quat_access.hpp>
#include <modalities_util.h>

#ifdef _WIN32
#include <winsock2.h>
#elif __APPLE__
#include <arpa/inet.h>
#else
#include <endian.h>
#define htonll(x) htobe64(x)
#endif

namespace thalamus {
  struct StorageNode::Impl {
    struct AnalogStorage {
      std::string name;
      int num_channels;
      thalamus::vector<size_t> written;
      size_t received;
      std::optional<std::chrono::nanoseconds> first_received_time;
      std::optional<std::chrono::nanoseconds> last_received_time;
    };
    ObservableDictPtr state;
    boost::signals2::scoped_connection state_connection;
    std::atomic_bool is_running = false;
    thalamus::vector<boost::signals2::scoped_connection> source_connections;
    size_t changes_written;
    int recording_number = 0;
    NodeGraph* graph;
    std::chrono::steady_clock::time_point start_time;
    boost::signals2::scoped_connection events_connection;
    boost::signals2::scoped_connection log_connection;
    boost::signals2::scoped_connection change_connection;
    std::ofstream output_stream;
    thalamus::vector<std::pair<double, bool>> metrics;
    thalamus::vector<std::string> names;
    std::chrono::nanoseconds metrics_time;
    std::chrono::steady_clock::time_point last_publish;
    StorageNode* outer;
    std::map<std::pair<int, int>, size_t> offsets;
    boost::asio::steady_timer stats_timer;

    Impl(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph* graph, StorageNode* outer)
      : state(state)
      , graph(graph)
      , outer(outer)
      , stats_timer(io_context) {
      using namespace std::placeholders;
      state_connection = state->changed.connect(std::bind(&Impl::on_change, this, _1, _2, _3));
      this->state->recap(std::bind(&Impl::on_change, this, _1, _2, _3));
      events_connection = graph->get_service().events_signal.connect(std::bind(&Impl::on_event, this, _1));
      log_connection = graph->get_service().log_signal.connect(std::bind(&Impl::on_log, this, _1));
    }

    ~Impl() {
      (*state)["Running"].assign(false, [] {});
      stop_thread();
    }

    void on_event(const thalamus_grpc::Event& e) {
      TRACE_EVENT0("thalamus", "StorageNode::on_event");
      if (!is_running) {
        return;
      }

      update_metrics(1, 0, 1, [&] { return "Events"; });

      thalamus_grpc::StorageRecord record;
      auto body = record.mutable_event();
      *body = e;
      record.set_time(e.time());

      queue_record(std::move(record));
    }

    void on_log(const thalamus_grpc::Text& e) {
      TRACE_EVENT0("thalamus", "StorageNode::on_log");
      if (!is_running) {
        return;
      }

      update_metrics(1, 0, 1, [&] { return "Log"; });

      thalamus_grpc::StorageRecord record;
      auto body = record.mutable_text();
      *body = e;
      record.set_time(e.time());

      queue_record(std::move(record));
    }

    void on_data(Node* node, const std::string& name, AnalogNode* locked_analog, int metrics_index) {
      if (!is_running || !locked_analog->has_analog_data()) {
        return;
      }

      TRACE_EVENT0("thalamus", "StorageNode::on_data");

      thalamus_grpc::StorageRecord record;
      auto body = record.mutable_analog();
      for (auto i = 0; i < locked_analog->num_channels(); ++i) {
        auto data = locked_analog->data(i);
        auto channel_name_view = locked_analog->name(i);
        std::string channel_name(channel_name_view.begin(), channel_name_view.end());

        update_metrics(metrics_index, i, data.size(), [&] { return name + "(" + channel_name + ")"; });
        auto span = body->add_spans();

        span->set_begin(body->mutable_data()->size());
        body->mutable_data()->Add(data.begin(), data.end());
        span->set_end(body->mutable_data()->size());
        span->set_name(channel_name);

        body->add_sample_intervals(locked_analog->sample_interval(i).count());
      }

      record.set_time(locked_analog->time().count());
      record.set_node(name);

      queue_record(std::move(record));
    }

    template <typename T>
    void update_metrics(int metrics_index, int sub_index, size_t count, T name, bool is_rate = true) {
      auto key = std::make_pair(metrics_index, sub_index);
      auto offset = offsets.find(key);
      if(offset == offsets.end()) {
        offset = offsets.insert(decltype(offsets)::value_type(key, metrics.size())).first;
        metrics.emplace_back(0.0, is_rate);
        names.push_back(name());
        outer->channels_changed(outer);
      }

      metrics.at(offset->second).first += count;
    }

    void on_image_data(Node* node, const std::string& name, ImageNode* locked_analog, size_t metrics_index) {
      if (!is_running || !locked_analog->has_image_data()) {
        return;
      }

      TRACE_EVENT0("thalamus", "StorageNode::on_data");
      update_metrics(metrics_index, 0, 1, [&] { return name; });

      thalamus_grpc::StorageRecord record;
      auto body = record.mutable_image();
      body->set_width(locked_analog->width());
      body->set_height(locked_analog->height());
      switch(locked_analog->format()) {
      case ImageNode::Format::Gray:
        body->set_format(thalamus_grpc::Image::Format::Image_Format_Gray);
        break;
      case ImageNode::Format::RGB:
        body->set_format(thalamus_grpc::Image::Format::Image_Format_RGB);
        break;
      case ImageNode::Format::YUYV422:
        body->set_format(thalamus_grpc::Image::Format::Image_Format_YUYV422);
        break;
      }

      for (auto i = 0; i < locked_analog->num_planes(); ++i) {
        auto data = locked_analog->plane(i);
        body->add_data(data.data(), data.size());
      }

      record.set_time(locked_analog->time().count());
      record.set_node(name);

      queue_record(std::move(record));
    }

    void on_text_data(Node* node, const std::string& name, TextNode* locked_text, size_t metrics_index) {
      if (!is_running || !locked_text->has_text_data()) {
        return;
      }

      update_metrics(metrics_index, 0, 1, [&] { return name; });
      thalamus_grpc::StorageRecord record;
      auto body = record.mutable_text();
      auto text = locked_text->text();

      body->set_text(text.data(), text.size());

      record.set_time(locked_text->time().count());
      record.set_node(name);

      queue_record(std::move(record));
    }

    void on_xsens_data(Node* node, const std::string& name, MotionCaptureNode* locked_xsens, size_t metrics_index) {
      if (!is_running || !locked_xsens->has_motion_data()) {
        return;
      }

      TRACE_EVENT0("thalamus", "StorageNode::on_xsens_data");
      update_metrics(metrics_index, 0, 1, [&] { return name; });

      thalamus_grpc::StorageRecord record;
      auto body = record.mutable_xsens();
      body->set_pose_name(locked_xsens->pose_name());
      auto segments = locked_xsens->segments();
      for (auto& segment : segments) {
        auto protobuf_segment = body->add_segments();
        protobuf_segment->set_id(segment.segment_id);
        protobuf_segment->set_frame(segment.frame);
        protobuf_segment->set_time(segment.time);
        protobuf_segment->set_actor(segment.actor);
        protobuf_segment->set_x(boost::qvm::X(segment.position));
        protobuf_segment->set_y(boost::qvm::Y(segment.position));
        protobuf_segment->set_z(boost::qvm::Z(segment.position));
        protobuf_segment->set_q0(boost::qvm::S(segment.rotation));
        protobuf_segment->set_q1(boost::qvm::X(segment.rotation));
        protobuf_segment->set_q2(boost::qvm::Y(segment.rotation));
        protobuf_segment->set_q3(boost::qvm::Z(segment.rotation));
      }
      record.set_time(locked_xsens->time().count());
      record.set_node(name);

      queue_record(std::move(record));
    }

    void prepare_storage(const std::string& filename) {
      output_stream = std::ofstream(get_next_file(filename, graph->get_system_clock_at_start()), std::ios::trunc | std::ios::binary);
    }

    void close_file() {
      output_stream.close();
    }

    std::vector<thalamus_grpc::StorageRecord> records;
    std::condition_variable records_condition;
    std::mutex records_mutex;
    std::thread _thread;
    std::atomic_uint queued_records = 0;
    std::atomic_ullong queued_bytes = 0;

    void thread_target(std::string output_file) {
      prepare_storage(output_file);
      while(is_running) {
        std::vector<thalamus_grpc::StorageRecord> local_records;
        {
          std::unique_lock<std::mutex> lock(records_mutex);
          records_condition.wait_for(lock, 1s, [&] { return !records.empty() || !is_running; });
          local_records.swap(records);
        }
        for(auto& record : local_records) {
          auto serialized = record.SerializePartialAsString();
          auto size = serialized.size();
          auto bigendian_size = htonll(size);
          auto size_bytes = reinterpret_cast<char*>(&bigendian_size);
          output_stream.write(size_bytes, sizeof(bigendian_size));
          output_stream.write(serialized.data(), size);
          --queued_records;
          queued_bytes -= record.ByteSizeLong();
        }
      }
      close_file();
    }

    void queue_record(thalamus_grpc::StorageRecord&& record) {
      ++queued_records;
      queued_bytes += record.ByteSizeLong();
      std::lock_guard<std::mutex> lock(records_mutex);
      records.push_back(std::move(record));
      records_condition.notify_one();
    }

    void on_stats_timer(const boost::system::error_code& error) {
      if (error.value() == boost::asio::error::operation_aborted) {
        return;
      }
      BOOST_ASSERT(!error);
      if (!is_running) {
        return;
      }

      update_metrics(0, 0, queued_records, [&] { return "Output Queue Count"; });
      update_metrics(0, 1, queued_bytes, [&] { return "Output Queue Bytes"; });

      auto now = std::chrono::steady_clock::now();
      auto elapsed = now - last_publish;
      for(auto i = metrics.begin();i < metrics.end();++i) {
        if(i->second) {
          i->first /= double(elapsed.count())/decltype(elapsed)::period::den;
        }
      }
      metrics_time = now.time_since_epoch();
      outer->ready(outer);
      last_publish = now;
      for(auto i = metrics.begin();i < metrics.end();++i) {
        i->first = 0;
      }

      stats_timer.expires_after(1s);
      stats_timer.async_wait(std::bind(&Impl::on_stats_timer, this, _1));
    }

    void start_thread(std::string output_file) {
      is_running = true;
      records.clear();
      queued_bytes = 0;
      queued_records = 0;
      _thread = std::thread([&, output_file] { thread_target(output_file); });
      stats_timer.expires_after(1s);
      stats_timer.async_wait(std::bind(&Impl::on_stats_timer, this, _1));
    }

    void stop_thread() {
      is_running = false;
      if(_thread.joinable()) {
        _thread.join();
      }
    }

    void on_change(ObservableCollection::Action, const ObservableCollection::Key&, const ObservableCollection::Value&) {
      if (!state->contains("Running")) {
        return;
      }
      is_running = static_cast<bool>(state->at("Running"));
      if (!is_running) {
        stop_thread();
        return;
      }

      last_publish = std::chrono::steady_clock::now();
      if (!state->contains("Output File")) {
        stop_thread();
        return;
      }
      std::string output_file = state->at("Output File");

      if (is_running) {
        start_thread(output_file);
      } else {
        stop_thread();
      }
      start_time = std::chrono::steady_clock::now();
      source_connections.clear();

      if (state->contains("Sources")) {
        metrics.clear();
        offsets.clear();
        names.clear();
        std::string source_str = state->at("Sources");
        auto tokens = absl::StrSplit(source_str, ',');
        auto i = 2;
        for (auto& raw_token : tokens) {
          auto token = std::string(absl::StripAsciiWhitespace(raw_token));

          graph->get_node(token, [this,token,i](auto source) {
            auto locked_source = source.lock();
            if (!locked_source) {
              return;
            }
            
            if (node_cast<MotionCaptureNode*>(locked_source.get()) != nullptr) {
              auto xsens_source = node_cast<MotionCaptureNode*>(locked_source.get());
              auto xsens_source_connection = locked_source->ready.connect(std::bind(&Impl::on_xsens_data, this, _1, token, xsens_source, i));
              source_connections.push_back(std::move(xsens_source_connection));
            } 
            if (node_cast<AnalogNode*>(locked_source.get()) != nullptr) {
              auto analog_source = node_cast<AnalogNode*>(locked_source.get());
              auto analog_source_connection = locked_source->ready.connect(std::bind(&Impl::on_data, this, _1, token, analog_source, i));
              source_connections.push_back(std::move(analog_source_connection));
            } 
            if (node_cast<ImageNode*>(locked_source.get()) != nullptr) {
              auto image_source = node_cast<ImageNode*>(locked_source.get());
              auto image_source_connection = locked_source->ready.connect(std::bind(&Impl::on_image_data, this, _1, token, image_source, i));
              source_connections.push_back(std::move(image_source_connection));
            }
            if (node_cast<TextNode*>(locked_source.get()) != nullptr) {
              auto text_source = node_cast<TextNode*>(locked_source.get());
              auto text_source_connection = locked_source->ready.connect(std::bind(&Impl::on_text_data, this, _1, token, text_source, i));
              source_connections.push_back(std::move(text_source_connection));
            }
          });
          ++i;
        }
      }
    }
  };

  StorageNode::StorageNode(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph* graph) : impl(new Impl(state, io_context, graph, this)) {}

  StorageNode::~StorageNode() {}

  std::string StorageNode::type_name() {
    return "STORAGE";
  }

  std::filesystem::path StorageNode::get_next_file(const std::filesystem::path& name, std::chrono::system_clock::time_point time) {
    const auto start_time = absl::FromChrono(time);
    auto start_time_str = absl::FormatTime("%Y%m%d%H%M%S", start_time, absl::LocalTimeZone());
    auto i = 0;
    std::filesystem::path filename;
    do {
      filename = absl::StrFormat("%s.%s.%d", name.string(), start_time_str, ++i);
    } while (std::filesystem::exists(filename));
    return filename;
  }

  void StorageNode::record(std::ofstream& output, const thalamus_grpc::StorageRecord& record) {
    auto serialized = record.SerializePartialAsString();
    auto size = serialized.size();
    size = htonll(size);
    auto size_bytes = reinterpret_cast<char*>(&size);
    output.write(size_bytes, sizeof(size));
    output.write(serialized.data(), serialized.size());
  }

  void StorageNode::record(std::ofstream& output, const std::string& serialized) {
    auto size = serialized.size();
    size = htonll(size);
    auto size_bytes = reinterpret_cast<char*>(&size);
    output.write(size_bytes, sizeof(size));
    output.write(serialized.data(), serialized.size());
  }

  std::span<const double> StorageNode::data(int channel) const {
    return std::span<const double>(&(impl->metrics.begin() + channel)->first, &(impl->metrics.begin() + channel)->first + 1);
  }

  int StorageNode::num_channels() const {
    return impl->metrics.size();
  }

  std::chrono::nanoseconds StorageNode::sample_interval(int channel) const {
    return 1s;
  }

  std::chrono::nanoseconds StorageNode::time() const {
    return impl->metrics_time;
  }

  std::string_view StorageNode::name(int channel) const {
    return impl->names.at(channel);
  }
  std::span<const std::string> StorageNode::get_recommended_channels() const {
    return std::span<const std::string>(impl->names.begin(), impl->names.end());
  }

  void StorageNode::inject(const thalamus::vector<std::span<double const>>&, const thalamus::vector<std::chrono::nanoseconds>&, const thalamus::vector<std::string_view>&) {
  }
}
