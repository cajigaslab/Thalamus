#include <atomic>
#include <image_node.hpp>
#include <modalities_util.hpp>
#include <thalamus/thread.hpp>

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

struct FfmpegNode::Impl {
  ObservableDictPtr state;
  boost::asio::io_context &io_context;
  boost::signals2::scoped_connection state_connection;
  boost::signals2::scoped_connection options_connection;
  NodeGraph::NodeConnection node_connection;
  boost::signals2::scoped_connection data_connection;
  bool is_running = false;
  FfmpegNode *outer;
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
  AnalogNodeImpl analog_impl;
  bool has_analog = false;
  bool has_image = false;
  NodeGraph *graph;
  std::atomic_int stream_time;

  Impl(ObservableDictPtr _state, boost::asio::io_context &_io_context,
       NodeGraph *_graph, FfmpegNode *_outer)
      : state(_state), io_context(_io_context), outer(_outer), graph(_graph),
        stream_time(-1) {
    using namespace std::placeholders;
    analog_impl.inject({{std::span<double const>()}}, {0ns}, {""});

    analog_impl.ready.connect([_outer](Node *) { _outer->ready(_outer); });

    state_connection =
        state->changed.connect(std::bind(&Impl::on_change, this, _1, _2, _3));
    this->state->recap(std::bind(&Impl::on_change, this, _1, _2, _3));
  }

  ~Impl() {
    (*state)["Running"].assign(false, [&] {});
  }

  struct FfmpegContext {
    AVFormatContext *input_format = nullptr;
    AVDictionary *options = nullptr;
    AVCodecContext *codec = nullptr;
    std::shared_ptr<AVFrame> frame = nullptr;
    AVPacket *packet = nullptr;
    FfmpegContext() { new_frame(); }
    void new_frame() {
      frame.reset(av_frame_alloc(),
                  [](AVFrame *self) { av_frame_free(&self); });
    }
    ~FfmpegContext() {
      if (input_format) {
        avformat_close_input(&input_format);
      }
      if (codec) {
        avcodec_free_context(&codec);
      }
      av_dict_free(&options);
      av_packet_free(&packet);
    }
  };

  void ffmpeg_target(const std::string input_format_name,
                     const std::string input_name, AVDictionary *options) {
    set_current_thread_name("FFMPEG");
    FfmpegContext context;
    context.options = options;
    context.input_format = avformat_alloc_context();

    if (!context.input_format) {
      THALAMUS_LOG(error) << "Failed to allocate AVFormatContext";
      return;
    }

    const AVInputFormat *input_format = nullptr;
    if (!input_format_name.empty()) {
      input_format = av_find_input_format(input_format_name.c_str());
      if (!input_format) {
        THALAMUS_LOG(error) << "Input Format not found " << input_format_name;
        return;
      }
    }

    auto err = avformat_open_input(&context.input_format, input_name.c_str(),
                                   input_format, &context.options);
    if (err < 0) {
      THALAMUS_LOG(error) << "Failed to open input: " << input_name;
      return;
    }

    context.codec = avcodec_alloc_context3(nullptr);
    auto stream_index = av_find_best_stream(
        context.input_format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    avcodec_parameters_to_context(
        context.codec, context.input_format->streams[stream_index]->codecpar);
    context.codec->pkt_timebase =
        context.input_format->streams[stream_index]->time_base;
    auto time_base = context.codec->pkt_timebase;
    auto av_frame_rate =
        context.input_format->streams[stream_index]->avg_frame_rate;
    std::chrono::nanoseconds new_frame_interval(
        std::chrono::nanoseconds::period::den * av_frame_rate.den /
        av_frame_rate.num);
    boost::asio::post(io_context, [&, new_frame_interval] {
      this->frame_interval = new_frame_interval;
    });

    auto codec = avcodec_find_decoder(context.codec->codec_id);
    {
      FfmpegContext sub_context;
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
    while (running) {
      err = av_read_frame(context.input_format, context.packet);
      if (err < 0) {
        THALAMUS_LOG(error) << "Failed to read frame, stopping Ffmpeg capture";
        boost::asio::post(io_context, stop);
        return;
      }
      if (context.packet->stream_index != stream_index) {
        av_packet_unref(context.packet);
        continue;
      }

      auto now = std::chrono::steady_clock::now();
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
          std::this_thread::sleep_for(target - now);
        }
      }

      err = avcodec_send_packet(context.codec, context.packet);
      if (err < 0) {
        THALAMUS_LOG(error)
            << "Failed to decode frame, stopping Ffmpeg capture";
        boost::asio::post(io_context, stop);
        return;
      }
      av_packet_unref(context.packet);

      while (true) {
        err = avcodec_receive_frame(context.codec, context.frame.get());
        if (err == AVERROR(EAGAIN)) {
          break;
        } else if (err < 0) {
          THALAMUS_LOG(error)
              << "Failed to receive decode frame, stopping Ffmpeg capture";
          boost::asio::post(io_context, stop);
          return;
        }

        if (frame_pending || context.frame->pict_type == AV_PICTURE_TYPE_NONE) {
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
          THALAMUS_LOG(error)
              << "Unsupported pixel format: " << context.frame->format;
          boost::asio::post(io_context, stop);
          return;
        }

        while (!frame_times.empty() && now - *frame_times.begin() > 1s) {
          frame_times.erase(frame_times.begin());
        }
        frame_times.insert(now);

        width = size_t(context.frame->width);
        height = size_t(context.frame->height);

        auto frame_copy = context.frame;
        context.new_frame();
        frame_pending = true;
        boost::asio::post(io_context, [&, now, frame_copy,
                                       framerate = double(frame_times.size()),
                                       sleep_time_sec = 1e-9 * double(sleep_time.count()),
                                       new_frame_interval] {
          this->time = now.time_since_epoch();
          this->has_image = true;
          this->has_analog = true;
          double target_framerate = 1e9 / double(this->frame_interval.count());
          analog_impl.inject(
              {
                  std::span<double const>(&target_framerate,
                                          &target_framerate + 1),
                  std::span<double const>(&framerate, &framerate + 1),
                  std::span<double const>(&sleep_time_sec, &sleep_time_sec + 1),
              },
              {new_frame_interval, new_frame_interval, new_frame_interval}, {""});

          frame_pending = false;
        });
      }
    }
  }

  void start() {
    std::string input_format_name = state->contains("Input Format")
                                        ? state->at("Input Format")
                                        : std::string();
    std::string input_name =
        state->contains("Input Name") ? state->at("Input Name") : std::string();

    AVDictionary *options = nullptr;
    if (state->contains("Options")) {
      ObservableDictPtr state_options = state->at("Options");
      state_options->recap([&](ObservableCollection::Action,
                               const ObservableCollection::Key &k,
                               const ObservableCollection::Value &v) {
        auto key_str = std::get<std::string>(k);
        auto value_str = std::get<std::string>(v);
        av_dict_set(&options, key_str.c_str(), value_str.c_str(), 0);
      });
    }

    ffmpeg_thread = std::thread(std::bind(
        &Impl::ffmpeg_target, this, input_format_name, input_name, options));
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
    } else if (key_str == "Time Source") {
      auto val_str = std::get<std::string>(v);
      absl::StripAsciiWhitespace(&val_str);
      data_connection.disconnect();
      if (val_str.empty()) {
        node_connection.disconnect();
        stream_time = -1;
        return;
      }
      node_connection = graph->get_node_scoped(
          val_str, [this](std::weak_ptr<Node> weak_node) {
            auto node = weak_node.lock();
            auto analog = node_cast<AnalogNode *>(node.get());
            data_connection = node->ready.connect([this, analog](auto) {
              if (!analog->has_analog_data() || analog->num_channels() == 0) {
                return;
              }

              auto _data = analog->data(0);
              if (_data.empty()) {
                return;
              }
 
              stream_time = int(1000 * _data.front());
            });
          });
    }
  }
};

FfmpegNode::FfmpegNode(ObservableDictPtr state,
                       boost::asio::io_context &io_context, NodeGraph *graph)
    : impl(new Impl(state, io_context, graph, this)) {}

FfmpegNode::~FfmpegNode() {}

std::string FfmpegNode::type_name() { return "FFMPEG"; }

ImageNode::Plane FfmpegNode::plane(int i) const { return impl->data.at(size_t(i)); }

size_t FfmpegNode::num_planes() const { return impl->data.size(); }

ImageNode::Format FfmpegNode::format() const { return impl->format; }

size_t FfmpegNode::width() const { return impl->width; }

size_t FfmpegNode::height() const { return impl->height; }

void FfmpegNode::inject(const thalamus_grpc::Image &) {
  THALAMUS_ASSERT(false);
}

std::chrono::nanoseconds FfmpegNode::time() const { return impl->time; }

std::chrono::nanoseconds FfmpegNode::frame_interval() const {
  return impl->frame_interval;
}

bool FfmpegNode::prepare() {
  avdevice_register_all();
  return true;
}

std::span<const double> FfmpegNode::data(int index) const {
  return impl->analog_impl.data(index);
}

int FfmpegNode::num_channels() const {
  return impl->analog_impl.num_channels();
}

std::chrono::nanoseconds FfmpegNode::sample_interval(int channel) const {
  return impl->analog_impl.sample_interval(channel);
}

std::string_view FfmpegNode::name(int channel) const {
  switch (channel) {
  case 0:
    return "Target Framerate";
  case 1:
    return "Actual Framerate";
  case 2:
    return "Sleep Time";
  default:
    return "";
  }
}

void FfmpegNode::inject(
    const thalamus::vector<std::span<double const>> &data,
    const thalamus::vector<std::chrono::nanoseconds> &interval,
    const thalamus::vector<std::string_view> &) {
  impl->has_analog = true;
  impl->has_image = false;
  impl->analog_impl.inject(data, interval, {""});
}

bool FfmpegNode::has_analog_data() const { return impl->has_analog; }

bool FfmpegNode::has_image_data() const { return impl->has_image; }

size_t FfmpegNode::modalities() const { return infer_modalities<FfmpegNode>(); }
} // namespace thalamus
