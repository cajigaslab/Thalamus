#include <atomic>
#include <modalities_util.hpp>
#include <thalamus/thread.hpp>
#include <thalamus/tracing.hpp>
#include <video_node.hpp>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>
}
#ifdef __clang__
#pragma clang diagnostic pop
#endif

namespace thalamus {
using namespace std::chrono_literals;

struct VideoNode::Impl {
  ObservableDictPtr state;
  boost::asio::io_context &io_context;
  boost::signals2::scoped_connection state_connection;
  boost::signals2::scoped_connection options_connection;
  NodeGraph::NodeConnection node_connection;
  boost::signals2::scoped_connection data_connection;
  bool is_running = false;
  VideoNode *outer;
  std::chrono::nanoseconds time;
  std::thread ffmpeg_thread;
  std::atomic_bool running = false;
  thalamus_grpc::Image image;
  std::atomic_bool frame_pending;
  std::vector<unsigned char> intermediate;
  thalamus::vector<Plane> data;
  std::chrono::nanoseconds frame_interval;
  Format format;
  size_t width;
  size_t height;
  bool has_analog = false;
  bool has_image = false;
  NodeGraph *graph;
  std::atomic_int stream_time;
  double framerate = 0;
  double bps = 0;

  Impl(ObservableDictPtr _state, boost::asio::io_context &_io_context,
       NodeGraph *_graph, VideoNode *_outer)
      : state(_state), io_context(_io_context), outer(_outer), graph(_graph),
        stream_time(-1) {
    using namespace std::placeholders;

    state_connection =
        state->changed.connect(std::bind(&Impl::on_change, this, _1, _2, _3));
    this->state->recap(std::bind(&Impl::on_change, this, _1, _2, _3));
  }

  ~Impl() {
    (*state)["Running"].assign(false, [&] {});
  }

  struct VideoContext {
    AVFormatContext *format_context = nullptr;
    AVDictionary *options = nullptr;
    AVCodecContext *codec = nullptr;
    std::shared_ptr<AVFrame> frame = nullptr;
    AVPacket *packet = nullptr;
    VideoContext() { new_frame(); }
    void new_frame() {
      frame.reset(av_frame_alloc(),
                  [](AVFrame *self) { av_frame_free(&self); });
    }
    ~VideoContext() {
      if (format_context) {
        avformat_close_input(&format_context);
      }
      if (codec) {
        avcodec_free_context(&codec);
      }
      av_dict_free(&options);
      av_packet_free(&packet);
    }
  };

  static std::atomic_int frame_id;

  void ffmpeg_target(const std::string input_name) {
    set_current_thread_name("FFMPEG");
    VideoContext context;
    context.format_context = avformat_alloc_context();

    if (!context.format_context) {
      THALAMUS_LOG(error) << "Failed to allocate AVFormatContext";
      return;
    }

    auto err = avformat_open_input(&context.format_context, input_name.c_str(),
                                   nullptr, &context.options);
    if (err < 0) {
      THALAMUS_LOG(error) << "Failed to open input: " << input_name;
      return;
    }

    context.codec = avcodec_alloc_context3(nullptr);
    auto stream_index = av_find_best_stream(
        context.format_context, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    avcodec_parameters_to_context(
        context.codec, context.format_context->streams[stream_index]->codecpar);
    context.codec->pkt_timebase =
        context.format_context->streams[stream_index]->time_base;
    auto time_base = context.codec->pkt_timebase;
    auto av_frame_rate =
        context.format_context->streams[stream_index]->avg_frame_rate;
    std::chrono::nanoseconds new_frame_interval(
        std::chrono::nanoseconds::period::den * av_frame_rate.den /
        av_frame_rate.num);
    boost::asio::post(io_context, [&, new_frame_interval] {
      this->frame_interval = new_frame_interval;
    });

    auto codec = avcodec_find_decoder(context.codec->codec_id);
    {
      VideoContext sub_context;
      err = av_dict_copy(&sub_context.options, context.options, 0);
      if (err < 0) {
        THALAMUS_LOG(error) << "Failed to copy options";
        return;
      }

      if (!av_dict_get(sub_context.options, "threads", nullptr, 0))
        av_dict_set(&sub_context.options, "threads", "auto", 0);

      av_dict_set(&sub_context.options, "flags", "+copy_opaque",
                  AV_DICT_MULTIKEY);

      err = avcodec_open2(context.codec, codec, &sub_context.options);
      if (err < 0) {
        THALAMUS_LOG(error) << "Failed to open codec context";
        return;
      }
    }

    context.packet = av_packet_alloc();

    auto stop = [this] { (*state)["Running"].assign(false, [&] {}); };
    int64_t start_pts = -1;
    auto start_time = std::chrono::steady_clock::now();
    std::set<std::chrono::steady_clock::time_point> frame_times;
    std::set<std::pair<std::chrono::steady_clock::time_point, int>> time_bits;
    while (running) {
      TRACE_EVENT("thalamus", "loop");
      {
        TRACE_EVENT("thalamus", "av_read_frame");
        err = av_read_frame(context.format_context, context.packet);
      }
      if (err < 0) {
        THALAMUS_LOG(error) << "Failed to read frame, stopping Video capture";
        boost::asio::post(io_context, stop);
        return;
      }
      if (context.packet->stream_index != stream_index) {
        TRACE_EVENT_INSTANT("thalamus", "wrong stream");
        av_packet_unref(context.packet);
        continue;
      }

      auto now = std::chrono::steady_clock::now();
      time_bits.emplace(now, 8 * context.packet->size);

      auto sleep_time = 0ns;
      if (start_pts < 0) {
        start_pts = context.packet->pts;
        start_time = now;
      } else {
        auto pts =
            1'000'000'000 * (context.packet->pts - start_pts) * time_base.num;
        pts /= time_base.den;
        auto target = start_time + std::chrono::nanoseconds(pts);
        sleep_time = target - now;
        if (now < target) {
          TRACE_EVENT("thalamus", "sleep_for");
          std::this_thread::sleep_for(target - now);
        }
      }

      {
        TRACE_EVENT("thalamus", "avcodec_send_packet");
        err = avcodec_send_packet(context.codec, context.packet);
      }
      if (err < 0) {
        THALAMUS_LOG(error) << "Failed to decode frame, stopping Video capture";
        boost::asio::post(io_context, stop);
        return;
      }
      av_packet_unref(context.packet);

      while (true) {
        auto id = get_unique_id();
        TRACE_EVENT_BEGIN("thalamus", "read frame",
                          perfetto::Flow::ProcessScoped(id));
        {
          TRACE_EVENT("thalamus", "avcodec_receive_frame");
          err = avcodec_receive_frame(context.codec, context.frame.get());
        }
        if (err == AVERROR(EAGAIN)) {
          TRACE_EVENT_END("thalamus");
          break;
        } else if (err < 0) {
          TRACE_EVENT_END("thalamus");
          THALAMUS_LOG(error)
              << "Failed to receive decode frame, stopping Video capture";
          boost::asio::post(io_context, stop);
          return;
        }

        if (frame_pending || context.frame->pict_type == AV_PICTURE_TYPE_NONE) {
          TRACE_EVENT_END("thalamus");
          continue;
        }

        data.clear();
        switch (context.frame->format) {
        case AV_PIX_FMT_GRAY8:
          format = Format::Gray;
          data.emplace_back(context.frame->data[0],
                            context.frame->data[0] +
                                context.frame->height *
                                    context.frame->linesize[0]);
          break;
        case AV_PIX_FMT_RGB24:
          format = Format::RGB;
          data.emplace_back(context.frame->data[0],
                            context.frame->data[0] +
                                context.frame->height *
                                    context.frame->linesize[0]);
          break;
        case AV_PIX_FMT_YUYV422:
          format = Format::YUYV422;
          data.emplace_back(context.frame->data[0],
                            context.frame->data[0] +
                                context.frame->height *
                                    context.frame->linesize[0]);
          break;
        case AV_PIX_FMT_YUVJ420P:
          format = Format::YUVJ420P;
          data.emplace_back(context.frame->data[0],
                            context.frame->data[0] +
                                context.frame->height *
                                    context.frame->linesize[0]);
          data.emplace_back(context.frame->data[1],
                            context.frame->data[1] +
                                context.frame->height / 2 *
                                    context.frame->linesize[1]);
          data.emplace_back(context.frame->data[2],
                            context.frame->data[2] +
                                context.frame->height / 2 *
                                    context.frame->linesize[2]);
          break;
        case AV_PIX_FMT_YUV420P:
          format = Format::YUV420P;
          data.emplace_back(context.frame->data[0],
                            context.frame->data[0] +
                                context.frame->height *
                                    context.frame->linesize[0]);
          data.emplace_back(context.frame->data[1],
                            context.frame->data[1] +
                                context.frame->height / 2 *
                                    context.frame->linesize[1]);
          data.emplace_back(context.frame->data[2],
                            context.frame->data[2] +
                                context.frame->height / 2 *
                                    context.frame->linesize[2]);
          break;
        default:
          TRACE_EVENT_END("thalamus");
          THALAMUS_LOG(error)
              << "Unsupported pixel format: " << context.frame->format;
          boost::asio::post(io_context, stop);
          return;
        }

        while (!frame_times.empty() && now - *frame_times.begin() > 1s) {
          frame_times.erase(frame_times.begin());
        }
        frame_times.insert(now);

        while (!time_bits.empty() && now - time_bits.begin()->first > 1s) {
          time_bits.erase(time_bits.begin());
        }

        auto new_bps =
            std::accumulate(time_bits.begin(), time_bits.end(), 0.0,
                            [](auto a, auto b) { return a + b.second; });

        width = size_t(context.frame->width);
        height = size_t(context.frame->height);

        auto frame_copy = context.frame;
        context.new_frame();
        frame_pending = true;
        TRACE_EVENT_END("thalamus");
        boost::asio::post(io_context, [&, now, frame_copy,
                                       new_framerate = frame_times.size(),
                                       new_bps, id] {
          TRACE_EVENT("thalamus", "VideoNode Post Main",
                      perfetto::TerminatingFlow::ProcessScoped(id));
          this->time = now.time_since_epoch();
          this->has_image = true;
          this->has_analog = true;
          this->framerate = double(new_framerate);
          this->bps = new_bps;
          frame_pending = false;
          TRACE_EVENT("thalamus", "VideoNode_ready");
          outer->ready(outer);
        });
      }
    }
  }

  void start() {
    std::string filename =
        state->contains("File Name") ? state->at("File Name") : std::string();

    ffmpeg_thread =
        std::thread(std::bind(&Impl::ffmpeg_target, this, filename));
  }

  void stop() {
    if (ffmpeg_thread.joinable()) {
      ffmpeg_thread.join();
    }
  }

  void on_change(ObservableCollection::Action,
                 const ObservableCollection::Key &k,
                 const ObservableCollection::Value &v) {
    auto key_str = std::get<std::string>(k);
    if (key_str == "Running") {
      running = std::get<bool>(v);
      if (running) {
        start();
      } else {
        stop();
      }
    }
  }
};

VideoNode::VideoNode(ObservableDictPtr state,
                     boost::asio::io_context &io_context, NodeGraph *graph)
    : impl(new Impl(state, io_context, graph, this)) {}

VideoNode::~VideoNode() {}

std::string VideoNode::type_name() { return "VIDEO"; }

ImageNode::Plane VideoNode::plane(int i) const {
  return impl->data.at(size_t(i));
}

size_t VideoNode::num_planes() const { return impl->data.size(); }

ImageNode::Format VideoNode::format() const { return impl->format; }

size_t VideoNode::width() const { return impl->width; }

size_t VideoNode::height() const { return impl->height; }

void VideoNode::inject(const thalamus_grpc::Image &) { THALAMUS_ASSERT(false, "Unimplemented"); }

std::chrono::nanoseconds VideoNode::time() const { return impl->time; }

std::chrono::nanoseconds VideoNode::frame_interval() const {
  return impl->frame_interval;
}

bool VideoNode::prepare() {
  avdevice_register_all();
  return true;
}

std::span<const double> VideoNode::data(int index) const {
  if (index == 0) {
    return std::span<const double>(&impl->framerate, &impl->framerate + 1);
  } else if (index == 1) {
    return std::span<const double>(&impl->bps, &impl->bps + 1);
  }
  return {};
}

int VideoNode::num_channels() const { return 2; }

std::chrono::nanoseconds VideoNode::sample_interval(int) const {
  return impl->frame_interval;
}

std::string_view VideoNode::name(int channel) const {
  if (channel == 0) {
    return "Framerate";
  } else if (channel == 1) {
    return "BPS";
  }
  return "";
}

void VideoNode::inject(const thalamus::vector<std::span<double const>> &,
                       const thalamus::vector<std::chrono::nanoseconds> &,
                       const thalamus::vector<std::string_view> &) {
  impl->has_analog = true;
  impl->has_image = false;
}

bool VideoNode::has_analog_data() const { return impl->has_analog; }

bool VideoNode::has_image_data() const { return impl->has_image; }

size_t VideoNode::modalities() const { return infer_modalities<VideoNode>(); }

std::atomic_int VideoNode::Impl::frame_id = 0;
} // namespace thalamus
