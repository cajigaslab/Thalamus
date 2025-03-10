#include <thalamus/tracing.hpp>
#include <fstream>
#include <image_node.hpp>
#include <modalities_util.hpp>
#include <storage_node.hpp>
#include <text_node.hpp>
#include <thalamus/async.hpp>
#include <thalamus/thread.hpp>
#include <thread_pool.hpp>
#include <util.hpp>

#ifdef _WIN32
#include <WinSock2.h>
#elif defined(__APPLE__)
#include <arpa/inet.h>
#else
#include <endian.h>
#define htonll(x) htobe64(x)
#endif

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include <absl/strings/str_format.h>
#include <absl/time/time.h>
#include <boost/pool/object_pool.hpp>
#include <boost/qvm/quat_access.hpp>
#include <boost/qvm/vec_access.hpp>
#include <inja/inja.hpp>
#include <zlib.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

#ifdef __clang__
#pragma clang diagnostic pop
#endif

namespace thalamus {
static std::string render_filename(const std::string &filename,
                                   const inja::json &tdata,
                                   std::chrono::system_clock::time_point time) {
  auto rendered = inja::render(filename, tdata);
  const auto start_time = absl::FromChrono(time);
  rendered = absl::FormatTime(rendered, start_time, absl::LocalTimeZone());
  return rendered;
}

static int get_rec_number(const std::filesystem::path &name,
                          const inja::json &_tdata,
                          std::chrono::system_clock::time_point time) {
  inja::json tdata = _tdata;
  const auto start_time = absl::FromChrono(time);
  auto start_time_str =
      absl::FormatTime("%Y%m%d", start_time, absl::LocalTimeZone());
  auto i = 0;
  std::filesystem::path filename;
  do {
    ++i;
    tdata["rec"] = absl::StrFormat("%03d", i);
    filename = render_filename(name.string(), tdata, time);
    filename =
        absl::StrFormat("%s.%s.%d", filename.string(), start_time_str, i);
  } while (std::filesystem::exists(filename));
  return i;
}

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
  NodeGraph *graph;
  std::chrono::steady_clock::time_point start_time;
  boost::signals2::scoped_connection events_connection;
  boost::signals2::scoped_connection log_connection;
  boost::signals2::scoped_connection change_connection;
  std::ofstream output_stream;
  thalamus::vector<std::pair<double, bool>> metrics;
  thalamus::vector<std::string> names;
  std::chrono::nanoseconds metrics_time;
  std::chrono::steady_clock::time_point last_publish;
  StorageNode *outer;
  std::map<std::pair<int, int>, size_t> offsets;
  boost::asio::steady_timer stats_timer;
  boost::asio::io_context &io_context;
  ThreadPool &pool;

  Impl(ObservableDictPtr _state, boost::asio::io_context &_io_context,
       NodeGraph *_graph, StorageNode *_outer)
      : state(_state), graph(_graph), outer(_outer), stats_timer(_io_context),
        io_context(_io_context), pool(graph->get_thread_pool()) {
    using namespace std::placeholders;
    state_connection =
        state->changed.connect(std::bind(&Impl::on_change, this, _1, _2, _3));
    this->state->recap(std::bind(&Impl::on_change, this, _1, _2, _3));
    events_connection = graph->get_service().events_signal.connect(
        std::bind(&Impl::on_event, this, _1));
    log_connection = graph->get_service().log_signal.connect(
        std::bind(&Impl::on_log, this, _1));
  }

  ~Impl() {
    (*state)["Running"].assign(false, [] {});
    stop_thread();
  }

  void on_event(const thalamus_grpc::Event &e) {
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

  void on_log(const thalamus_grpc::Text &e) {
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

  std::map<std::pair<Node *, int>, int> stream_mappings;

  void on_data(Node *node, const std::string &name, AnalogNode *locked_analog,
               int metrics_index) {
    if (!is_running || !locked_analog->has_analog_data()) {
      return;
    }

    TRACE_EVENT("thalamus", "StorageNode::on_analog_data");

    thalamus_grpc::StorageRecord record;
    auto body = record.mutable_analog();
    if (compress_analog) {
      records_mutex.lock();
    }

    {
      TRACE_EVENT("thalamus", "StorageNode::on_analog_data(build record)");
      record.set_time(uint64_t(locked_analog->time().count()));
      record.set_node(name);
      body->set_time(uint64_t(locked_analog->time().count()));
      body->set_remote_time(uint64_t(locked_analog->remote_time().count()));
      visit_node(locked_analog, [&]<typename T>(T *wrapper) {
        for (auto i = 0; i < wrapper->num_channels(); ++i) {
          auto data = wrapper->data(i);
          if (compress_analog) {
            if (data.empty()) {
              continue;
            }
            record = thalamus_grpc::StorageRecord();
            body = record.mutable_analog();
            record.set_time(uint64_t(locked_analog->time().count()));
            record.set_node(name);
            body->set_time(uint64_t(locked_analog->time().count()));
            body->set_remote_time(
                uint64_t(locked_analog->remote_time().count()));
          }
          auto channel_name_view = wrapper->name(i);
          std::string channel_name(channel_name_view.begin(),
                                   channel_name_view.end());

          update_metrics(metrics_index, i, data.size(),
                         [&] { return name + "(" + channel_name + ")"; });
          auto span = body->add_spans();

          if constexpr (std::is_same<typename decltype(data)::value_type,
                                     short>::value) {
            span->set_begin(uint32_t(body->mutable_int_data()->size()));
            body->mutable_int_data()->Add(data.begin(), data.end());
            span->set_end(uint32_t(body->mutable_int_data()->size()));
            body->set_is_int_data(true);
          } else {
            span->set_begin(uint32_t(body->mutable_data()->size()));
            body->mutable_data()->Add(data.begin(), data.end());
            span->set_end(uint32_t(body->mutable_data()->size()));
          }
          span->set_name(channel_name);

          body->add_sample_intervals(
              uint64_t(wrapper->sample_interval(i).count()));

          if (compress_analog) {
            auto j = stream_mappings.find(std::make_pair(node, i));
            if (j == stream_mappings.end()) {
              stream_mappings[std::make_pair(node, i)] = int(get_unique_id());
              j = stream_mappings.find(std::make_pair(node, i));
            }
            ++queued_records;
            queued_bytes += record.ByteSizeLong();
            records.emplace_back(std::move(record), j->second);
          }
        }
      });
    }
    if (!compress_analog) {
      queue_record(std::move(record));
    } else {
      records_condition.notify_one();
      records_mutex.unlock();
    }
  }

  template <typename T>
  void update_metrics(int metrics_index, int sub_index, size_t count, T name,
                      bool is_rate = true) {
    auto key = std::make_pair(metrics_index, sub_index);
    auto offset = offsets.find(key);
    if (offset == offsets.end()) {
      offset =
          offsets.insert(decltype(offsets)::value_type(key, metrics.size()))
              .first;
      metrics.emplace_back(0.0, is_rate);
      names.push_back(name());
      outer->channels_changed(outer);
    }

    metrics.at(offset->second).first += double(count);
  }

  void on_image_data(Node *, const std::string &name, ImageNode *locked_analog,
                     size_t metrics_index) {
    if (!is_running || !locked_analog->has_image_data()) {
      return;
    }

    TRACE_EVENT("thalamus", "StorageNode::on_image_data");
    update_metrics(int(metrics_index), 0, 1, [&] { return name; });

    thalamus_grpc::StorageRecord record;
    {
      TRACE_EVENT("thalamus", "StorageNode::on_image_data(build record)");
      auto body = record.mutable_image();
      body->set_width(uint32_t(locked_analog->width()));
      body->set_height(uint32_t(locked_analog->height()));
      body->set_frame_interval(
          uint32_t(locked_analog->frame_interval().count()));
      switch (locked_analog->format()) {
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

      for (auto i = 0; i < int(locked_analog->num_planes()); ++i) {
        auto data = locked_analog->plane(i);
        body->add_data(data.data(), data.size());
      }

      record.set_time(size_t(locked_analog->time().count()));
      record.set_node(name);
    }

    queue_record(std::move(record));
  }

  void on_text_data(Node *, const std::string &name, TextNode *locked_text,
                    size_t metrics_index) {
    if (!is_running || !locked_text->has_text_data()) {
      return;
    }

    TRACE_EVENT("thalamus", "StorageNode::on_text_data");

    update_metrics(int(metrics_index), 0, 1, [&] { return name; });
    thalamus_grpc::StorageRecord record;
    {
      TRACE_EVENT("thalamus", "StorageNode::on_text_data(build record)");
      auto body = record.mutable_text();
      auto text = locked_text->text();

      body->set_text(text.data(), text.size());

      record.set_time(size_t(locked_text->time().count()));
      record.set_node(name);
    }

    queue_record(std::move(record));
  }

  void on_xsens_data(Node *, const std::string &name,
                     MotionCaptureNode *locked_xsens, size_t metrics_index) {
    if (!is_running || !locked_xsens->has_motion_data()) {
      return;
    }

    TRACE_EVENT("thalamus", "StorageNode::on_motion_data");
    update_metrics(int(metrics_index), 0, 1, [&] { return name; });

    thalamus_grpc::StorageRecord record;
    {
      TRACE_EVENT("thalamus", "StorageNode::on_motion_data(build record)");
      auto body = record.mutable_xsens();
      body->set_pose_name(locked_xsens->pose_name());
      auto segments = locked_xsens->segments();
      for (auto &segment : segments) {
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
      record.set_time(uint64_t(locked_xsens->time().count()));
      record.set_node(name);
    }

    queue_record(std::move(record));
  }

  void prepare_storage(const std::string &filename) {
    inja::json tdata;
    auto time = graph->get_system_clock_at_start();
    int rec_number = get_rec_number(filename, tdata, time);

    boost::asio::post(io_context, [_state = this->state, rec_number] {
      (*_state)["rec"].assign(rec_number, [] {});
    });

    tdata["rec"] = absl::StrFormat("%03d", rec_number);
    auto rendered = render_filename(filename, tdata, time);

    const auto absl_time = absl::FromChrono(time);
    auto start_time_str =
        absl::FormatTime("%Y%m%d", absl_time, absl::LocalTimeZone());
    rendered =
        absl::StrFormat("%s.%s.%d", rendered, start_time_str, rec_number);
    std::filesystem::path rendered_path(rendered);
    auto parent_path = rendered_path.parent_path();
    if (!parent_path.empty()) {
      std::filesystem::create_directories(parent_path);
    }

    output_stream = std::ofstream(rendered, std::ios::trunc | std::ios::binary);
  }

  void close_file() { output_stream.close(); }

  std::vector<std::pair<thalamus_grpc::StorageRecord, int>> records;
  std::condition_variable records_condition;
  std::mutex records_mutex;
  std::thread _thread;
  std::atomic_uint queued_records = 0;
  std::atomic_ullong queued_bytes = 0;

  const size_t zbuffer_size = 1024;

  template <typename T> struct SimplePool {
    std::mutex mutex;
    std::list<T *> pool;
    ~SimplePool() {
      for (auto t : pool) {
        delete t;
      }
    }
    std::shared_ptr<T> get() {
      std::lock_guard<std::mutex> lock(mutex);
      if (pool.empty()) {
        pool.push_back(new T());
      }
      auto result = pool.front();
      pool.pop_front();
      return std::shared_ptr<T>(result, [&](T *t) {
        std::lock_guard<std::mutex> lock2(mutex);
        pool.push_back(t);
      });
    }
  };

  struct StreamState {
    z_stream zstream;
    int index;
  };

  struct ContextDeleter {
    void operator()(AVCodecContext *c) { avcodec_free_context(&c); }
  };

  struct PacketDeleter {
    void operator()(AVPacket *c) { av_packet_free(&c); }
  };

  struct FrameDeleter {
    void operator()(AVFrame *c) { av_frame_free(&c); }
  };

  struct VideoCodec {
    const AVCodec *codec;
    std::unique_ptr<AVCodecContext, ContextDeleter> context;
    std::unique_ptr<AVPacket, PacketDeleter> packet;
    std::unique_ptr<AVFrame, FrameDeleter> frame;
    VideoCodec(const AVCodec *_codec)
        : codec(_codec), context(avcodec_alloc_context3(codec)),
          packet(av_packet_alloc()), frame(av_frame_alloc()) {}
  };

  struct Encoder {
    virtual ~Encoder();
    virtual void work() = 0;
    virtual void finish() = 0;
    virtual void push(thalamus_grpc::StorageRecord &&record) = 0;
    virtual std::optional<thalamus_grpc::StorageRecord> pull() = 0;
  };

  struct IdentityEncoder : public Encoder {
    std::list<thalamus_grpc::StorageRecord> queue;

    void work() override;
    void finish() override {}
    void push(thalamus_grpc::StorageRecord &&record) override {
      queue.push_back(std::move(record));
    }
    std::optional<thalamus_grpc::StorageRecord> pull() override {
      if (!queue.empty()) {
        std::optional<thalamus_grpc::StorageRecord> result =
            std::move(queue.front());
        queue.pop_front();
        return result;
      }
      return std::nullopt;
    }
  };

  struct VideoEncoder : public Encoder {
    const AVCodec *codec;
    AVCodecContext *context;
    AVPacket *packet;
    AVFrame *frame;
    std::vector<thalamus_grpc::StorageRecord> in_queue;
    std::list<thalamus_grpc::StorageRecord> out_queue;
    int pts = 0;
    struct SwsContext *sws_context;
    uint8_t *src_data[4], *dst_data[4];
    int src_linesize[4], dst_linesize[4];
    uint8_t *dst_data_0;
    std::string node;
    AVPixelFormat src_format;

    VideoEncoder(int width, int height, AVPixelFormat format,
                 AVRational framerate, const std::string &_node)
        : node(_node), src_format(format) {
      codec = avcodec_find_encoder(AV_CODEC_ID_MPEG4);
      THALAMUS_ASSERT(codec, "avcodec_find_encoder failed");
      context = avcodec_alloc_context3(codec);
      THALAMUS_ASSERT(context, "avcodec_alloc_context3 failed");
      packet = av_packet_alloc();
      frame = av_frame_alloc();

      context->bit_rate = std::numeric_limits<int>::max();
      context->width = width;
      context->height = height;
      context->framerate = framerate;
      context->time_base = {framerate.den, framerate.num};

      context->pix_fmt = AV_PIX_FMT_YUV420P;

      frame->format = context->pix_fmt;
      frame->width = width;
      frame->height = height;

      auto ret = avcodec_open2(context, codec, nullptr);
      THALAMUS_ASSERT(ret >= 0, "Could not open codec: %d", ret);

      ret = av_frame_get_buffer(frame, 0);
      THALAMUS_ASSERT(ret >= 0, "Could not allocate the video frame data");

      sws_context =
          sws_getContext(width, height, format, width, height, context->pix_fmt,
                         SWS_BILINEAR, nullptr, nullptr, nullptr);
      ret = av_image_alloc(src_data, src_linesize, width, height, format, 16);
      THALAMUS_ASSERT(ret >= 0, "Could not allocate source image");
      ret = av_image_alloc(dst_data, dst_linesize, width, height,
                           context->pix_fmt, 16);
      dst_data_0 = dst_data[0];
      THALAMUS_ASSERT(ret >= 0, "Could not allocate destination image");
    }
    ~VideoEncoder() override {
      avcodec_free_context(&context);
      av_packet_free(&packet);
      av_frame_free(&frame);
      av_freep(&src_data[0]);
      dst_data[0] = dst_data_0;
      av_freep(&dst_data[0]);
    }
    void work() override {
      for (auto &record : in_queue) {
        auto image = record.image();

        thalamus_grpc::StorageRecord compressed_record;
        compressed_record.set_node(node);
        compressed_record.set_time(record.time());
        auto compressed_image = compressed_record.mutable_image();
        compressed_image->set_width(image.width());
        compressed_image->set_height(image.height());
        compressed_image->set_format(
            thalamus_grpc::Image::Format::Image_Format_MPEG4);
        compressed_image->set_frame_interval(image.frame_interval());
        compressed_image->set_last(image.last());
        compressed_image->set_bigendian(image.bigendian());

        auto ret = av_frame_make_writable(frame);
        THALAMUS_ASSERT(ret >= 0, "av_frame_make_writable failed");

        std::array<unsigned int, 3> bps;
        switch (image.format()) {
        case thalamus_grpc::Image::Format::Image_Format_Gray:
          bps = {1, 1, 1};
          break;
        case thalamus_grpc::Image::Format::Image_Format_RGB:
          bps = {3, 3, 3};
          break;
        case thalamus_grpc::Image::Format::Image_Format_YUYV422:
          bps = {1, 2, 1};
          break;
        case thalamus_grpc::Image::Format::Image_Format_YUV420P:
          bps = {1, 1, 1};
          break;
        case thalamus_grpc::Image::Format::Image_Format_YUVJ420P:
          bps = {1, 1, 1};
          break;
        case thalamus_grpc::Image::Format::Image_Format_Gray16:
          bps = {2, 2, 2};
          break;
        case thalamus_grpc::Image::Format::Image_Format_RGB16:
          bps = {6, 6, 6};
          break;
        case thalamus_grpc::Image::Format::Image_Format_MPEG1:
        case thalamus_grpc::Image::Format::Image_Format_MPEG4:
        case thalamus_grpc::Image::Format::
            Image_Format_Image_Format_INT_MIN_SENTINEL_DO_NOT_USE_:
        case thalamus_grpc::Image::Format::
            Image_Format_Image_Format_INT_MAX_SENTINEL_DO_NOT_USE_:
          THALAMUS_ASSERT(false, "Unsupported format");
        }

        for (auto p = 0ull; p < size_t(image.data().size()); ++p) {
          auto linesize = image.data(int(p)).size() / image.height();
          auto width = image.width() * bps[p];
          for (auto y = 0u; y < image.height(); ++y) {
            std::copy_n(image.data(int(p)).data() + y * linesize, width,
                        src_data[p] + y * uint32_t(src_linesize[p]));
          }
        }

        if (pts > 0 && src_format == AV_PIX_FMT_GRAY8) {
          dst_data[0] = src_data[0];
        } else {
          sws_scale(sws_context, src_data, src_linesize, 0, int(image.height()),
                    dst_data, dst_linesize);
        }
        std::copy(std::begin(dst_data), std::end(dst_data), frame->data);
        std::copy(std::begin(dst_linesize), std::end(dst_linesize),
                  frame->linesize);

        frame->pts = pts++;

        ret = avcodec_send_frame(context, frame);
        THALAMUS_ASSERT(ret >= 0, "Error sending a frame for encoding");

        auto data = compressed_image->add_data();
        while (ret >= 0) {
          ret = avcodec_receive_packet(context, packet);
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
#endif
          if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
          }
#ifdef __clang__
#pragma clang diagnostic pop
#endif
          THALAMUS_ASSERT(ret >= 0, "Error during encoding");
          data->append(reinterpret_cast<char *>(packet->data),
                       size_t(packet->size));
          av_packet_unref(packet);
        }
        out_queue.push_back(compressed_record);
      }
      in_queue.clear();
    }
    void finish() override {
      thalamus_grpc::StorageRecord compressed_record;
      auto compressed_image = compressed_record.mutable_image();
      compressed_record.set_node(node);
      compressed_image->set_format(
          thalamus_grpc::Image::Format::Image_Format_MPEG4);

      auto ret = avcodec_send_frame(context, nullptr);
      THALAMUS_ASSERT(ret >= 0, "Error sending a frame for encoding");

      auto data = compressed_image->add_data();
      while (ret >= 0) {
        ret = avcodec_receive_packet(context, packet);
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
#endif
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
#ifdef __clang__
#pragma clang diagnostic pop
#endif
          break;
        }
        THALAMUS_ASSERT(ret >= 0, "Error during encoding");
        data->append(reinterpret_cast<char *>(packet->data),
                     size_t(packet->size));
        av_packet_unref(packet);
      }
      out_queue.push_back(compressed_record);
    }
    void push(thalamus_grpc::StorageRecord &&record) override;
    std::optional<thalamus_grpc::StorageRecord> pull() override {
      if (!out_queue.empty()) {
        std::optional<thalamus_grpc::StorageRecord> result =
            std::move(out_queue.front());
        out_queue.pop_front();
        return result;
      }
      return std::nullopt;
    }
  };

  struct ZlibEncoder : public Encoder {
    int stream_id;
    z_stream zstream;
    std::vector<thalamus_grpc::StorageRecord> in_queue;
    std::list<thalamus_grpc::StorageRecord> out_queue;
    thalamus_grpc::StorageRecord current;

    ZlibEncoder(int _stream_id) : stream_id(_stream_id) {
      zstream.zalloc = nullptr;
      zstream.zfree = nullptr;
      zstream.opaque = nullptr;
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
#endif
      auto error = deflateInit(&zstream, 1);
#ifdef __clang__
#pragma clang diagnostic pop
#endif
      THALAMUS_ASSERT(error == Z_OK, "ZLIB Error: %d", error);
    }

    void work() override {
      for (auto &record : in_queue) {
        auto serialized = record.SerializePartialAsString();
        thalamus_grpc::StorageRecord compressed_record;
        compressed_record.set_time(record.time());
        auto record_compressed = compressed_record.mutable_compressed();
        record_compressed->set_type(
            thalamus_grpc::Compressed::Type::Compressed_Type_ANALOG);
        record_compressed->set_stream(stream_id);
        record_compressed->set_size(int(serialized.size()));
        auto compressed_data = record_compressed->mutable_data();
        if (compressed_data->empty()) {
          compressed_data->resize(1024);
        }

        zstream.avail_in = uint32_t(serialized.size());
        zstream.next_in = reinterpret_cast<unsigned char *>(serialized.data());
        auto compressing = true;
        size_t offset = 0;
        while (compressing) {
          zstream.avail_out = uint32_t(compressed_data->size() - offset);
          zstream.next_out =
              reinterpret_cast<unsigned char *>(compressed_data->data()) +
              offset;
          auto error = deflate(&zstream, Z_NO_FLUSH);
          THALAMUS_ASSERT(error == Z_OK, "ZLIB Error: %d", error);
          compressing = zstream.avail_out == 0;
          if (compressing) {
            offset = compressed_data->size();
            compressed_data->resize(2 * compressed_data->size());
          }
        }
        compressed_data->resize(compressed_data->size() - zstream.avail_out);
        out_queue.push_back(std::move(compressed_record));
      }
      in_queue.clear();
    }
    void finish() override {
      thalamus_grpc::StorageRecord compressed_record;
      auto record_compressed = compressed_record.mutable_compressed();
      record_compressed->set_type(
          thalamus_grpc::Compressed::Type::Compressed_Type_NONE);
      record_compressed->set_stream(stream_id);
      auto compressed_data = record_compressed->mutable_data();
      if (compressed_data->empty()) {
        compressed_data->resize(1024);
      }

      auto compressing = true;
      size_t offset = 0;
      while (compressing) {
        zstream.avail_out = uint32_t(compressed_data->size() - offset);
        zstream.next_out =
            reinterpret_cast<unsigned char *>(compressed_data->data()) + offset;
        auto error = deflate(&zstream, Z_FINISH);
        THALAMUS_ASSERT(error == Z_OK || error == Z_STREAM_END,
                        "ZLIB Error: %d", error);
        compressing = zstream.avail_out == 0;
        if (compressing) {
          offset = compressed_data->size();
          compressed_data->resize(2 * compressed_data->size());
        }
      }
      compressed_data->resize(compressed_data->size() - zstream.avail_out);
      out_queue.push_back(std::move(compressed_record));
    }
    void push(thalamus_grpc::StorageRecord &&record) override;
    std::optional<thalamus_grpc::StorageRecord> pull() override {
      if (!out_queue.empty()) {
        std::optional<thalamus_grpc::StorageRecord> result =
            std::move(out_queue.front());
        out_queue.pop_front();
        return result;
      }
      return std::nullopt;
    }
  };

  void thread_target(std::string output_file) {
    set_current_thread_name("STORAGE");
    prepare_storage(output_file);
    Finally f([&] { close_file(); });

    SimplePool<thalamus_grpc::StorageRecord> record_pool;

    IdentityEncoder identity_encoder;
    std::map<int, std::unique_ptr<ZlibEncoder>> zlib_encoders;
    std::map<std::string, std::unique_ptr<VideoEncoder>> video_encoders;
    std::vector<Encoder *> encoders;
    encoders.push_back(&identity_encoder);
    std::string buffer;

    std::vector<std::pair<double, AVRational>> framerates = {
        {24000.0 / 1001, {24000, 1001}},
        {24, {24, 1}},
        {25, {25, 1}},
        {30000.0 / 1001, {30000, 1001}},
        {30, {30, 1}},
        {50, {50, 1}},
        {60000.0 / 1001, {60000, 1001}},
        {60, {60, 1}},
        {15, {15, 1}},
        {5, {5, 1}},
        {10, {10, 1}},
        {12, {12, 1}},
        {15, {15, 1}}};
    std::sort(framerates.begin(), framerates.end(),
              [](auto &lhs, auto &rhs) { return lhs.first < rhs.first; });

    auto service_encoders = [&](bool finish) {
      std::mutex mutex;
      std::condition_variable condition;
      auto band_size = std::max(size_t(1), encoders.size() / pool.num_threads);
      band_size += (band_size * pool.num_threads < encoders.size()) ? 1 : 0;
      size_t pending_bands = encoders.size() / band_size;
      pending_bands += (pending_bands * band_size < encoders.size()) ? 1 : 0;
      {
        TRACE_EVENT("thalamus", "encode_all");
        for (auto i = 0ull; i < encoders.size(); i += band_size) {
          auto upper = std::min(size_t(i + band_size), encoders.size());
          pool.push([&, i, upper] {
            TRACE_EVENT("thalamus", "encode");
            for (auto j = i; j < upper; ++j) {
              finish ? encoders[j]->finish() : encoders[j]->work();
            }
            std::lock_guard<std::mutex> lock(mutex);
            --pending_bands;
            condition.notify_all();
          });
        }
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [&] { return pending_bands == 0; });
      }

      std::vector<std::pair<uint64_t, thalamus_grpc::StorageRecord>> heap;
      auto comparator = [](decltype(heap)::value_type lhs,
                           decltype(heap)::value_type rhs) {
        return lhs.first > rhs.first;
      };

      {
        TRACE_EVENT("thalamus", "sort");
        for (auto encoder : encoders) {
          auto record = encoder->pull();
          while (record) {
            heap.emplace_back(record->time(), std::move(*record));
            std::push_heap(heap.begin(), heap.end(), comparator);
            record = encoder->pull();
          }
        }
      }

      {
        TRACE_EVENT("thalamus", "serialize");
        buffer.clear();
        while (!heap.empty()) {
          std::pop_heap(heap.begin(), heap.end(), comparator);
          auto serialized = heap.back().second.SerializePartialAsString();
          heap.pop_back();

          auto size = serialized.size();
          auto bigendian_size = htonll(size);
          auto size_bytes = reinterpret_cast<char *>(&bigendian_size);
          buffer.append(size_bytes, sizeof(bigendian_size));
          buffer.append(serialized);
        }
      }

      TRACE_EVENT("thalamus", "write");
      output_stream.write(buffer.data(), int64_t(buffer.size()));
    };

    while (is_running) {
      std::vector<std::pair<thalamus_grpc::StorageRecord, int>> local_records;
      {
        std::unique_lock<std::mutex> lock(records_mutex);
        records_condition.wait_for(
            lock, 1s, [&] { return !records.empty() || !is_running; });
        local_records.swap(records);
      }
      for (auto &record_pair : local_records) {
        auto &[record, stream] = record_pair;
        auto body_type = record.body_case();
        if (body_type == thalamus_grpc::StorageRecord::kAnalog &&
            compress_analog) {
          if (!zlib_encoders.contains(stream)) {
            auto encoder = std::make_unique<ZlibEncoder>(stream);
            encoders.push_back(encoder.get());
            zlib_encoders[stream] = std::move(encoder);
          }
          zlib_encoders[stream]->push(std::move(record));
        } else if (body_type == thalamus_grpc::StorageRecord::kImage &&
                   compress_video) {
          if (!video_encoders.contains(record.node())) {
            auto &image = record.image();
            auto framerate_original = image.frame_interval()
                                          ? 1e9 / double(image.frame_interval())
                                          : 1.0 / 60;
            auto framerate_i = std::lower_bound(
                framerates.begin(), framerates.end(),
                std::make_pair(framerate_original, AVRational{1, 1}),
                [](auto &lhs, auto &rhs) { return lhs.first < rhs.first; });

            AVRational framerate;
            if (framerate_i == framerates.begin()) {
              framerate = framerates.front().second;
            } else if (framerate_i == framerates.end()) {
              framerate = framerates.back().second;
            } else {
              if (framerate_original - (framerate_i - 1)->first <
                  framerate_i->first - framerate_original) {
                framerate = (framerate_i - 1)->second;
              } else {
                framerate = framerate_i->second;
              }
            }

            AVPixelFormat format;
            switch (image.format()) {
            case thalamus_grpc::Image::Format::Image_Format_Gray:
              format = AV_PIX_FMT_GRAY8;
              break;
            case thalamus_grpc::Image::Format::Image_Format_Gray16:
              format =
                  image.bigendian() ? AV_PIX_FMT_GRAY16BE : AV_PIX_FMT_GRAY16LE;
              break;
            case thalamus_grpc::Image::Format::Image_Format_RGB:
              format = AV_PIX_FMT_RGB24;
              break;
            case thalamus_grpc::Image::Format::Image_Format_YUYV422:
            case thalamus_grpc::Image::Format::Image_Format_YUV420P:
            case thalamus_grpc::Image::Format::Image_Format_YUVJ420P:
            case thalamus_grpc::Image::Format::Image_Format_RGB16:
            case thalamus_grpc::Image::Format::Image_Format_MPEG1:
            case thalamus_grpc::Image::Format::Image_Format_MPEG4:
            case thalamus_grpc::Image::Format::
                Image_Format_Image_Format_INT_MIN_SENTINEL_DO_NOT_USE_:
            case thalamus_grpc::Image::Format::
                Image_Format_Image_Format_INT_MAX_SENTINEL_DO_NOT_USE_:
              THALAMUS_ASSERT(false, "Usupported image format");
            }

            auto encoder = std::make_unique<VideoEncoder>(
                image.width(), image.height(), format, framerate,
                record.node());
            encoders.push_back(encoder.get());
            video_encoders[record.node()] = std::move(encoder);
          }
          video_encoders[record.node()]->push(std::move(record));
        } else {
          identity_encoder.push(std::move(record));
        }
      }

      service_encoders(false);
    }
    service_encoders(true);
  }

  void queue_record(thalamus_grpc::StorageRecord &&record, int stream = 0) {
    // TRACE_EVENT("thalamus", "StorageNode::queue_record");
    ++queued_records;
    queued_bytes += record.ByteSizeLong();
    std::lock_guard<std::mutex> lock(records_mutex);
    records.emplace_back(std::move(record), stream);
    records_condition.notify_one();
  }

  void on_stats_timer(const boost::system::error_code &error) {
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
    for (auto i = metrics.begin(); i < metrics.end(); ++i) {
      if (i->second) {
        i->first /= double(elapsed.count()) / decltype(elapsed)::period::den;
      }
    }
    metrics_time = now.time_since_epoch();
    outer->ready(outer);
    last_publish = now;
    for (auto i = metrics.begin(); i < metrics.end(); ++i) {
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
    if (_thread.joinable()) {
      _thread.join();
    }
  }

  bool compress_analog = false;
  bool compress_video = false;

  void on_change(ObservableCollection::Action,
                 const ObservableCollection::Key &key,
                 const ObservableCollection::Value &) {
    auto key_str = std::get<std::string>(key);
    if (key_str == "rec") {
      return;
    }

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
    compress_analog = state->contains("Compress Analog")
                          ? state->at("Compress Analog")
                          : false;
    compress_video =
        state->contains("Compress Video") ? state->at("Compress Video") : false;

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
      for (auto &raw_token : tokens) {
        auto token = std::string(absl::StripAsciiWhitespace(raw_token));

        graph->get_node(token, [this, token, i](auto source) {
          auto locked_source = source.lock();
          if (!locked_source) {
            return;
          }

          if (node_cast<MotionCaptureNode *>(locked_source.get()) != nullptr) {
            auto xsens_source =
                node_cast<MotionCaptureNode *>(locked_source.get());
            auto xsens_source_connection =
                locked_source->ready.connect(std::bind(
                    &Impl::on_xsens_data, this, _1, token, xsens_source, i));
            source_connections.push_back(std::move(xsens_source_connection));
          }
          if (node_cast<AnalogNode *>(locked_source.get()) != nullptr) {
            auto analog_source = node_cast<AnalogNode *>(locked_source.get());
            auto analog_source_connection = locked_source->ready.connect(
                std::bind(&Impl::on_data, this, _1, token, analog_source, i));
            source_connections.push_back(std::move(analog_source_connection));
          }
          if (node_cast<ImageNode *>(locked_source.get()) != nullptr) {
            auto image_source = node_cast<ImageNode *>(locked_source.get());
            auto image_source_connection =
                locked_source->ready.connect(std::bind(
                    &Impl::on_image_data, this, _1, token, image_source, i));
            source_connections.push_back(std::move(image_source_connection));
          }
          if (node_cast<TextNode *>(locked_source.get()) != nullptr) {
            auto text_source = node_cast<TextNode *>(locked_source.get());
            auto text_source_connection =
                locked_source->ready.connect(std::bind(
                    &Impl::on_text_data, this, _1, token, text_source, i));
            source_connections.push_back(std::move(text_source_connection));
          }
        });
        ++i;
      }
    }
  }
};

StorageNode::Impl::Encoder::~Encoder() {}
void StorageNode::Impl::ZlibEncoder::push(thalamus_grpc::StorageRecord &&record) {
  in_queue.push_back(std::move(record));
}
void StorageNode::Impl::VideoEncoder::push(thalamus_grpc::StorageRecord &&record) {
  in_queue.push_back(std::move(record));
}
void StorageNode::Impl::IdentityEncoder::work() {}

StorageNode::StorageNode(ObservableDictPtr state,
                         boost::asio::io_context &io_context, NodeGraph *graph)
    : impl(new Impl(state, io_context, graph, this)) {}

StorageNode::~StorageNode() {}

std::string StorageNode::type_name() { return "STORAGE"; }

std::filesystem::path
StorageNode::get_next_file(const std::filesystem::path &name,
                           std::chrono::system_clock::time_point time) {
  inja::json tdata;
  auto rec = get_rec_number(name, tdata, time);

  const auto start_time = absl::FromChrono(time);
  auto start_time_str =
      absl::FormatTime("%Y%m%d", start_time, absl::LocalTimeZone());
  auto filename =
      absl::StrFormat("%s.%s.%d", name.string(), start_time_str, rec);
  return std::move(filename);
}

void StorageNode::record(std::ofstream &output,
                         const thalamus_grpc::StorageRecord &record) {
  auto serialized = record.SerializePartialAsString();
  auto size = serialized.size();
  size = htonll(size);
  auto size_bytes = reinterpret_cast<char *>(&size);
  output.write(size_bytes, sizeof(size));
  output.write(serialized.data(), int64_t(serialized.size()));
}

void StorageNode::record(std::ofstream &output, const std::string &serialized) {
  auto size = serialized.size();
  size = htonll(size);
  auto size_bytes = reinterpret_cast<char *>(&size);
  output.write(size_bytes, sizeof(size));
  output.write(serialized.data(), int64_t(serialized.size()));
}

std::span<const double> StorageNode::data(int channel) const {
  return std::span<const double>(&(impl->metrics.begin() + channel)->first,
                                 &(impl->metrics.begin() + channel)->first + 1);
}

int StorageNode::num_channels() const { return int(impl->metrics.size()); }

std::chrono::nanoseconds StorageNode::sample_interval(int) const { return 1s; }

std::chrono::nanoseconds StorageNode::time() const {
  return impl->metrics_time;
}

std::string_view StorageNode::name(int channel) const {
  return impl->names.at(size_t(channel));
}
std::span<const std::string> StorageNode::get_recommended_channels() const {
  return std::span<const std::string>(impl->names.begin(), impl->names.end());
}

void StorageNode::inject(const thalamus::vector<std::span<double const>> &,
                         const thalamus::vector<std::chrono::nanoseconds> &,
                         const thalamus::vector<std::string_view> &) {}
} // namespace thalamus
