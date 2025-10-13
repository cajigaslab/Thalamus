#include <thalamus/record_reader.hpp>
#include <thalamus/log.hpp>
#include <thalamus/assert.hpp>

#include <iostream>
#include <map>
#include <vector>

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

#define ZLIB_CONST
#include <zlib.h>

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

using namespace thalamus;

static char hydrate_av_error[AV_ERROR_MAX_STRING_SIZE];

struct RecordReader::Impl {
  std::istream &stream;
  double progress = 0;
  std::map<int, z_stream> zstreams;
  std::map<int, std::pair<size_t, std::vector<unsigned char>>> zstream_buffers;
  std::list<thalamus_grpc::StorageRecord> record_buffer;

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

  bool do_decode_video;

  Impl(std::istream &_stream, bool _do_decode_video = true)
      : stream(_stream), do_decode_video(_do_decode_video) {
    std::sort(framerates.begin(), framerates.end(),
              [](auto &lhs, auto &rhs) { return lhs.first < rhs.first; });
  }
  ~Impl() {
    for (auto &pair : zstreams) {
      deflateEnd(&pair.second);
    }
  }

  int z = 0;

  struct VideoDecoder {
    const AVCodec *codec;
    AVCodecContext *context;
    AVPacket *packet;
    AVFrame *frame;
    AVCodecParserContext *parser;
    std::list<thalamus_grpc::StorageRecord> buffer;
    std::list<thalamus_grpc::StorageRecord> pending;

    ~VideoDecoder() {
      avcodec_free_context(&context);
      av_packet_free(&packet);
      av_frame_free(&frame);
      av_parser_close(parser);
    }

    VideoDecoder(int width, int height, AVRational framerate,
                 AVPixelFormat pixel_format,
                 thalamus_grpc::Image::Format image_format) {
      if (image_format == thalamus_grpc::Image::Format::Image_Format_MPEG4) {
        codec = avcodec_find_decoder(AV_CODEC_ID_MPEG4);
      } else {
        codec = avcodec_find_decoder(AV_CODEC_ID_MPEG1VIDEO);
      }

      THALAMUS_ASSERT(codec, "avcodec_find_decoder failed");
      parser = av_parser_init(codec->id);
      THALAMUS_ASSERT(parser, "av_parser_init failed");

      context = avcodec_alloc_context3(codec);
      THALAMUS_ASSERT(context, "avcodec_alloc_context3 failed");
      packet = av_packet_alloc();
      frame = av_frame_alloc();

      context->bit_rate = std::numeric_limits<int>::max();
      context->width = width;
      context->height = height;
      context->framerate = framerate;
      context->time_base = {framerate.den, framerate.num};

      context->pix_fmt = pixel_format;

      frame->format = pixel_format;
      frame->width = width;
      frame->height = height;

      auto ret = avcodec_open2(context, codec, nullptr);
      THALAMUS_ASSERT(ret >= 0, "Could not open codec: %d", ret);

      ret = av_frame_get_buffer(frame, 0);
      THALAMUS_ASSERT(ret >= 0, "Could not allocate the video frame data");
    }

    std::string empty_string;

    void decode(const thalamus_grpc::StorageRecord *record) {
      int ret = 0;
      if (record) {
        if (record->image().width() > 0) {
          pending.emplace_back();
          pending.back().set_time(record->time());
          pending.back().set_node(record->node());
          auto pending_image = pending.back().mutable_image();
          pending_image->set_width(record->image().width());
          pending_image->set_height(record->image().height());
          pending_image->set_format(
              thalamus_grpc::Image::Format::Image_Format_Gray);
          pending_image->set_frame_interval(record->image().frame_interval());
          pending_image->set_last(record->image().last());
          pending_image->set_bigendian(record->image().bigendian());
        }

        auto &data = record->image().data_size() ? record->image().data(0)
                                                 : empty_string;
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
#endif
        auto offset = 0;
        while (size_t(offset) < data.size()) {
          ret = av_parser_parse2(
              parser, context, &packet->data, &packet->size,
              reinterpret_cast<const unsigned char *>(data.data()) + offset,
              int(data.size()) - offset, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
          THALAMUS_ASSERT(ret >= 0, "av_parser_parse2 failed");

          offset += ret;

          if (packet->size) {
            ret = avcodec_send_packet(context, packet);
            THALAMUS_ASSERT(ret >= 0, "avcodec_send_packet failed");
          }
        }
      } else {
        ret = avcodec_send_packet(context, nullptr);
        av_strerror(ret, hydrate_av_error, sizeof(hydrate_av_error));
        THALAMUS_ASSERT(ret >= 0, "avcodec_send_packet failed %s", hydrate_av_error);
      }
      while (ret >= 0) {
        ret = avcodec_receive_frame(context, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
#ifdef __clang__
#pragma clang diagnostic pop
#endif
          break;
        }
        THALAMUS_ASSERT(ret >= 0, "Error during decoding");

        buffer.push_back(std::move(pending.front()));
        pending.pop_front();
        auto &buffer_record = buffer.back();
        for (auto i = 0; i < 1; ++i) {
          buffer_record.mutable_image()->add_data()->assign(
              frame->data[0],
              frame->data[0] + frame->linesize[0] * frame->height);
        }
      }
    }
    std::optional<thalamus_grpc::StorageRecord> pull() {
      if (!buffer.empty()) {
        auto result = std::move(buffer.front());
        buffer.pop_front();
        return std::move(result);
      }
      return std::nullopt;
    }
  };

  std::optional<thalamus_grpc::StorageRecord> read_record_from_stream() {
    while (true) {
      auto initial_position = stream.tellg();
      auto current_position = initial_position;

      stream.seekg(0, std::ios::end);
      auto file_size = stream.tellg();
      stream.seekg(initial_position);

      progress = 100.0 * double(current_position) / double(file_size);

      if (file_size == current_position) {
        // std::cout << "End of file" << std::endl;
        flush_video_decoders();
        return std::nullopt;
      }

      if (file_size - current_position < 8) {
        std::cout << "Not enough bytes to read message size, likely final "
                     "message was corrupted."
                  << std::endl;
        flush_video_decoders();
        return std::nullopt;
      }

      std::string buffer;
      buffer.resize(8);
      stream.read(buffer.data(), 8);
      size_t size = *reinterpret_cast<size_t *>(buffer.data());
      size = htonll(size);

      current_position = stream.tellg();
      if (size_t(file_size - current_position) < size) {
        std::cout << "Not enough bytes to read message, likely final message "
                     "was corrupted."
                  << std::endl;
        flush_video_decoders();
        return std::nullopt;
      }

      buffer.resize(size);
      stream.read(buffer.data(), int64_t(size));

      thalamus_grpc::StorageRecord record;
      auto parsed = record.ParseFromString(buffer);
      if (!parsed) {
        std::cout << "Failed to parse message" << std::endl;
        flush_video_decoders();
        return std::nullopt;
      }
      if (record.body_case() == thalamus_grpc::StorageRecord::kCompressed) {
        inflate_record(record.compressed());
        if (record.compressed().type() ==
            thalamus_grpc::Compressed::Type::Compressed_Type_NONE) {
          continue;
        }
      } else if (do_decode_video &&
                 record.body_case() == thalamus_grpc::StorageRecord::kImage &&
                 (record.image().format() ==
                      thalamus_grpc::Image::Format::Image_Format_MPEG1 ||
                  record.image().format() ==
                      thalamus_grpc::Image::Format::Image_Format_MPEG4)) {
        decode_video(record);
        if (record.image().width() == 0) {
          continue;
        }
      }
      return std::move(record);
    }
  }

  bool video_flushed = false;

  void flush_video_decoders() {
    if (video_flushed) {
      return;
    }
    video_flushed = true;
    for (auto &pair : video_decoders) {
      pair.second->decode(nullptr);
    }
  }

  std::map<std::string, std::unique_ptr<VideoDecoder>> video_decoders;
  void decode_video(const thalamus_grpc::StorageRecord &record) {
    auto &image = record.image();
    if (!video_decoders.contains(record.node())) {
      auto framerate_original = 1e9 / double(image.frame_interval());
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
        format = image.bigendian() ? AV_PIX_FMT_GRAY16BE : AV_PIX_FMT_GRAY16LE;
        break;
      case thalamus_grpc::Image::Format::Image_Format_MPEG1:
      case thalamus_grpc::Image::Format::Image_Format_MPEG4:
        format = AV_PIX_FMT_YUV420P;
        break;
      case thalamus_grpc::Image::Format::Image_Format_RGB:
      case thalamus_grpc::Image::Format::Image_Format_YUYV422:
      case thalamus_grpc::Image::Format::Image_Format_YUV420P:
      case thalamus_grpc::Image::Format::Image_Format_YUVJ420P:
      case thalamus_grpc::Image::Format::Image_Format_RGB16:
      case thalamus_grpc::Image::Format::
          Image_Format_Image_Format_INT_MIN_SENTINEL_DO_NOT_USE_:
      case thalamus_grpc::Image::Format::
          Image_Format_Image_Format_INT_MAX_SENTINEL_DO_NOT_USE_:
        THALAMUS_ASSERT(false, "Usupported image format");
      }

      video_decoders[record.node()] = std::make_unique<VideoDecoder>(
          image.width(), image.height(), framerate, format, image.format());
    }
    video_decoders[record.node()]->decode(&record);
    // if(image.width() == 0) {
    //   video_decoders[record.node()]->decode(nullptr);
    // }
  }

  void inflate_record(const thalamus_grpc::Compressed &compressed) {
    auto &compressed_data = compressed.data();
    // std::cout << compressed.stream() << std::endl;
    if (!zstreams.contains(compressed.stream())) {
      // std::cout << "create " << compressed.stream() << std::endl;
      zstreams[compressed.stream()] = z_stream();
      auto &zstream = zstreams[compressed.stream()];
      zstream.zalloc = nullptr;
      zstream.zfree = nullptr;
      zstream.opaque = nullptr;
      zstream.avail_in = 0;
      zstream.next_in = nullptr;
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
#endif
      auto error = inflateInit(&zstream);
#ifdef __clang__
#pragma clang diagnostic pop
#endif
      THALAMUS_ASSERT(error == Z_OK, "ZLIB Error: %d", error);
      zstream_buffers[compressed.stream()] =
          std::make_pair(0ull, std::vector<unsigned char>(1024));
    }
    auto &zstream = zstreams[compressed.stream()];
    auto &[offset, zbuffer] = zstream_buffers[compressed.stream()];
    zstream.avail_in = uint32_t(compressed_data.size());
    zstream.next_in =
        reinterpret_cast<const unsigned char *>(compressed_data.data());
    auto compressing = true;
    while (compressing) {
      zstream.avail_out = uint32_t(zbuffer.size() - offset);
      zstream.next_out = zbuffer.data() + offset;
      auto error = inflate(&zstream, Z_NO_FLUSH);
      THALAMUS_ASSERT(error == Z_OK || error == Z_BUF_ERROR ||
                          error == Z_STREAM_END,
                      "ZLIB Error: %d", error);
      compressing = zstream.avail_out == 0;
      if (compressing) {
        offset = zbuffer.size();
        zbuffer.resize(2 * zbuffer.size());
      }
    }
    offset = zbuffer.size() - zstream.avail_out;
  }

  std::optional<thalamus_grpc::StorageRecord>
  process_record(const thalamus_grpc::StorageRecord &record) {
    if (record.body_case() == thalamus_grpc::StorageRecord::kCompressed) {
      auto compressed = record.compressed();
      auto i = zstream_buffers.find(compressed.stream());
      auto offset = i->second.first;

      while (offset < size_t(compressed.size())) {
        auto new_record = read_record_from_stream();
        i = zstream_buffers.find(compressed.stream());
        offset = i->second.first;
        if (!new_record) {
          break;
        }
        record_buffer.push_back(*new_record);
      }
      if (offset < size_t(compressed.size())) {
        return std::nullopt;
      }

      thalamus_grpc::StorageRecord inflated_record;
      auto parsed = inflated_record.ParseFromArray(i->second.second.data(),
                                                   compressed.size());
      if (!parsed) {
        return std::nullopt;
      }
      i->second.second.erase(i->second.second.begin(),
                             i->second.second.begin() + compressed.size());
      i->second.first -= size_t(compressed.size());

      return std::move(inflated_record);
    } else if (do_decode_video &&
               record.body_case() == thalamus_grpc::StorageRecord::kImage &&
               (record.image().format() ==
                    thalamus_grpc::Image::Format::Image_Format_MPEG1 ||
                record.image().format() ==
                    thalamus_grpc::Image::Format::Image_Format_MPEG4)) {
      auto &decoder = video_decoders[record.node()];
      auto pulled = decoder->pull();
      while (!pulled) {
        auto new_record = read_record_from_stream();
        pulled = decoder->pull();
        if (!new_record) {
          break;
        }
        record_buffer.push_back(*new_record);
      }
      if (!pulled) {
        return std::nullopt;
      }
      return pulled;
    } else {
      return record;
    }
  }

  std::optional<thalamus_grpc::StorageRecord> read_record() {
    if (!record_buffer.empty()) {
      thalamus_grpc::StorageRecord result = std::move(record_buffer.front());
      record_buffer.pop_front();
      return process_record(result);
    }

    auto record = read_record_from_stream();
    if (!record) {
      return std::nullopt;
    }
    return process_record(*record);
  }
};

RecordReader::RecordReader(std::istream &_stream, bool _do_decode_video)
 : impl(new Impl(_stream, _do_decode_video)) {}
RecordReader::~RecordReader() {}
std::optional<thalamus_grpc::StorageRecord> RecordReader::read_record() {
  return impl->read_record();
}
double RecordReader::progress() {
  return impl->progress;
}
