#include <chrono>
#include <fstream>
#include <hydrate.hpp>
#include <iostream>
#include <optional>
#include <cstdio>
#include <thalamus_config.h>
#ifdef _WIN32
#include <WinSock2.h>
#endif
#include <base_node.hpp>
#include <h5handle.hpp>
#include <xsens_node.hpp>

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

#include <absl/strings/str_replace.h>
#include <boost/dll.hpp>
#include <boost/endian.hpp>
#include <boost/process.hpp>
#include <boost/program_options.hpp>
#include <boost/qvm/quat.hpp>
#include <boost/qvm/vec.hpp>
#include <hdf5.h>
#include <thalamus.pb.h>

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

using namespace std::chrono_literals;

namespace hydrate {
static char hydrate_av_error[AV_ERROR_MAX_STRING_SIZE];
struct RecordReader {
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

  RecordReader(std::istream &_stream, bool _do_decode_video = true)
      : stream(_stream), do_decode_video(_do_decode_video) {
    std::sort(framerates.begin(), framerates.end(),
              [](auto &lhs, auto &rhs) { return lhs.first < rhs.first; });
  }
  ~RecordReader() {
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
using H5Handle = thalamus::H5Handle;

using vecf3 = boost::qvm::vec<float, 3>;

struct Segment {
  unsigned int frame;
  unsigned int segment_id;
  unsigned int time;
  boost::qvm::vec<float, 3> position;
  boost::qvm::quat<float> rotation;
  const char *pose;
  unsigned char actor;
};

H5Handle createH5Segment(size_t pose_length = 0);
H5Handle createH5Segment(size_t pose_length) {
  H5Handle position_type =
      H5Tcreate(H5T_COMPOUND, sizeof(boost::qvm::vec<float, 3>));
  THALAMUS_ASSERT(position_type, "H5Tcreate failed");
  auto h5_status =
      H5Tinsert(position_type, "x", HOFFSET(vecf3, a[0]), H5T_NATIVE_FLOAT);
  THALAMUS_ASSERT(h5_status >= 0,
                  "Failed to create boost::qvm::vec<float, 3>.x");
  h5_status =
      H5Tinsert(position_type, "y", HOFFSET(vecf3, a[1]), H5T_NATIVE_FLOAT);
  THALAMUS_ASSERT(h5_status >= 0,
                  "Failed to create boost::qvm::vec<float, 3>.y");
  h5_status =
      H5Tinsert(position_type, "z", HOFFSET(vecf3, a[2]), H5T_NATIVE_FLOAT);
  THALAMUS_ASSERT(h5_status >= 0,
                  "Failed to create boost::qvm::vec<float, 3>.z");

  H5Handle rotation_type =
      H5Tcreate(H5T_COMPOUND, sizeof(boost::qvm::quat<float>));
  THALAMUS_ASSERT(rotation_type, "H5Tcreate failed");
  h5_status =
      H5Tinsert(rotation_type, "q0", HOFFSET(boost::qvm::quat<float>, a[0]),
                H5T_NATIVE_FLOAT);
  THALAMUS_ASSERT(h5_status >= 0, "Failed to create boost::qvm::quat<float>.s");
  h5_status =
      H5Tinsert(rotation_type, "q1", HOFFSET(boost::qvm::quat<float>, a[1]),
                H5T_NATIVE_FLOAT);
  THALAMUS_ASSERT(h5_status >= 0, "Failed to create boost::qvm::quat<float>.x");
  h5_status =
      H5Tinsert(rotation_type, "q2", HOFFSET(boost::qvm::quat<float>, a[2]),
                H5T_NATIVE_FLOAT);
  THALAMUS_ASSERT(h5_status >= 0, "Failed to create boost::qvm::quat<float>.y");
  h5_status =
      H5Tinsert(rotation_type, "q3", HOFFSET(boost::qvm::quat<float>, a[3]),
                H5T_NATIVE_FLOAT);
  THALAMUS_ASSERT(h5_status >= 0, "Failed to create boost::qvm::quat<float>.z");

  H5Handle str_type = H5Tcopy(H5T_C_S1);
  h5_status = H5Tset_size(str_type, pose_length ? pose_length : H5T_VARIABLE);
  THALAMUS_ASSERT(h5_status >= 0, "H5Tset_size failed");
  h5_status = H5Tset_strpad(str_type, H5T_STR_NULLTERM);
  THALAMUS_ASSERT(h5_status >= 0, "H5Tset_strpad failed");
  h5_status = H5Tset_cset(str_type, H5T_CSET_UTF8);
  THALAMUS_ASSERT(h5_status >= 0, "H5Tset_cset failed");

  H5Handle segment_type = H5Tcreate(H5T_COMPOUND, sizeof(Segment));
  THALAMUS_ASSERT(segment_type, "H5Tcreate failed");
  h5_status = H5Tinsert(segment_type, "time", HOFFSET(Segment, time),
                        H5T_NATIVE_UINT32);
  THALAMUS_ASSERT(h5_status >= 0, "Failed to create Segment.time");
  h5_status = H5Tinsert(segment_type, "frame", HOFFSET(Segment, frame),
                        H5T_NATIVE_UINT32);
  THALAMUS_ASSERT(h5_status >= 0, "Failed to create Segment.frame");
  h5_status = H5Tinsert(segment_type, "segment_id",
                        HOFFSET(Segment, segment_id), H5T_NATIVE_UINT32);
  THALAMUS_ASSERT(h5_status >= 0, "Failed to create Segment.segment_id");
  h5_status = H5Tinsert(segment_type, "position", HOFFSET(Segment, position),
                        position_type);
  THALAMUS_ASSERT(h5_status >= 0, "Failed to create Segment.position");
  h5_status = H5Tinsert(segment_type, "rotation", HOFFSET(Segment, rotation),
                        rotation_type);
  THALAMUS_ASSERT(h5_status >= 0, "Failed to create Segment.rotation");
  h5_status = H5Tinsert(segment_type, "pose", HOFFSET(Segment, pose), str_type);
  THALAMUS_ASSERT(h5_status >= 0, "Failed to create Segment.pose");
  h5_status = H5Tinsert(segment_type, "actor", HOFFSET(Segment, actor),
                        H5T_NATIVE_UINT8);
  THALAMUS_ASSERT(h5_status >= 0, "Failed to create Segment.frame");

  return segment_type;
}

struct Channel {
  std::string node;
  std::string channel;
  H5Handle data;
  H5Handle received;
  size_t count;
  size_t received_count;
  thalamus_grpc::StorageRecord::BodyCase type;
};

void create_parent_groups(H5Handle root, const std::string &leaf);
void create_parent_groups(H5Handle root, const std::string &leaf) {
  std::vector<std::string> path = absl::StrSplit(leaf, "/");
  path.erase(path.end() - 1);
  auto current = root;
  for (auto &token : path) {
    auto exists = H5Lexists(current, token.c_str(), H5P_DEFAULT);
    if (exists) {
      current = H5Gopen2(current, token.c_str(), H5P_DEFAULT);
    } else {
      current = H5Gcreate2(current, token.c_str(), H5P_DEFAULT, H5P_DEFAULT,
                           H5P_DEFAULT);
    }
  }
}

struct Event {
  size_t time;
  hvl_t payload;
};

struct Text {
  size_t time;
  char *text;
};

struct RecapState {
  std::vector<std::string> path;
};

struct DataCount {
  std::map<std::string, size_t> counts;
  std::map<std::string, std::tuple<size_t, size_t, size_t>> dimensions;
  std::map<std::string, hid_t> datatypes;
  size_t max_pose_length = 0;
};

DataCount count_data(const std::string &filename,
                     const std::optional<std::string> slash_replace);
DataCount count_data(const std::string &filename,
                     const std::optional<std::string> slash_replace) {
  std::optional<thalamus_grpc::StorageRecord> record;
  std::ifstream input_stream(filename, std::ios::binary);
  DataCount result;
  std::map<std::string, size_t> &counts = result.counts;
  auto last_time = std::chrono::steady_clock::now();
  RecordReader reader(input_stream);

  while ((record = reader.read_record())) {
    auto now = std::chrono::steady_clock::now();
    if (now - last_time >= 5s) {
      std::cout << reader.progress << "%" << std::endl;
      last_time = now;
    }
    auto node_name = record->node();
    if (slash_replace) {
      node_name = absl::StrReplaceAll(node_name, {{"/", *slash_replace}});
    }
    switch (record->body_case()) {
    case thalamus_grpc::StorageRecord::kAnalog: {
      auto analog = record->analog();
      auto spans = analog.spans();
      for (auto &span : spans) {
        hsize_t span_size = span.end() - span.begin();
        auto span_name = span.name().empty() ? "" : span.name();
        if (slash_replace) {
          span_name = absl::StrReplaceAll(span_name, {{"/", *slash_replace}});
        }
        counts["analog/" + node_name + "/" + span_name + "/data"] += span_size;
        ++counts["analog/" + node_name + "/" + span_name + "/received"];
        if (analog.is_int_data()) {
          result.datatypes["analog/" + node_name + "/" + span_name + "/data"] =
              H5T_NATIVE_SHORT;
        } else {
          result.datatypes["analog/" + node_name + "/" + span_name + "/data"] =
              H5T_NATIVE_DOUBLE;
        }
      }
    } break;
    case thalamus_grpc::StorageRecord::kXsens: {
      auto xsens = record->xsens();
      result.max_pose_length =
          std::max(result.max_pose_length, xsens.pose_name().size());
      auto key = std::pair<std::string, std::string>(node_name, "");
      counts["xsens/" + key.first + "/data"] +=
          uint64_t(xsens.segments().size());
      ++counts["xsens/" + key.first + "/received"];
    } break;
    case thalamus_grpc::StorageRecord::kText: {
      auto text = record->text();
      auto key = std::pair<std::string, std::string>(node_name, "");
      if (key.first.empty()) {
        ++counts["log/data"];
        ++counts["log/received"];
      } else {
        ++counts["text/" + key.first + "/data"];
        ++counts["text/" + key.first + "/received"];
      }
    } break;
    case thalamus_grpc::StorageRecord::kImage: {
      auto image = record->image();
      auto key = std::pair<std::string, std::string>(node_name, "");
      switch (image.format()) {
      case thalamus_grpc::Image::Format::Image_Format_Gray:
        ++counts["image/" + key.first + "/data"];
        ++counts["image/" + key.first + "/received"];
        result.dimensions["image/" + key.first + "/data"] =
            std::make_tuple(image.width(), image.height(), 0);
        result.datatypes["image/" + key.first + "/data"] = H5T_NATIVE_UCHAR;
        break;
      case thalamus_grpc::Image::Format::Image_Format_RGB:
        ++counts["image/" + key.first + "/data"];
        ++counts["image/" + key.first + "/received"];
        result.dimensions["image/" + key.first + "/data"] =
            std::make_tuple(image.width(), image.height(), 3);
        result.datatypes["image/" + key.first + "/data"] = H5T_NATIVE_UCHAR;
        break;
      case thalamus_grpc::Image::Format::Image_Format_YUYV422:
        ++counts["image/" + key.first + "/data"];
        ++counts["image/" + key.first + "/received"];
        result.dimensions["image/" + key.first + "/data"] =
            std::make_tuple(2 * image.width(), image.height(), 0);
        result.datatypes["image/" + key.first + "/data"] = H5T_NATIVE_UCHAR;
        break;
      case thalamus_grpc::Image::Format::Image_Format_YUV420P:
      case thalamus_grpc::Image::Format::Image_Format_YUVJ420P:
        ++counts["image/" + key.first + "/y"];
        ++counts["image/" + key.first + "/u"];
        ++counts["image/" + key.first + "/v"];
        ++counts["image/" + key.first + "/received"];
        result.dimensions["image/" + key.first + "/y"] =
            std::make_tuple(image.width(), image.height(), 0);
        result.datatypes["image/" + key.first + "/y"] = H5T_NATIVE_UCHAR;
        result.dimensions["image/" + key.first + "/u"] =
            std::make_tuple(image.width() / 2, image.height() / 2, 0);
        result.datatypes["image/" + key.first + "/u"] = H5T_NATIVE_UCHAR;
        result.dimensions["image/" + key.first + "/v"] =
            std::make_tuple(image.width() / 2, image.height() / 2, 0);
        result.datatypes["image/" + key.first + "/v"] = H5T_NATIVE_UCHAR;
        break;
      case thalamus_grpc::Image::Format::Image_Format_Gray16:
        ++counts["image/" + key.first + "/data"];
        ++counts["image/" + key.first + "/received"];
        result.dimensions["image/" + key.first + "/data"] =
            std::make_tuple(image.width(), image.height(), 0);
        result.datatypes["image/" + key.first + "/data"] = H5T_NATIVE_USHORT;
        break;
      case thalamus_grpc::Image::Format::Image_Format_RGB16:
        ++counts["image/" + key.first + "/data"];
        ++counts["image/" + key.first + "/received"];
        result.dimensions["image/" + key.first + "/data"] =
            std::make_tuple(image.width(), image.height(), 3);
        result.datatypes["image/" + key.first + "/data"] = H5T_NATIVE_USHORT;
        break;
      case thalamus_grpc::Image::Format::Image_Format_MPEG1:
      case thalamus_grpc::Image::Format::Image_Format_MPEG4:
      case thalamus_grpc::Image::Format::
          Image_Format_Image_Format_INT_MIN_SENTINEL_DO_NOT_USE_:
      case thalamus_grpc::Image::Format::
          Image_Format_Image_Format_INT_MAX_SENTINEL_DO_NOT_USE_:
        THALAMUS_ASSERT(false, "Unexpected image format %d", image.format());
      }
    } break;
    case thalamus_grpc::StorageRecord::kEvent: {
      auto event = record->event();
      auto key = std::pair<std::string, std::string>("events", "");
      ++counts[key.first + "/data"];
      ++counts[key.first + "/received"];
    } break;
    case thalamus_grpc::StorageRecord::BODY_NOT_SET:
    case thalamus_grpc::StorageRecord::kCompressed:
      break;
      // std::cout << "Unhandled record type " << record->body_case() <<
      // std::endl;
    }
  }
  return result;
}

template <typename BUFFER_TYPE, typename CACHE_TYPE>
void write_data(size_t time, size_t remote_time, size_t length, hid_t data,
                hid_t received, size_t &data_written, size_t &received_written,
                hid_t h5_type, const BUFFER_TYPE *data_buffer,
                std::vector<CACHE_TYPE> &data_cache,
                std::vector<size_t> &received_cache, size_t data_chunk,
                size_t received_chunk, std::vector<hsize_t> dims = {},
                bool update_received = true) {
  herr_t error;
  {
    auto &cache = data_cache;
    cache.insert(cache.end(), data_buffer, data_buffer + length);
    if (cache.size() >= data_chunk) {
      std::vector<hsize_t> hlength(1, data_chunk);
      hlength.insert(hlength.end(), dims.begin(), dims.end());
      for (auto i : dims) {
        hlength[0] /= i;
      }

      H5Handle mem_space =
          H5Screate_simple(int(hlength.size()), hlength.data(), nullptr);
      THALAMUS_ASSERT(mem_space, "H5Screate_simple failed");

      H5Handle file_space = H5Dget_space(data);
      THALAMUS_ASSERT(file_space, "H5Dget_space failed");

      std::vector<hsize_t> start(hlength.size(), 0);
      start[0] = data_written;
      for (auto i : dims) {
        start[0] /= i;
      }

      error = H5Sselect_hyperslab(file_space, H5S_SELECT_SET, start.data(),
                                  nullptr, hlength.data(), nullptr);
      THALAMUS_ASSERT(error >= 0, "H5Sselect_hyperslab failed");

      error = H5Dwrite(data, h5_type, mem_space, file_space, H5P_DEFAULT,
                       cache.data());
      THALAMUS_ASSERT(error >= 0, "H5Dwrite failed");
      data_written += data_chunk;

      if constexpr (std::is_pointer<BUFFER_TYPE>::value) {
        std::for_each(cache.begin(), cache.begin() + int64_t(data_chunk),
                      [](auto arg) { delete[] arg; });
      }
      cache.erase(cache.begin(), cache.begin() + int64_t(data_chunk));
    }
  }
  if (update_received) {
    auto &cache = received_cache;
    cache.push_back(time);
    cache.push_back(data_written + data_cache.size());
    for (auto i : dims) {
      cache.back() /= i;
    }
    cache.push_back(remote_time);
    if (cache.size() >= 3 * received_chunk) {
      hsize_t one_row[] = {received_chunk, 3};
      H5Handle mem_space = H5Screate_simple(2, one_row, nullptr);
      THALAMUS_ASSERT(mem_space, "H5Screate_simple failed");

      H5Handle file_space = H5Dget_space(received);
      THALAMUS_ASSERT(file_space, "H5Dget_space failed");
      hsize_t start[] = {received_written, 0};
      error = H5Sselect_hyperslab(file_space, H5S_SELECT_SET, start, nullptr,
                                  one_row, nullptr);
      THALAMUS_ASSERT(error >= 0, "H5Sselect_hyperslab failed");

      error = H5Dwrite(received, H5T_NATIVE_UINT64, mem_space, file_space,
                       H5P_DEFAULT, cache.data());
      THALAMUS_ASSERT(error >= 0, "H5Dwrite failed");
      received_written += received_chunk;

      cache.erase(cache.begin(), cache.begin() + int64_t(3 * received_chunk));
    }
  }
}

int generate_video(boost::program_options::variables_map &vm);
int generate_video(boost::program_options::variables_map &vm) {
  auto video =
      vm.contains("video") ? vm["video"].as<std::string>() : std::string();
  std::string input = vm["input"].as<std::string>();

  std::string output;
  if (vm.count("output")) {
    output = vm["output"].as<std::string>();
  } else {
    output = input + "_" + video + ".mkv";
  }

  unsigned int width = 0;
  unsigned int height = 0;
  std::string pixel_format;
  std::string video_format;
  std::set<size_t> times;
  {
    std::ifstream input_stream(input, std::ios::binary);
    RecordReader reader(input_stream, false);
    std::optional<thalamus_grpc::StorageRecord> record;
    while ((record = reader.read_record())) {
      if (record->body_case() == thalamus_grpc::StorageRecord::kImage &&
          record->node() == video) {
        times.insert(record->time());
        if (width) {
          continue;
        }
        auto image = record->image();
        width = image.width();
        height = image.height();
        switch (image.format()) {
        case thalamus_grpc::Image::Format::Image_Format_Gray:
          pixel_format = "gray";
          break;
        case thalamus_grpc::Image::Format::Image_Format_RGB:
          pixel_format = "rgb24";
          break;
        case thalamus_grpc::Image::Format::Image_Format_YUYV422:
          pixel_format = "yuyv422";
          break;
        case thalamus_grpc::Image::Format::Image_Format_YUV420P:
          pixel_format = "yuv420p";
          break;
        case thalamus_grpc::Image::Format::Image_Format_YUVJ420P:
          pixel_format = "yuvj420p";
          break;
        case thalamus_grpc::Image::Format::Image_Format_Gray16:
          pixel_format = image.bigendian() ? "gray16be" : "gray16le";
          break;
        case thalamus_grpc::Image::Format::Image_Format_RGB16:
          pixel_format = image.bigendian() ? "rgb48be" : "rgb48le";
          break;
        case thalamus_grpc::Image::Format::Image_Format_MPEG1:
          video_format = "mpeg1video";
          break;
        case thalamus_grpc::Image::Format::Image_Format_MPEG4:
          video_format = "mpeg4video";
          break;
        case thalamus_grpc::Image::Format::
            Image_Format_Image_Format_INT_MIN_SENTINEL_DO_NOT_USE_:
        case thalamus_grpc::Image::Format::
            Image_Format_Image_Format_INT_MAX_SENTINEL_DO_NOT_USE_:
          THALAMUS_ASSERT(false, "Unexpected image format %d", image.format());
        }
      }
    }
  }
  THALAMUS_ASSERT(width > 0 && height > 0, "Failed to detect video dimensions");
  size_t total_diffs = 0;
  size_t last_time = 0;
  for (auto time : times) {
    if (last_time > 0) {
      total_diffs += time - last_time;
    }
    last_time = time;
  }
  auto average_interval = total_diffs / (times.size() - 1);

  std::vector<std::pair<double, std::string>> framerates = {
      {24000.0 / 1001, "24000/1001"},
      {24, "24"},
      {25, "25"},
      {30000.0 / 1001, "30000/1001"},
      {30, "30"},
      {50, "50"},
      {60000.0 / 1001, "60000/1001"},
      {60, "60"},
      {15, "15"},
      {5, "5"},
      {10, "10"},
      {12, "12"},
      {15, "15"}};
  std::sort(framerates.begin(), framerates.end());
  auto framerate = 1e9 / double(average_interval);
  auto framerate_i =
      std::lower_bound(framerates.begin(), framerates.end(),
                       std::make_pair(framerate, std::string("")));
  std::string ffmpeg_framerate;
  if (framerate_i == framerates.begin()) {
    ffmpeg_framerate = framerates.front().second;
  } else if (framerate_i == framerates.end()) {
    ffmpeg_framerate = framerates.back().second;
  } else {
    if (framerate - (framerate_i - 1)->first < framerate_i->first - framerate) {
      ffmpeg_framerate = (framerate_i - 1)->second;
    } else {
      ffmpeg_framerate = framerate_i->second;
    }
  }

  AVRational time_base;
  std::vector<std::string> tokens = absl::StrSplit(ffmpeg_framerate, '/');
  if (tokens.size() == 1) {
    tokens.push_back("1");
  }
  auto success = absl::SimpleAtoi(tokens[0], &time_base.den);
  THALAMUS_ASSERT(success, "SimpleAtoi failed %s", tokens[0]);
  success = absl::SimpleAtoi(tokens[1], &time_base.num);
  THALAMUS_ASSERT(success, "SimpleAtoi failed %s", tokens[1]);

  auto location = boost::dll::program_location();
  std::string command;
  if (!video_format.empty()) {
    command = absl::StrFormat("%s ffmpeg -y -i pipe: -c:v copy \"%s\"",
                              location.string(), output);
  } else {
    command = absl::StrFormat(
        "%s ffmpeg -y -f rawvideo -pixel_format %s -video_size %dx%d -i pipe: "
        "-codec mpeg1video -f matroska -qscale:v 2 -b:v 100M -r %s \"%s\"",
        location.string(), pixel_format, width, height, ffmpeg_framerate,
        output);
  }
  std::cout << "command " << command;
  boost::process::opstream in;
  boost::process::child ffmpeg(
      command, boost::process::std_in<in, boost::process::std_out> stdout,
      boost::process::std_err > stderr);
  {
    std::ifstream input_stream(input, std::ios::binary);
    std::optional<thalamus_grpc::StorageRecord> record;
    RecordReader reader(input_stream, false);
    while ((record = reader.read_record())) {
      if (record->body_case() == thalamus_grpc::StorageRecord::kImage &&
          record->node() == video) {
        auto image = record->image();
        width = image.width();
        height = image.height();
        switch (image.format()) {
        case thalamus_grpc::Image::Format::Image_Format_Gray: {
          auto data = image.data(0);
          auto image_width = image.width();
          auto image_height = image.height();
          auto linesize = data.size() / image_height;
          auto char_ptr = reinterpret_cast<const char *>(data.data());
          for (auto i = 0u; i < image_height; ++i) {
            in.write(char_ptr + i * linesize, image_width);
          }
          break;
        }
        case thalamus_grpc::Image::Format::Image_Format_RGB: {
          auto data = image.data(0);
          auto image_width = image.width();
          auto image_height = image.height();
          auto linesize = data.size() / image_height;
          auto char_ptr = reinterpret_cast<const char *>(data.data());
          for (auto i = 0u; i < image_height; ++i) {
            in.write(char_ptr + i * linesize, 3 * image_width);
          }
          break;
        }
        case thalamus_grpc::Image::Format::Image_Format_YUYV422: {
          auto data = image.data(0);
          auto image_width = image.width();
          auto image_height = image.height();
          auto linesize = data.size() / image_height;
          auto char_ptr = reinterpret_cast<const char *>(data.data());
          for (auto i = 0u; i < image_height; ++i) {
            in.write(char_ptr + i * linesize, 2 * image_width);
          }
          break;
        }
        case thalamus_grpc::Image::Format::Image_Format_YUV420P: {
          for (auto i = 0; i < 3; ++i) {
            auto data = image.data(i);
            auto image_width = image.width();
            auto image_height = image.height();
            if (i) {
              image_width /= 2;
              image_height /= 2;
            }
            auto linesize = data.size() / image_height;
            auto char_ptr = reinterpret_cast<const char *>(data.data());
            for (auto j = 0u; j < image_height; ++j) {
              in.write(char_ptr + j * linesize, image_width);
            }
          }
          break;
        }
        case thalamus_grpc::Image::Format::Image_Format_YUVJ420P: {
          for (auto i = 0; i < 3; ++i) {
            auto data = image.data(i);
            auto image_width = image.width();
            auto image_height = image.height();
            if (i) {
              image_width /= 2;
              image_height /= 2;
            }
            auto linesize = data.size() / image_height;
            auto char_ptr = reinterpret_cast<const char *>(data.data());
            for (auto j = 0u; j < image_height; ++j) {
              in.write(char_ptr + j * linesize, image_width);
            }
          }
          break;
        }
        case thalamus_grpc::Image::Format::Image_Format_Gray16: {
          auto data = image.data(0);
          auto image_width = image.width();
          auto image_height = image.height();
          auto linesize = data.size() / image_height;
          auto char_ptr = reinterpret_cast<const char *>(data.data());
          for (auto i = 0u; i < image_height; ++i) {
            in.write(char_ptr + i * linesize, 2 * image_width);
          }
          break;
        }
        case thalamus_grpc::Image::Format::Image_Format_RGB16: {
          auto data = image.data(0);
          auto image_width = image.width();
          auto image_height = image.height();
          auto linesize = data.size() / image_height;
          auto char_ptr = reinterpret_cast<const char *>(data.data());
          for (auto i = 0u; i < image_height; ++i) {
            in.write(char_ptr + i * linesize, 6 * image_width);
          }
          break;
        }
        case thalamus_grpc::Image::Format::Image_Format_MPEG1:
        case thalamus_grpc::Image::Format::Image_Format_MPEG4:
          in.write(image.data(0).data(), int64_t(image.data(0).size()));
          break;
        case thalamus_grpc::Image::Format::
            Image_Format_Image_Format_INT_MIN_SENTINEL_DO_NOT_USE_:
        case thalamus_grpc::Image::Format::
            Image_Format_Image_Format_INT_MAX_SENTINEL_DO_NOT_USE_:
          THALAMUS_ASSERT(false, "Unexpected image format %d", image.format());
        }
      }
    }
  }

  in.flush();
  in.pipe().close();
  ffmpeg.join();
  return 0;
}

int generate_csv(boost::program_options::variables_map &vm);
int generate_csv(boost::program_options::variables_map &vm) {
  auto csv = vm.contains("csv") ? vm["csv"].as<std::string>() : std::string();
  std::string input = vm["input"].as<std::string>();

  std::set<std::string> channels;
  if (vm.contains("channels")) {
    auto text = vm["channels"].as<std::string>();
    auto tokens = absl::StrSplit(text, ',');
    channels.insert(tokens.begin(), tokens.end());
  }

  std::string output;
  if (vm.count("output")) {
    output = vm["output"].as<std::string>();
  } else {
    output = input + "_" + csv + ".csv";
  }

  std::map<std::string, FILE*> column_files;
  std::ifstream input_stream(input, std::ios::binary);
  RecordReader reader(input_stream);
  std::optional<thalamus_grpc::StorageRecord> record;
  auto last_time = std::chrono::steady_clock::now();
  auto line_count = 0l;

  std::cout << "Extracting Channel CSVs" << std::endl;
  while ((record = reader.read_record())) {
    auto now = std::chrono::steady_clock::now();
    if (now - last_time >= 5s) {
      std::cout << reader.progress << "%" << std::endl;
      last_time = now;
    }

    auto node_name = record->node();
    if (node_name != csv) {
      continue;
    }
    switch (record->body_case()) {
    case thalamus_grpc::StorageRecord::kAnalog: {
      auto analog = record->analog();
      auto spans = analog.spans();

      for (auto &span : spans) {
        auto span_name = span.name().empty() ? "" : span.name();
        if (!channels.empty() && !channels.contains(span_name)) {
          continue;
        }
        if (!column_files.contains(span_name)) {
          column_files[span_name] = std::tmpfile();
          fprintf(column_files[span_name], "Time (ns),%s,\n", span_name.c_str());
          ++line_count;
        }

        uint64_t record_time = record->time();
        if (analog.is_int_data()) {
          for (auto i = span.begin(); i < span.end(); ++i) {
            fprintf(column_files[span_name], "%llu,%d,\n", record_time, analog.int_data(int(i)));
          }
        } else {
          for (auto i = span.begin(); i < span.end(); ++i) {
            fprintf(column_files[span_name], "%llu,%f,\n", record_time, analog.data(int(i)));
          }
        }
        line_count += span.end() - span.begin();
      }
      break;
    }
    case thalamus_grpc::StorageRecord::kXsens:
    case thalamus_grpc::StorageRecord::kEvent:
    case thalamus_grpc::StorageRecord::kImage:
    case thalamus_grpc::StorageRecord::kText:
    case thalamus_grpc::StorageRecord::kCompressed:
    case thalamus_grpc::StorageRecord::BODY_NOT_SET:
      break;
    }
  }

  for (auto &pair : column_files) {
    fseek(pair.second, 0, SEEK_SET);
  }

  std::cout << "Merging Channel CSVs" << std::endl;
  std::ofstream total_output = std::ofstream(output);
  char buffer[1024];
  auto working = true;
  auto merged_lines = 0;

  last_time = std::chrono::steady_clock::now();
  while (working) {
    auto now = std::chrono::steady_clock::now();
    if (now - last_time >= 5s) {
      std::cout << (100.0 * double(merged_lines) / double(line_count)) << "%" << std::endl;
      last_time = now;
    }

    working = false;
    for (auto &pair : column_files) {
      if (feof(pair.second)) {
        total_output << ",,";
      } else {
        fgets(buffer, sizeof(buffer), pair.second);
        auto std_line = absl::StripAsciiWhitespace(buffer);
        if (std_line.empty()) {
          total_output << ",,";
          continue;
        }
        total_output << std_line;
        working = true;
        ++merged_lines;
      }
    }
    total_output << std::endl;
  }
  return 0;
}

int main(int argc, char **argv) {
  boost::program_options::positional_options_description p;
  p.add("input", 1);

  boost::program_options::options_description desc(
      "Thalamus capture file parsing, version " GIT_COMMIT_HASH);
  desc.add_options()("help,h", "produce help message")(
      "trace,t", "Enable tracing")("stats,s", "Just print stats")(
      "gzip,g", boost::program_options::value<size_t>(),
      "GZIP compression level (default is no compression)")(
      "chunk,c", boost::program_options::value<size_t>(),
      "Chunk size")("repeat,r", boost::program_options::value<size_t>(),
                    "Run indefinitely, executing every N ms")(
      "input,i", boost::program_options::value<std::string>(),
      "Input file")("csv", boost::program_options::value<std::string>(),
                    "Output this node's data as csv")(
      "video", boost::program_options::value<std::string>(),
      "Output this node's data as video")(
      "channels", boost::program_options::value<std::string>(),
      "Channels to output")("slash-replace",
                            boost::program_options::value<std::string>(),
                            "Text to replace slashes with")(
      "output,o", boost::program_options::value<std::string>(), "Output file");

  boost::program_options::variables_map vm;
  try {
    boost::program_options::store(
        boost::program_options::command_line_parser(argc, argv)
            .options(desc)
            .positional(p)
            .run(),
        vm);
  } catch (std::exception &e) {
    std::cout << e.what() << std::endl;
    return 1;
  }
  boost::program_options::notify(vm);

  if (vm.count("help")) {
    std::cout << desc << std::endl;
    return 0;
  }

  auto gzip = vm.contains("gzip") ? vm["gzip"].as<size_t>() : 0;
  std::optional<std::string> slash_replace;
  if (vm.contains("slash-replace")) {
    slash_replace = vm["slash-replace"].as<std::string>();
  }
  auto video =
      vm.contains("video") ? vm["video"].as<std::string>() : std::string();
  auto csv = vm.contains("csv") ? vm["csv"].as<std::string>() : std::string();
  auto chunk_size = vm.contains("chunk") ? vm["chunk"].as<size_t>()
                                         : std::numeric_limits<size_t>::max();

  if (!vm.count("input")) {
    std::cout << desc << std::endl;
    return 1;
  }

  std::chrono::milliseconds interval = 0ms;
  if (vm.count("repeat")) {
    interval = std::chrono::milliseconds(vm["repeat"].as<size_t>());
  }

  auto just_stats = vm.contains("stats");

  std::string input = vm["input"].as<std::string>();
  std::string output;

  if (!csv.empty()) {
    return generate_csv(vm);
  } else if (!video.empty()) {
    return generate_video(vm);
  }

  if (vm.count("output")) {
    output = vm["output"].as<std::string>();
  } else {
    output = input + ".h5";
  }

  bool running = true;
  while (running) {
    auto start = std::chrono::steady_clock::now();

    std::cout << "Measuring Capture File" << std::endl;
    DataCount data_count = count_data(input, slash_replace);
    auto &dataset_counts = data_count.counts;
    std::map<std::string, H5Handle> datasets;
    std::map<std::string, size_t> written;
    for (const auto &pair : dataset_counts) {
      std::vector<std::string> tokens = absl::StrSplit(pair.first, '/');
      THALAMUS_ASSERT(tokens.size() > 0, "StrSplit failed");
      if (tokens.back() == "received") {
        continue;
      }
      std::cout << pair.first << " " << pair.second << std::endl;
    }
    if (just_stats) {
      auto duration = std::chrono::steady_clock::now() - start;
      std::cout << "Duration: "
                << std::chrono::duration_cast<std::chrono::milliseconds>(
                       duration)
                       .count()
                << " ms" << std::endl;
      if (interval != 0ms) {
        running = true;
        if (interval > duration) {
          std::this_thread::sleep_for(interval - duration);
        }
      } else {
        running = false;
      }
      continue;
    }

    std::cout << "Writing H5 file: " << output << std::endl;
    H5Handle fid =
        H5Fcreate(output.c_str(), H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
    if (!fid) {
      std::cout << "Failed to open output file" << std::endl;
      return 1;
    }

    H5Handle segment_type = createH5Segment(0);

    H5Handle blob_type = H5Tvlen_create(H5T_NATIVE_UCHAR);
    THALAMUS_ASSERT(blob_type, "H5Tvlen_create(H5T_NATIVE_UCHAR) failed");

    H5Handle event_type = H5Tcreate(H5T_COMPOUND, sizeof(Event));
    THALAMUS_ASSERT(event_type,
                    "H5Tcreate(H5T_COMPOUND, sizeof(Event)) failed");
    auto error =
        H5Tinsert(event_type, "time", HOFFSET(Event, time), H5T_NATIVE_UINT64);
    THALAMUS_ASSERT(error >= 0, "Failed to create Event.time");
    error =
        H5Tinsert(event_type, "payload", HOFFSET(Event, payload), blob_type);
    THALAMUS_ASSERT(error >= 0, "Failed to create Event.payload");

    H5Handle str_type = H5Tcopy(H5T_C_S1);
    error = H5Tset_size(str_type, H5T_VARIABLE);
    THALAMUS_ASSERT(error >= 0, "H5Tset_size failed");
    error = H5Tset_strpad(str_type, H5T_STR_NULLTERM);
    THALAMUS_ASSERT(error >= 0, "H5Tset_strpad failed");
    error = H5Tset_cset(str_type, H5T_CSET_UTF8);
    THALAMUS_ASSERT(error >= 0, "H5Tset_cset failed");

    H5Handle link_plist = H5Pcreate(H5P_LINK_CREATE);
    error = H5Pset_create_intermediate_group(link_plist, 1);
    THALAMUS_ASSERT(error >= 0, "H5Pset_create_intermediate_group failed");

    for (const auto &pair : dataset_counts) {
      std::vector<std::string> tokens = absl::StrSplit(pair.first, '/');
      THALAMUS_ASSERT(tokens.size() > 0, "StrSplit failed");
      if (tokens.back() == "received") {
        hsize_t dims[] = {pair.second, 3};
        hsize_t max_dims[] = {pair.second, 3};
        H5Handle file_space = H5Screate_simple(2, dims, max_dims);
        THALAMUS_ASSERT(file_space, "H5Screate_simple failed");

        H5Handle plist_id = H5P_DEFAULT;
        if (gzip) {
          plist_id = H5Pcreate(H5P_DATASET_CREATE);

          hsize_t chunk[] = {dims[0], 3};
          error = H5Pset_chunk(plist_id, 2, chunk);
          THALAMUS_ASSERT(error >= 0, "H5Pset_chunk failed");

          error = H5Pset_deflate(plist_id, uint32_t(gzip));
          THALAMUS_ASSERT(error >= 0, "H5Pset_deflate failed");
        }

        datasets[pair.first] =
            H5Handle(H5Dcreate(fid, pair.first.c_str(), H5T_NATIVE_UINT64,
                               file_space, link_plist, plist_id, H5P_DEFAULT));
        THALAMUS_ASSERT(datasets[pair.first], "H5Dcreate failed");
      } else {
        hid_t type = H5T_NATIVE_OPAQUE;
        hsize_t dims[] = {pair.second, 0, 0, 0};
        hsize_t max_dims[] = {pair.second, 0, 0, 0};
        int rank = 1;

        if (tokens.front() == "analog") {
          type = data_count.datatypes[pair.first];
        } else if (tokens.front() == "xsens") {
          type = segment_type;
        } else if (tokens.front() == "events") {
          type = event_type;
        } else if (tokens.front() == "text") {
          type = str_type;
        } else if (tokens.front() == "log") {
          type = str_type;
        } else if (tokens.front() == "image") {
          type = data_count.datatypes[pair.first];
          auto image_dims = data_count.dimensions[pair.first];
          if (std::get<0>(image_dims) != 0) {
            dims[1] = std::get<0>(image_dims);
            max_dims[1] = std::get<0>(image_dims);
            ++rank;
          }
          if (std::get<1>(image_dims) != 0) {
            dims[2] = std::get<1>(image_dims);
            max_dims[2] = std::get<1>(image_dims);
            ++rank;
          }
          if (std::get<2>(image_dims) != 0) {
            dims[3] = std::get<2>(image_dims);
            max_dims[3] = std::get<2>(image_dims);
            ++rank;
          }
        }
        H5Handle file_space = H5Screate_simple(rank, dims, max_dims);
        THALAMUS_ASSERT(file_space, "H5Screate_simple failed");

        H5Handle plist_id = H5P_DEFAULT;
        if (gzip && dims[0] > 0) {
          plist_id = H5Pcreate(H5P_DATASET_CREATE);

          std::vector<hsize_t> chunk_dims(std::begin(dims), std::end(dims));
          chunk_dims[0] = std::min(chunk_dims[0], hsize_t(chunk_size));
          error = H5Pset_chunk(plist_id, rank, chunk_dims.data());
          THALAMUS_ASSERT(error >= 0, "H5Pset_chunk failed");

          error = H5Pset_deflate(plist_id, uint32_t(gzip));
          THALAMUS_ASSERT(error >= 0, "H5Pset_deflate failed");
        }

        datasets[pair.first] =
            H5Handle(H5Dcreate(fid, pair.first.c_str(), type, file_space,
                               link_plist, plist_id, H5P_DEFAULT));
        THALAMUS_ASSERT(datasets[pair.first], "H5Dcreate failed");
      }
    }

    std::ifstream input_stream(input, std::ios::binary);
    RecordReader reader(input_stream);

    std::optional<thalamus_grpc::StorageRecord> record;
    auto last_time = std::chrono::steady_clock::now();
    std::map<hid_t, std::vector<double>> data_caches;
    std::map<hid_t, std::vector<short>> int_data_caches;
    std::map<hid_t, std::vector<Segment>> segment_caches;
    std::map<hid_t, std::vector<char *>> text_caches;
    std::map<hid_t, std::vector<unsigned char>> image_caches;
    std::map<hid_t, std::vector<unsigned short>> short_image_caches;
    std::vector<Event> event_cache;
    std::map<hid_t, std::vector<size_t>> received_caches;

    while ((record = reader.read_record())) {
      auto now = std::chrono::steady_clock::now();
      if (now - last_time >= 5s) {
        std::cout << reader.progress << "%" << std::endl;
        last_time = now;
      }
      auto node_name = record->node();
      if (slash_replace) {
        node_name = absl::StrReplaceAll(node_name, {{"/", *slash_replace}});
      }

      switch (record->body_case()) {
      case thalamus_grpc::StorageRecord::kAnalog: {
        auto analog = record->analog();
        auto spans = analog.spans();

        for (auto &span : spans) {
          hsize_t span_size = span.end() - span.begin();
          auto span_name = span.name().empty() ? "" : span.name();
          if (slash_replace) {
            span_name = absl::StrReplaceAll(span_name, {{"/", *slash_replace}});
          }
          auto data_path = "analog/" + node_name + "/" + span_name + "/data";
          auto received_path =
              "analog/" + node_name + "/" + span_name + "/received";
          auto data = datasets[data_path];
          auto received = datasets[received_path];
          auto &data_written = written[data_path];
          auto &received_written = written[received_path];
          auto data_chunk =
              gzip ? std::min(dataset_counts[data_path] - data_written,
                              chunk_size)
                   : span_size;
          auto received_chunk =
              gzip ? std::min(dataset_counts[received_path] - received_written,
                              chunk_size)
                   : 1;
          if (analog.is_int_data()) {
            write_data(
                record->time(), analog.remote_time(), span_size, data, received,
                data_written, received_written, H5T_NATIVE_SHORT,
                analog.int_data().data() + span.begin(), int_data_caches[data],
                received_caches[received], data_chunk, received_chunk);
          } else {
            write_data(record->time(), analog.remote_time(), span_size, data,
                       received, data_written, received_written,
                       H5T_NATIVE_DOUBLE, analog.data().data() + span.begin(),
                       data_caches[data], received_caches[received], data_chunk,
                       received_chunk);
          }
        }
      } break;
      case thalamus_grpc::StorageRecord::kXsens: {
        auto xsens = record->xsens();

        auto data_path = "xsens/" + node_name + "/data";
        auto received_path = "xsens/" + node_name + "/received";
        auto data = datasets[data_path];
        auto received = datasets[received_path];
        auto &data_written = written[data_path];
        auto &received_written = written[received_path];
        auto segment_count =
            gzip
                ? std::min(dataset_counts[data_path] - data_written, chunk_size)
                : size_t(xsens.segments().size());
        auto received_count =
            gzip ? std::min(dataset_counts[received_path] - received_written,
                            chunk_size)
                 : 1;
        std::string pose = xsens.pose_name();
        auto pose_cstr = pose.c_str();

        std::vector<Segment> segments;
        for (auto &s : xsens.segments()) {
          segments.emplace_back();
          auto &segment = segments.back();
          segment.frame = s.frame();
          segment.segment_id = s.id();
          segment.time = s.time();
          segment.actor = uint8_t(s.actor());
          segment.position = boost::qvm::vec<float, 3>{s.x(), s.y(), s.z()};
          segment.rotation =
              boost::qvm::quat<float>{s.q0(), s.q1(), s.q2(), s.q3()};
          segment.pose = pose_cstr;
        }
        auto num_segments = hsize_t(xsens.segments().size());
        write_data(record->time(), 0, num_segments, data, received,
                   data_written, received_written, segment_type,
                   segments.data(), segment_caches[data],
                   received_caches[received], segment_count, received_count);
      } break;
      case thalamus_grpc::StorageRecord::kText: {
        auto text = record->text().text();
        auto text_data = new char[text.size() + 1];
        strcpy(text_data, text.data());

        auto data_path =
            record->node().empty() ? "log/data" : "text/" + node_name + "/data";
        auto received_path = record->node().empty()
                                 ? "log/received"
                                 : "text/" + node_name + "/received";
        auto data = datasets[data_path];
        auto received = datasets[received_path];
        auto &data_written = written[data_path];
        auto &received_written = written[received_path];
        auto text_data_count =
            gzip
                ? std::min(dataset_counts[data_path] - data_written, chunk_size)
                : 1;
        auto received_count =
            gzip ? std::min(dataset_counts[received_path] - received_written,
                            chunk_size)
                 : 1;

        write_data(record->time(), 0, 1, data, received, data_written,
                   received_written, str_type, &text_data, text_caches[data],
                   received_caches[received], text_data_count, received_count);
      } break;
      case thalamus_grpc::StorageRecord::kImage: {
        auto image = record->image();

        const unsigned char *bytes_pointer;
        std::vector<unsigned char> bytes_vector;
        std::vector<unsigned short> shorts_vector;

        switch (image.format()) {
        case thalamus_grpc::Image::Format::Image_Format_Gray: {
          auto data_path = "image/" + node_name + "/data";
          auto received_path = "image/" + node_name + "/received";
          auto data = datasets[data_path];
          auto received = datasets[received_path];
          auto &data_written = written[data_path];
          auto &received_written = written[received_path];
          auto image_data_count =
              gzip ? std::min(dataset_counts[data_path] - data_written,
                              chunk_size)
                   : 1;
          auto received_count =
              gzip ? std::min(dataset_counts[received_path] - received_written,
                              chunk_size)
                   : 1;
          auto length = image.width() * image.height();
          auto linesize = image.data(0).size() / image.height();
          image_data_count *= length;

          bytes_pointer =
              reinterpret_cast<const unsigned char *>(image.data(0).data());
          if (image.width() != linesize) {
            bytes_vector.clear();
            for (auto i = 0u; i < image.height(); ++i) {
              bytes_vector.insert(bytes_vector.end(), bytes_pointer,
                                  bytes_pointer + linesize * i + image.width());
            }
            bytes_pointer = bytes_vector.data();
          }

          write_data(record->time(), 0, length, data, received, data_written,
                     received_written, H5T_NATIVE_UCHAR, bytes_pointer,
                     image_caches[data], received_caches[received],
                     image_data_count, received_count,
                     {image.width(), image.height()});
          break;
        }
        case thalamus_grpc::Image::Format::Image_Format_RGB: {
          auto data_path = "image/" + node_name + "/data";
          auto received_path = "image/" + node_name + "/received";
          auto data = datasets[data_path];
          auto received = datasets[received_path];
          auto &data_written = written[data_path];
          auto &received_written = written[received_path];
          auto image_data_count =
              gzip ? std::min(dataset_counts[data_path] - data_written,
                              chunk_size)
                   : 1;
          auto received_count =
              gzip ? std::min(dataset_counts[received_path] - received_written,
                              chunk_size)
                   : 1;
          auto length = image.width() * image.height() * 3;
          auto linesize = image.data(0).size() / image.height();
          image_data_count *= length;

          bytes_pointer =
              reinterpret_cast<const unsigned char *>(image.data(0).data());
          if (3 * image.width() != linesize) {
            bytes_vector.clear();
            for (auto i = 0u; i < image.height(); ++i) {
              bytes_vector.insert(bytes_vector.end(), bytes_pointer,
                                  bytes_pointer + linesize * i +
                                      3 * image.width());
            }
            bytes_pointer = bytes_vector.data();
          }

          write_data(record->time(), 0, length, data, received, data_written,
                     received_written, H5T_NATIVE_UCHAR, bytes_pointer,
                     image_caches[data], received_caches[received],
                     image_data_count, received_count,
                     {image.width(), image.height(), 3});
          break;
        }
        case thalamus_grpc::Image::Format::Image_Format_YUYV422: {
          auto data_path = "image/" + node_name + "/data";
          auto received_path = "image/" + node_name + "/received";
          auto data = datasets[data_path];
          auto received = datasets[received_path];
          auto &data_written = written[data_path];
          auto &received_written = written[received_path];
          auto image_data_count =
              gzip ? std::min(dataset_counts[data_path] - data_written,
                              chunk_size)
                   : 1;
          auto received_count =
              gzip ? std::min(dataset_counts[received_path] - received_written,
                              chunk_size)
                   : 1;
          auto length = 2 * image.width() * image.height();
          auto linesize = image.data(0).size() / image.height();
          image_data_count *= length;

          bytes_pointer =
              reinterpret_cast<const unsigned char *>(image.data(0).data());
          if (2 * image.width() != linesize) {
            bytes_vector.clear();
            for (auto i = 0u; i < image.height(); ++i) {
              bytes_vector.insert(bytes_vector.end(), bytes_pointer,
                                  bytes_pointer + linesize * i +
                                      2 * image.width());
            }
            bytes_pointer = bytes_vector.data();
          }

          write_data(record->time(), 0, length, data, received, data_written,
                     received_written, H5T_NATIVE_UCHAR, bytes_pointer,
                     image_caches[data], received_caches[received],
                     image_data_count, received_count,
                     {2 * image.width(), image.height()});
          break;
        }
        case thalamus_grpc::Image::Format::Image_Format_YUV420P:
        case thalamus_grpc::Image::Format::Image_Format_YUVJ420P: {
          std::vector<std::string> data_paths = {
              "image/" + node_name + "/y",
              "image/" + node_name + "/u",
              "image/" + node_name + "/v",
          };
          auto update_received = true;
          auto i = 0;
          for (auto &data_path : data_paths) {
            auto received_path = "image/" + node_name + "/received";
            auto data = datasets[data_path];
            auto received = datasets[received_path];
            auto &data_written = written[data_path];
            auto &received_written = written[received_path];
            auto image_data_count =
                gzip ? std::min(dataset_counts[data_path] - data_written,
                                chunk_size)
                     : 1;
            auto received_count =
                gzip
                    ? std::min(dataset_counts[received_path] - received_written,
                               chunk_size)
                    : 1;
            auto width = size_t(image.width());
            auto height = size_t(image.height());
            if (i > 0) {
              width /= 2;
              height /= 2;
            }
            auto length = width * height;
            image_data_count *= length;
            auto linesize = image.data(i).size() / height;

            bytes_pointer =
                reinterpret_cast<const unsigned char *>(image.data(i).data());
            if (width != linesize) {
              bytes_vector.clear();
              for (auto j = 0ull; j < height; ++j) {
                bytes_vector.insert(bytes_vector.end(),
                                    bytes_pointer + linesize * j,
                                    bytes_pointer + linesize * j + width);
              }
              bytes_pointer = bytes_vector.data();
            }

            write_data(record->time(), 0, length, data, received, data_written,
                       received_written, H5T_NATIVE_UCHAR, bytes_pointer,
                       image_caches[data], received_caches[received],
                       image_data_count, received_count, {width, height},
                       update_received);
            update_received = false;
            ++i;
          }
          break;
        }
        case thalamus_grpc::Image::Format::Image_Format_Gray16: {
          auto data_path = "image/" + node_name + "/data";
          auto received_path = "image/" + node_name + "/received";
          auto data = datasets[data_path];
          auto received = datasets[received_path];
          auto &data_written = written[data_path];
          auto &received_written = written[received_path];
          auto image_data_count =
              gzip ? std::min(dataset_counts[data_path] - data_written,
                              chunk_size)
                   : 1;
          auto received_count =
              gzip ? std::min(dataset_counts[received_path] - received_written,
                              chunk_size)
                   : 1;
          auto length = image.width() * image.height();
          image_data_count *= length;

          bytes_pointer =
              reinterpret_cast<const unsigned char *>(image.data(0).data());
          shorts_vector.clear();
          for (auto j = 0u; j < length; ++j) {
            shorts_vector.push_back(uint16_t((bytes_pointer[2 * j] << 8) |
                                             bytes_pointer[2 * j + 1]));
            if (image.bigendian()) {
              boost::endian::big_to_native_inplace(shorts_vector.back());
            } else {
              boost::endian::little_to_native_inplace(shorts_vector.back());
            }
          }

          write_data(record->time(), 0, length, data, received, data_written,
                     received_written, H5T_NATIVE_USHORT, shorts_vector.data(),
                     short_image_caches[data], received_caches[received],
                     image_data_count, received_count,
                     {image.width(), image.height()});
          break;
        }
        case thalamus_grpc::Image::Format::Image_Format_RGB16: {
          auto data_path = "image/" + node_name + "/data";
          auto received_path = "image/" + node_name + "/received";
          auto data = datasets[data_path];
          auto received = datasets[received_path];
          auto &data_written = written[data_path];
          auto &received_written = written[received_path];
          auto image_data_count =
              gzip ? std::min(dataset_counts[data_path] - data_written,
                              chunk_size)
                   : 1;
          auto received_count =
              gzip ? std::min(dataset_counts[received_path] - received_written,
                              chunk_size)
                   : 1;
          auto length = 3 * image.width() * image.height();
          image_data_count *= length;

          bytes_pointer =
              reinterpret_cast<const unsigned char *>(image.data(0).data());
          shorts_vector.clear();
          for (auto j = 0u; j < length; ++j) {
            shorts_vector.push_back(uint16_t((bytes_pointer[2 * j] << 8) |
                                             bytes_pointer[2 * j + 1]));
            if (image.bigendian()) {
              boost::endian::big_to_native_inplace(shorts_vector.back());
            } else {
              boost::endian::little_to_native_inplace(shorts_vector.back());
            }
          }

          write_data(record->time(), 0, length, data, received, data_written,
                     received_written, H5T_NATIVE_USHORT, shorts_vector.data(),
                     short_image_caches[data], received_caches[received],
                     image_data_count, received_count,
                     {image.width(), image.height(), 3});
          break;
        }
        case thalamus_grpc::Image::Format::Image_Format_MPEG1:
        case thalamus_grpc::Image::Format::Image_Format_MPEG4:
        case thalamus_grpc::Image::Format::
            Image_Format_Image_Format_INT_MIN_SENTINEL_DO_NOT_USE_:
        case thalamus_grpc::Image::Format::
            Image_Format_Image_Format_INT_MAX_SENTINEL_DO_NOT_USE_:
          THALAMUS_ASSERT(false, "Unexpected image format %d", image.format());
        }
      } break;
      case thalamus_grpc::StorageRecord::kEvent: {
        auto event = record->event();
        auto data = datasets["events/data"];
        auto received = datasets["events/received"];
        auto &data_written = written["events/data"];
        auto &received_written = written["events/received"];
        auto event_data_count =
            gzip ? std::min(dataset_counts["events/data"] - data_written,
                            chunk_size)
                 : 1;
        auto received_count =
            gzip
                ? std::min(dataset_counts["events/received"] - received_written,
                           chunk_size)
                : 1;

        Event h5_event;
        h5_event.time = event.time();
        h5_event.payload.len = event.payload().size();
        h5_event.payload.p = const_cast<void *>(
            static_cast<const void *>(event.payload().data()));
        write_data(record->time(), 0, 1, data, received, data_written,
                   received_written, event_type, &h5_event, event_cache,
                   received_caches[received], event_data_count, received_count);
      } break;
      case thalamus_grpc::StorageRecord::BODY_NOT_SET:
      case thalamus_grpc::StorageRecord::kCompressed:
        break;
        // std::cout << "Unhandled record type " << record->body_case() <<
        // std::endl;
      }
    }

    auto duration = std::chrono::steady_clock::now() - start;
    std::cout << "Duration: "
              << std::chrono::duration_cast<std::chrono::milliseconds>(duration)
                     .count()
              << " ms" << std::endl;
    if (interval != 0ms) {
      running = true;
      if (interval > duration) {
        std::this_thread::sleep_for(interval - duration);
      }
    } else {
      running = false;
    }
  }
  return 0;
}
} // namespace hydrate
