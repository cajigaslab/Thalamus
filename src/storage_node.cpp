#include <thalamus/tracing.hpp>
#include <storage_node.hpp>
#include <image_node.hpp>
#include <text_node.hpp>
#include <fstream>
#include <util.hpp>
#include <absl/strings/str_format.h>
#include <absl/time/time.h>
#include <boost/qvm/vec_access.hpp>
#include <boost/qvm/quat_access.hpp>
#include <boost/pool/object_pool.hpp>
#include <modalities_util.hpp>
#include <thalamus/thread.hpp>
#include <zlib.h>
#include <thalamus/async.hpp>
#include <thread_pool.hpp>

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
    boost::asio::io_context& io_context;
    ThreadPool& pool;

    Impl(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph* graph, StorageNode* outer)
      : state(state)
      , graph(graph)
      , outer(outer)
      , stats_timer(io_context)
      , io_context(io_context)
      , pool(graph->get_thread_pool()) {
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
      TRACE_EVENT("thalamus", "StorageNode::on_event");
      if (!is_running) {
        return;
      }

      update_metrics(1, 0, 1, [&] { return "Events"; });

      thalamus_grpc::StorageRecord record;
      {
        TRACE_EVENT("thalamus", "StorageNode::on_event(build record)");
        auto body = record.mutable_event();
        *body = e;
        record.set_time(e.time());
      }

      queue_record(std::move(record));
    }

    void on_log(const thalamus_grpc::Text& e) {
      TRACE_EVENT("thalamus", "StorageNode::on_log");
      if (!is_running) {
        return;
      }

      update_metrics(1, 0, 1, [&] { return "Log"; });

      thalamus_grpc::StorageRecord record;
      {
        TRACE_EVENT("thalamus", "StorageNode::on_event(build record)");
        auto body = record.mutable_text();
        *body = e;
        record.set_time(e.time());
      }

      queue_record(std::move(record));
    }

    std::map<std::pair<Node*, int>, int> stream_mappings;

    void on_data(Node* node, const std::string& name, AnalogNode* locked_analog, int metrics_index) {
      if (!is_running || !locked_analog->has_analog_data()) {
        return;
      }

      TRACE_EVENT("thalamus", "StorageNode::on_analog_data");

      thalamus_grpc::StorageRecord record;
      if(compress_analog) {
        records_mutex.lock();
      }

      {
        TRACE_EVENT("thalamus", "StorageNode::on_analog_data(build record)");
        visit_node(locked_analog, [&]<typename T>(T* locked_analog) {
          for (auto i = 0; i < locked_analog->num_channels(); ++i) {
            auto data = locked_analog->data(i);
            if(compress_analog) {
              if(data.empty()) {
                continue;
              }
              record = thalamus_grpc::StorageRecord();
            }
            auto body = record.mutable_analog();
            auto channel_name_view = locked_analog->name(i);
            std::string channel_name(channel_name_view.begin(), channel_name_view.end());

            update_metrics(metrics_index, i, data.size(), [&] { return name + "(" + channel_name + ")"; });
            auto span = body->add_spans();

            if constexpr (std::is_same<typename decltype(data)::value_type, short>::value) {
              span->set_begin(body->mutable_int_data()->size());
              body->mutable_int_data()->Add(data.begin(), data.end());
              span->set_end(body->mutable_int_data()->size());
              body->set_is_int_data(true);
            } else {
              span->set_begin(body->mutable_data()->size());
              body->mutable_data()->Add(data.begin(), data.end());
              span->set_end(body->mutable_data()->size());
            }
            span->set_name(channel_name);

            body->add_sample_intervals(locked_analog->sample_interval(i).count());

            record.set_time(locked_analog->time().count());
            record.set_node(name);
            if(compress_analog) {
              auto j = stream_mappings.find(std::make_pair(node, i));
              if(j == stream_mappings.end()) {
                stream_mappings[std::make_pair(node, i)] = get_unique_id();
                j = stream_mappings.find(std::make_pair(node, i));
              }
              ++queued_records;
              queued_bytes += record.ByteSizeLong();
              records.emplace_back(std::move(record), j->second);
            }
          }
        });
      }
      if(!compress_analog) {
        queue_record(std::move(record));
      } else {
        records_condition.notify_one();
        records_mutex.unlock();
      }

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

      TRACE_EVENT("thalamus", "StorageNode::on_image_data");
      update_metrics(metrics_index, 0, 1, [&] { return name; });

      thalamus_grpc::StorageRecord record;
      {
        TRACE_EVENT("thalamus", "StorageNode::on_image_data(build record)");
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
        case ImageNode::Format::YUV420P:
          body->set_format(thalamus_grpc::Image::Format::Image_Format_YUV420P);
          break;
        case ImageNode::Format::YUVJ420P:
          body->set_format(thalamus_grpc::Image::Format::Image_Format_YUVJ420P);
          break;
        }

        for (auto i = 0; i < locked_analog->num_planes(); ++i) {
          auto data = locked_analog->plane(i);
          body->add_data(data.data(), data.size());
        }

        record.set_time(locked_analog->time().count());
        record.set_node(name);
      }

      queue_record(std::move(record));
    }

    void on_text_data(Node* node, const std::string& name, TextNode* locked_text, size_t metrics_index) {
      if (!is_running || !locked_text->has_text_data()) {
        return;
      }

      TRACE_EVENT("thalamus", "StorageNode::on_text_data");

      update_metrics(metrics_index, 0, 1, [&] { return name; });
      thalamus_grpc::StorageRecord record;
      {
        TRACE_EVENT("thalamus", "StorageNode::on_text_data(build record)");
        auto body = record.mutable_text();
        auto text = locked_text->text();

        body->set_text(text.data(), text.size());

        record.set_time(locked_text->time().count());
        record.set_node(name);
      }

      queue_record(std::move(record));
    }

    void on_xsens_data(Node* node, const std::string& name, MotionCaptureNode* locked_xsens, size_t metrics_index) {
      if (!is_running || !locked_xsens->has_motion_data()) {
        return;
      }

      TRACE_EVENT("thalamus", "StorageNode::on_motion_data");
      update_metrics(metrics_index, 0, 1, [&] { return name; });

      thalamus_grpc::StorageRecord record;
      {
        TRACE_EVENT("thalamus", "StorageNode::on_motion_data(build record)");
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
      }

      queue_record(std::move(record));
    }

    void prepare_storage(const std::string& filename) {
      output_stream = std::ofstream(get_next_file(filename, graph->get_system_clock_at_start()), std::ios::trunc | std::ios::binary);
    }

    void close_file() {
      output_stream.close();
    }

    std::vector<std::pair<thalamus_grpc::StorageRecord, int>> records;
    std::condition_variable records_condition;
    std::mutex records_mutex;
    std::thread _thread;
    std::atomic_uint queued_records = 0;
    std::atomic_ullong queued_bytes = 0;

    const size_t zbuffer_size = 1024;

    template <typename T>
    struct SimplePool {
      std::mutex mutex;
      std::list<T*> pool;
      ~SimplePool() {
        for(auto t : pool) {
          delete t;
        }
      }
      std::shared_ptr<T> get() {
        std::lock_guard<std::mutex> lock(mutex);
        if(pool.empty()) {
          pool.push_back(new T());
        }
        auto result = pool.front();
        pool.pop_front();
        return std::shared_ptr<T>(result, [&] (T* t) {
          std::lock_guard<std::mutex> lock(mutex);
          pool.push_back(t);
        });
      }
    };

    struct StreamState {
      z_stream zstream;
      int index;
    };

    void thread_target(std::string output_file, bool compress_analog) {
      set_current_thread_name("STORAGE");
      prepare_storage(output_file);
      Finally f([&] {
        close_file();
      });

      std::map<int, std::shared_ptr<StreamState>> streams;
      SimplePool<thalamus_grpc::StorageRecord> record_pool;
      std::map<int, std::vector<size_t>> compressed_map;
      std::vector<std::pair<int, std::vector<size_t>>> compressed;
      std::vector<std::string> all_serialized;

      while(is_running) {
        std::vector<std::pair<thalamus_grpc::StorageRecord, int>> local_records;
        {
          std::unique_lock<std::mutex> lock(records_mutex);
          records_condition.wait_for(lock, 1s, [&] { return !records.empty() || !is_running; });
          local_records.swap(records);
        }

        all_serialized.resize(local_records.size());
        compressed_map.clear();
        {
          TRACE_EVENT("thalamus", "serialize");
          if(compress_analog) {
            size_t i = 0;
            for(auto& record_pair : local_records) {
              auto& [record, stream] = record_pair;
              all_serialized[i] = record.SerializePartialAsString();
              auto body_type = record.body_case();
              if(body_type == thalamus_grpc::StorageRecord::kAnalog) {
                compressed_map[stream].push_back(i);
                auto stream_i = streams.find(stream);
                if(stream_i == streams.end()) {
                  streams[stream] = std::make_shared<StreamState>();
                  stream_i = streams.find(stream);
                  stream_i->second->index = stream;
                  auto& zstream = stream_i->second->zstream;
                  zstream.zalloc = Z_NULL;
                  zstream.zfree = Z_NULL;
                  zstream.opaque = Z_NULL;
                  auto error = deflateInit(&zstream, 1);
                  THALAMUS_ASSERT(error == Z_OK, "ZLIB Error: %d", error);
                }
              }
              ++i;
            }
          } else {
            size_t i = 0;
            for(auto& record_pair : local_records) {
              auto& [record, stream] = record_pair;
              all_serialized[i] = record.SerializePartialAsString();
              ++i;
            }
          }
        }

        compressed.assign(compressed_map.begin(), compressed_map.end());

        auto band_size = std::max(1ull, compressed.size()/pool.num_threads);
        band_size += (band_size*pool.num_threads < compressed.size()) ? 1 : 0;
        size_t pending_bands = compressed.size()/band_size;
        pending_bands += (pending_bands * band_size < compressed.size()) ? 1 : 0;
        std::mutex mutex;
        std::condition_variable condition;
        {
          TRACE_EVENT("thalamus", "deflate_all");
          for(auto i = 0;i < compressed.size();i+=band_size) {
            auto upper = std::min(i+band_size, compressed.size());
            pool.push([&,i,upper] {
              TRACE_EVENT("thalamus", "deflate");
              for(auto j = i;j < upper;++j) {
                auto& [stream, indexes] = compressed[j];
                auto stream_state = streams[stream];
                auto& zstream = stream_state->zstream;
                for(auto index : indexes) {
                  auto& serialized = all_serialized[index];
                  auto& record = local_records[index].first;

                  auto compressed_record = std::make_shared<thalamus_grpc::StorageRecord>();
                  auto compressed = compressed_record->mutable_compressed();
                  auto compressed_data = compressed->mutable_data();
                  if(compressed_data->empty()) {
                    compressed_data->resize(1024);
                  }
                  zstream.avail_in = serialized.size();
                  zstream.next_in = reinterpret_cast<unsigned char*>(serialized.data());
                  auto compressing = true;
                  size_t offset = 0;
                  compressed->set_type(thalamus_grpc::Compressed::Type::Compressed_Type_ANALOG);
                  compressed->set_stream(stream);
                  compressed->set_size(serialized.size());
                  while(compressing) {
                    zstream.avail_out = compressed_data->size() - offset;
                    zstream.next_out = reinterpret_cast<unsigned char*>(compressed_data->data()) + offset;
                    auto error = deflate(&zstream, Z_NO_FLUSH);
                    THALAMUS_ASSERT(error == Z_OK, "ZLIB Error: %d", error);
                    compressing = zstream.avail_out == 0;
                    if(compressing) {
                      offset = compressed_data->size();
                      compressed_data->resize(2*compressed_data->size());
                    }
                  }
                  compressed_data->resize(compressed_data->size() - zstream.avail_out);
                  all_serialized[index] = compressed_record->SerializePartialAsString();
                }
              }
              {
                std::lock_guard<std::mutex> lock(mutex);
                --pending_bands;
                condition.notify_all();
              }
            });
          }
          std::unique_lock<std::mutex> lock(mutex);
          condition.wait(lock, [&] { return pending_bands == 0; });
        }

        {
          TRACE_EVENT("thalamus", "write");
          for (auto& serialized : all_serialized) {
            auto size = serialized.size();
            auto bigendian_size = htonll(size);
            auto size_bytes = reinterpret_cast<char*>(&bigendian_size);
            output_stream.write(size_bytes, sizeof(bigendian_size));
            output_stream.write(serialized.data(), size);
          }
        }
      }

      std::vector<std::shared_ptr<StreamState>> stream_vector;
      for(auto& stream : streams) {
        stream_vector.push_back(stream.second);
      }
      auto band_size = std::max(1ull, stream_vector.size()/pool.num_threads);
      band_size += (band_size*pool.num_threads < stream_vector.size()) ? 1 : 0;
      size_t pending_bands = stream_vector.size() / band_size;
      pending_bands += (pending_bands * band_size < stream_vector.size()) ? 1 : 0;
      std::mutex mutex;
      std::condition_variable condition;
      std::vector<std::string> flushes(streams.size());
      {
        TRACE_EVENT("thalamus", "deflate_flush_all");
        for(auto i = 0;i < stream_vector.size();i+=band_size) {
          auto upper = std::min(i+band_size, stream_vector.size());
          pool.push([&,i,upper] {
            TRACE_EVENT("thalamus", "deflate_flush");
            for(auto j = i;j < upper;++j) {
              auto stream_state = stream_vector[j];

              auto& zstream = stream_state->zstream;
              auto compressed_record = std::make_shared<thalamus_grpc::StorageRecord>();
              auto compressed = compressed_record->mutable_compressed();
              auto compressed_data = compressed->mutable_data();
              if(compressed_data->empty()) {
                compressed_data->resize(1024);
              }
              auto compressing = true;
              size_t offset = 0;
              compressed->set_type(thalamus_grpc::Compressed::Type::Compressed_Type_NONE);
              compressed->set_stream(stream_state->index);
              while(compressing) {
                zstream.avail_out = compressed_data->size() - offset;
                zstream.next_out = reinterpret_cast<unsigned char*>(compressed_data->data()) + offset;
                auto error = deflate(&zstream, Z_FINISH);
                THALAMUS_ASSERT(error == Z_OK || error == Z_STREAM_END);
                compressing = zstream.avail_out == 0;
                if(compressing) {
                  offset = compressed_data->size();
                  compressed_data->resize(2*compressed_data->size());
                }
              }
              compressed_data->resize(compressed_data->size() - zstream.avail_out);
              flushes[j] = compressed_record->SerializePartialAsString();
            }
            {
              std::lock_guard<std::mutex> lock(mutex);
              --pending_bands;
              condition.notify_all();
            }
          });
        }
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [&] { return pending_bands == 0; });
      }

      {
        TRACE_EVENT("thalamus", "write_flush");
        for (auto& serialized : flushes) {
          auto size = serialized.size();
          auto bigendian_size = htonll(size);
          auto size_bytes = reinterpret_cast<char*>(&bigendian_size);
          output_stream.write(size_bytes, sizeof(bigendian_size));
          output_stream.write(serialized.data(), size);
        }
      }
    }

    void queue_record(thalamus_grpc::StorageRecord&& record, int stream = 0) {
      //TRACE_EVENT("thalamus", "StorageNode::queue_record");
      ++queued_records;
      queued_bytes += record.ByteSizeLong();
      std::lock_guard<std::mutex> lock(records_mutex);
      records.emplace_back(std::move(record), stream);
      records_condition.notify_one();
    }

    void on_stats_timer(const boost::system::error_code& error) {
      TRACE_EVENT("thalamus", "StorageNode::on_stats_timer");
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

    void start_thread(std::string output_file, bool compress_analog) {
      is_running = true;
      records.clear();
      queued_bytes = 0;
      queued_records = 0;
      _thread = std::thread([&, output_file, compress_analog] { thread_target(output_file, compress_analog); });
      stats_timer.expires_after(1s);
      stats_timer.async_wait(std::bind(&Impl::on_stats_timer, this, _1));
    }

    void stop_thread() {
      is_running = false;
      if(_thread.joinable()) {
        _thread.join();
      }
    }

    bool compress_analog = false;

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
      compress_analog = state->contains("Compress Analog") ? state->at("Compress Analog") : false;

      if (is_running) {
        start_thread(output_file, compress_analog);
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
