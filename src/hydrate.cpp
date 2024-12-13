#include <boost/program_options.hpp>
#include <iostream>
#include <fstream>
#include <string>
#include <chrono>
#include <thalamus_config.h>
#include <thalamus.pb.h>
#include <optional>
#ifdef _WIN32
#include <WinSock2.h>
#endif
#include <hdf5.h>
#include <base_node.hpp>
#include <xsens_node.hpp>
#include <h5handle.hpp>
#include <boost/qvm/vec.hpp>
#include <boost/qvm/quat.hpp>
#include <boost/endian.hpp>
#include <boost/dll.hpp>
#include <boost/process.hpp>
#include <absl/strings/str_replace.h>

#ifdef _WIN32
#include <winsock2.h>
#elif __APPLE__
#include <arpa/inet.h>
#else
#include <endian.h>
#define htonll(x) htobe64(x)
#endif

using namespace std::chrono_literals;

namespace hydrate {
  using H5Handle = thalamus::H5Handle;

  using vecf3 = boost::qvm::vec<float, 3>;

  struct Segment {
    unsigned int frame;
    unsigned int segment_id;
    unsigned int time;
    boost::qvm::vec<float, 3> position;
    boost::qvm::quat<float> rotation;
    const char* pose;
    unsigned char actor;
  };

  H5Handle createH5Segment(size_t pose_length = 0) {
    H5Handle position_type = H5Tcreate(H5T_COMPOUND, sizeof(boost::qvm::vec<float, 3>));
    THALAMUS_ASSERT(position_type);
    auto h5_status = H5Tinsert(position_type, "x", HOFFSET(vecf3, a[0]), H5T_NATIVE_FLOAT);
    THALAMUS_ASSERT(h5_status >= 0, "Failed to create boost::qvm::vec<float, 3>.x");
    h5_status = H5Tinsert(position_type, "y", HOFFSET(vecf3, a[1]), H5T_NATIVE_FLOAT);
    THALAMUS_ASSERT(h5_status >= 0, "Failed to create boost::qvm::vec<float, 3>.y");
    h5_status = H5Tinsert(position_type, "z", HOFFSET(vecf3, a[2]), H5T_NATIVE_FLOAT);
    THALAMUS_ASSERT(h5_status >= 0, "Failed to create boost::qvm::vec<float, 3>.z");

    H5Handle rotation_type = H5Tcreate(H5T_COMPOUND, sizeof(boost::qvm::quat<float>));
    THALAMUS_ASSERT(rotation_type);
    h5_status = H5Tinsert(rotation_type, "q0", HOFFSET(boost::qvm::quat<float>, a[0]), H5T_NATIVE_FLOAT);
    THALAMUS_ASSERT(h5_status >= 0, "Failed to create boost::qvm::quat<float>.s");
    h5_status = H5Tinsert(rotation_type, "q1", HOFFSET(boost::qvm::quat<float>, a[1]), H5T_NATIVE_FLOAT);
    THALAMUS_ASSERT(h5_status >= 0, "Failed to create boost::qvm::quat<float>.x");
    h5_status = H5Tinsert(rotation_type, "q2", HOFFSET(boost::qvm::quat<float>, a[2]), H5T_NATIVE_FLOAT);
    THALAMUS_ASSERT(h5_status >= 0, "Failed to create boost::qvm::quat<float>.y");
    h5_status = H5Tinsert(rotation_type, "q3", HOFFSET(boost::qvm::quat<float>, a[3]), H5T_NATIVE_FLOAT);
    THALAMUS_ASSERT(h5_status >= 0, "Failed to create boost::qvm::quat<float>.z");

    H5Handle str_type = H5Tcopy(H5T_C_S1);
    h5_status = H5Tset_size(str_type, pose_length ? pose_length : H5T_VARIABLE);
    THALAMUS_ASSERT(h5_status >= 0);
    h5_status = H5Tset_strpad(str_type, H5T_STR_NULLTERM);
    THALAMUS_ASSERT(h5_status >= 0);
    h5_status = H5Tset_cset(str_type, H5T_CSET_UTF8);
    THALAMUS_ASSERT(h5_status >= 0);

    H5Handle segment_type = H5Tcreate(H5T_COMPOUND, sizeof(Segment));
    THALAMUS_ASSERT(segment_type);
    h5_status = H5Tinsert(segment_type, "time", HOFFSET(Segment, time), H5T_NATIVE_UINT32);
    THALAMUS_ASSERT(h5_status >= 0, "Failed to create Segment.time");
    h5_status = H5Tinsert(segment_type, "frame", HOFFSET(Segment, frame), H5T_NATIVE_UINT32);
    THALAMUS_ASSERT(h5_status >= 0, "Failed to create Segment.frame");
    h5_status = H5Tinsert(segment_type, "segment_id", HOFFSET(Segment, segment_id), H5T_NATIVE_UINT32);
    THALAMUS_ASSERT(h5_status >= 0, "Failed to create Segment.segment_id");
    h5_status = H5Tinsert(segment_type, "position", HOFFSET(Segment, position), position_type);
    THALAMUS_ASSERT(h5_status >= 0, "Failed to create Segment.position");
    h5_status = H5Tinsert(segment_type, "rotation", HOFFSET(Segment, rotation), rotation_type);
    THALAMUS_ASSERT(h5_status >= 0, "Failed to create Segment.rotation");
    h5_status = H5Tinsert(segment_type, "pose", HOFFSET(Segment, pose), str_type);
    THALAMUS_ASSERT(h5_status >= 0, "Failed to create Segment.pose");
    h5_status = H5Tinsert(segment_type, "actor", HOFFSET(Segment, actor), H5T_NATIVE_UINT8);
    THALAMUS_ASSERT(h5_status >= 0, "Failed to create Segment.frame");

    return segment_type;
  }

  std::optional<thalamus_grpc::StorageRecord> read_record(std::ifstream& stream, double* progress = nullptr) {
    auto initial_position = stream.tellg();
    auto current_position = initial_position;
  
    stream.seekg(0, std::ios::end);
    auto file_size = stream.tellg();
    stream.seekg(initial_position);

    if(progress) {
      *progress = 100.0*current_position/file_size;
    }
  
    if (file_size == current_position) {
      //std::cout << "End of file" << std::endl;
      return std::nullopt;
    }
  
    if(file_size - current_position < 8) {
      std::cout << "Not enough bytes to read message size, likely final message was corrupted." << std::endl;
      return std::nullopt;
    }
  
    std::string buffer;
    buffer.resize(8);
    stream.read(buffer.data(), 8);
    size_t size = *reinterpret_cast<size_t*>(buffer.data());
    size = htonll(size);
  
    
    current_position = stream.tellg();
    if(file_size - current_position < size) {
      std::cout << "Not enough bytes to read message, likely final message was corrupted." << std::endl;
      return std::nullopt;
    }
  
    buffer.resize(size);
    stream.read(buffer.data(), size);
  
    thalamus_grpc::StorageRecord record;
    if(record.ParseFromString(buffer)) {
      return record;
    } else {
      std::cout << "Failed to parse message" << std::endl;
      return std::nullopt;
    }
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

  void create_parent_groups(H5Handle root, const std::string& leaf) {
    std::vector<std::string> path = absl::StrSplit(leaf, "/");
    path.erase(path.end()-1);
    auto current = root;
    for(auto& token : path) {
      auto exists = H5Lexists(current, token.c_str(), H5P_DEFAULT);
      if(exists) {
        current = H5Gopen2(current, token.c_str(), H5P_DEFAULT);
      } else {
        current = H5Gcreate2(current, token.c_str(), H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
      }
    }
  }

  struct Event {
    size_t time;
    hvl_t payload;
  };

  struct Text {
    size_t time;
    char* text;
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

  DataCount count_data(const std::string& filename, const std::optional<std::string> slash_replace) {
    std::optional<thalamus_grpc::StorageRecord> record;
    std::ifstream input_stream(filename, std::ios::binary);
    DataCount result;
    std::map<std::string, size_t>& counts = result.counts;
    double progress;
    auto last_time = std::chrono::steady_clock::now();

    while((record = read_record(input_stream, &progress))) {
      auto now = std::chrono::steady_clock::now();
      if(now - last_time >= 5s) {
        std::cout << progress << "%" << std::endl;
        last_time = now;
      }
      auto node_name = record->node();
      if(slash_replace) {
        node_name = absl::StrReplaceAll(node_name, {{"/", *slash_replace}});
      }
      switch(record->body_case()) {
        case thalamus_grpc::StorageRecord::kAnalog:
          {
            auto analog = record->analog();
            auto spans = analog.spans();
            hsize_t num_channels = spans.size();
            for(auto& span : spans) {
              hsize_t span_size = span.end() - span.begin();
              auto span_name = span.name().empty() ? "" : span.name();
              if(slash_replace) {
                span_name = absl::StrReplaceAll(span_name, {{"/", *slash_replace}});
              }
              counts["analog/" + node_name + "/" + span_name + "/data"] += span_size;
              ++counts["analog/" + node_name + "/" + span_name + "/received"];
            }
          }
          break;
        case thalamus_grpc::StorageRecord::kXsens:
          {
            auto xsens = record->xsens();
            result.max_pose_length = std::max(result.max_pose_length, xsens.pose_name().size());
            auto key = std::pair<std::string, std::string>(node_name, "");
            counts["xsens/" + key.first + "/data"] += xsens.segments().size();
            ++counts["xsens/" + key.first + "/received"];
          }
          break;
        case thalamus_grpc::StorageRecord::kText:
          {
            auto text = record->text();
            auto key = std::pair<std::string, std::string>(node_name, "");
            if(key.first.empty()) {
              ++counts["log/data"];
              ++counts["log/received"];
            } else {
              ++counts["text/" + key.first + "/data"];
              ++counts["text/" + key.first + "/received"];
            }
          }
          break;
        case thalamus_grpc::StorageRecord::kImage:
          {
            auto image = record->image();
            auto key = std::pair<std::string, std::string>(node_name, "");
            switch(image.format()) {
              case thalamus_grpc::Image::Format::Image_Format_Gray:
                ++counts["image/" + key.first + "/data"];
                ++counts["image/" + key.first + "/received"];
                result.dimensions["image/" + key.first + "/data"] = std::make_tuple(image.width(), image.height(), 0);
                result.datatypes["image/" + key.first + "/data"] = H5T_NATIVE_UCHAR;
                break;
              case thalamus_grpc::Image::Format::Image_Format_RGB:
                ++counts["image/" + key.first + "/data"];
                ++counts["image/" + key.first + "/received"];
                result.dimensions["image/" + key.first + "/data"] = std::make_tuple(image.width(), image.height(), 3);
                result.datatypes["image/" + key.first + "/data"] = H5T_NATIVE_UCHAR;
                break;
              case thalamus_grpc::Image::Format::Image_Format_YUYV422:
                ++counts["image/" + key.first + "/data"];
                ++counts["image/" + key.first + "/received"];
                result.dimensions["image/" + key.first + "/data"] = std::make_tuple(2*image.width(), image.height(), 0);
                result.datatypes["image/" + key.first + "/data"] = H5T_NATIVE_UCHAR;
                break;
              case thalamus_grpc::Image::Format::Image_Format_YUV420P:
              case thalamus_grpc::Image::Format::Image_Format_YUVJ420P:
                ++counts["image/" + key.first + "/y"];
                ++counts["image/" + key.first + "/u"];
                ++counts["image/" + key.first + "/v"];
                ++counts["image/" + key.first + "/received"];
                result.dimensions["image/" + key.first + "/y"] = std::make_tuple(image.width(), image.height(), 0);
                result.datatypes["image/" + key.first + "/y"] = H5T_NATIVE_UCHAR;
                result.dimensions["image/" + key.first + "/u"] = std::make_tuple(image.width()/2, image.height()/2, 0);
                result.datatypes["image/" + key.first + "/u"] = H5T_NATIVE_UCHAR;
                result.dimensions["image/" + key.first + "/v"] = std::make_tuple(image.width()/2, image.height()/2, 0);
                result.datatypes["image/" + key.first + "/v"] = H5T_NATIVE_UCHAR;
                break;
              case thalamus_grpc::Image::Format::Image_Format_Gray16:
                ++counts["image/" + key.first + "/data"];
                ++counts["image/" + key.first + "/received"];
                result.dimensions["image/" + key.first + "/data"] = std::make_tuple(image.width(), image.height(), 0);
                result.datatypes["image/" + key.first + "/data"] = H5T_NATIVE_USHORT;
                break;
              case thalamus_grpc::Image::Format::Image_Format_RGB16:
                ++counts["image/" + key.first + "/data"];
                ++counts["image/" + key.first + "/received"];
                result.dimensions["image/" + key.first + "/data"] = std::make_tuple(image.width(), image.height(), 3);
                result.datatypes["image/" + key.first + "/data"] = H5T_NATIVE_USHORT;
                break;
              default:
                THALAMUS_ASSERT(false, "Unexpected image format %d", image.format());
            }
          }
          break;
        case thalamus_grpc::StorageRecord::kEvent:
          {
            auto event = record->event();
            auto key = std::pair<std::string, std::string>("events", "");
            ++counts[key.first + "/data"];
            ++counts[key.first + "/received"];
          }
          break;
        default:
          break;
          //std::cout << "Unhandled record type " << record->body_case() << std::endl;
      }
    }
    return result;
  }

  template<typename T>
  void write_data(size_t time, size_t length, hid_t data, hid_t received, size_t& data_written, size_t& received_written, hid_t h5_type, const T* data_buffer, std::vector<T>& data_cache, std::vector<size_t>& received_cache, size_t data_chunk, size_t received_chunk, std::vector<hsize_t> dims = {}, bool update_received = true) {
    herr_t error;
    {
      auto& cache = data_cache;
      cache.insert(cache.end(), data_buffer, data_buffer + length);
      if(cache.size() >= data_chunk) {
        std::vector<hsize_t> hlength(1, data_chunk);
        hlength.insert(hlength.end(), dims.begin(), dims.end());
        for(auto i : dims) {
          hlength[0] /= i;
        }

        H5Handle mem_space = H5Screate_simple(hlength.size(), hlength.data(), NULL);
        THALAMUS_ASSERT(mem_space);

        H5Handle file_space = H5Dget_space(data);
        THALAMUS_ASSERT(file_space);

        std::vector<hsize_t> start(hlength.size(), 0);
        start[0] = data_written;
        for (auto i : dims) {
          start[0] /= i;
        }

        error = H5Sselect_hyperslab(file_space, H5S_SELECT_SET, start.data(), nullptr, hlength.data(), NULL);
        THALAMUS_ASSERT(error >= 0);
    
        error = H5Dwrite(data, h5_type, mem_space, file_space, H5P_DEFAULT, cache.data());
        THALAMUS_ASSERT(error >= 0);
        data_written += data_chunk;

        if constexpr (std::is_pointer<T>::value) {
          std::for_each(cache.begin(), cache.begin() + data_chunk, [](auto arg) { delete[] arg; });
        }
        cache.erase(cache.begin(), cache.begin() + data_chunk);
      }
    }
    if (update_received) {
      auto& cache = received_cache;
      cache.push_back(time);
      cache.push_back(data_written + data_cache.size());
      for (auto i : dims) {
        cache.back() /= i;
      }
      if(cache.size() >= 2*received_chunk) {
        hsize_t one_row[] = {received_chunk, 2};
        H5Handle mem_space = H5Screate_simple(2, one_row, NULL);
        THALAMUS_ASSERT(mem_space);

        H5Handle file_space = H5Dget_space(received);
        THALAMUS_ASSERT(file_space);
        hsize_t start[] = { received_written, 0 };
        error = H5Sselect_hyperslab(file_space, H5S_SELECT_SET, start, nullptr, one_row, NULL);
        THALAMUS_ASSERT(error >= 0);

        error = H5Dwrite(received, H5T_NATIVE_UINT64, mem_space, file_space, H5P_DEFAULT, cache.data());
        THALAMUS_ASSERT(error >= 0);
        received_written += received_chunk;

        cache.erase(cache.begin(), cache.begin() + 2*received_chunk);
      }
    }
  }

  int generate_video(boost::program_options::variables_map& vm) {
    auto video = vm.contains("video") ? vm["video"].as<std::string>() : std::string();
    std::string input = vm["input"].as<std::string>();

    std::string output;
    if (vm.count("output")) {
      output = vm["output"].as<std::string>();
    }
    else {
      output = input + "_" + video + ".mpg";
    }

    int width = 0;
    int height = 0;
    std::string pixel_format;
    std::set<size_t> times;
    {
      std::ifstream input_stream(input, std::ios::binary);
      std::optional<thalamus_grpc::StorageRecord> record;
      double progress;
      while((record = read_record(input_stream, &progress))) {
        if(record->body_case() == thalamus_grpc::StorageRecord::kImage && record->node() == video) {
          times.insert(record->time());
          if(width) {
            continue;
          }
          auto image = record->image();
          width = image.width();
          height = image.height();
          switch(image.format()) {
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
            default:
              THALAMUS_ASSERT(false, "Unexpected image format %d", image.format());
          }
        }
      }
    }
    THALAMUS_ASSERT(width > 0 && height > 0, "Failed to detect video dimensions");
    size_t total_diffs = 0;
    size_t last_time = 0;
    for(auto time : times) {
      if(last_time > 0) {
        total_diffs += time - last_time;
      }
      last_time = time;
    }
    auto average_interval = total_diffs/(times.size()-1);

    std::vector<std::pair<double, std::string>> framerates = {
      {24000.0/1001, "24000/1001"},
      {24, "24"},
      {25, "25"},
      {30000.0/1001, "30000/1001"},
      {30, "30"},
      {50, "50"},
      {60000.0/1001, "60000/1001"},
      {60, "60"},
      {15, "15"}, 
      {5, "5"}, 
      {10, "10"}, 
      {12, "12"}, 
      {15, "15"}
    };
    std::sort(framerates.begin(), framerates.end());
    auto framerate = 1e9/average_interval;
    auto framerate_i = std::lower_bound(framerates.begin(), framerates.end(), std::make_pair(framerate, std::string("")));
    std::string ffmpeg_framerate;
    if(framerate_i == framerates.begin()) {
      ffmpeg_framerate = framerates.front().second;
    } else if (framerate_i == framerates.end()) {
      ffmpeg_framerate = framerates.back().second;
    } else {
      if(framerate - (framerate_i-1)->first < framerate_i->first - framerate) {
        ffmpeg_framerate = (framerate_i-1)->second;
      } else {
        ffmpeg_framerate = framerate_i->second;
      }
    }

    auto location = boost::dll::program_location();
    auto command = absl::StrFormat("%s ffmpeg -y -f rawvideo -pixel_format %s -video_size %dx%d -i pipe: "
                                   "-codec mpeg1video -f matroska -qscale:v 2 -b:v 100M -r %s \"%s\"",
                                   location.string(), pixel_format, width, height, ffmpeg_framerate, output);
    std::cout << "command " << command;
    boost::process::opstream in;
    boost::process::child ffmpeg(command, boost::process::std_in < in, boost::process::std_out > stdout,  boost::process::std_err > stderr);
    {
      std::ifstream input_stream(input, std::ios::binary);
      std::optional<thalamus_grpc::StorageRecord> record;
      double progress;
      while((record = read_record(input_stream, &progress))) {
        if(record->body_case() == thalamus_grpc::StorageRecord::kImage && record->node() == video) {
          auto image = record->image();
          width = image.width();
          height = image.height();
          switch(image.format()) {
            case thalamus_grpc::Image::Format::Image_Format_Gray:
              {
                auto data = image.data(0);
                auto width = image.width();
                auto height = image.height();
                auto linesize = data.size()/height;
                auto char_ptr = reinterpret_cast<const char*>(data.data());
                for(auto i = 0;i < height;++i) {
                  in.write(char_ptr + i*linesize, width);
                }
                break;
              }
            case thalamus_grpc::Image::Format::Image_Format_RGB:
              {
                auto data = image.data(0);
                auto width = image.width();
                auto height = image.height();
                auto linesize = data.size()/height;
                auto char_ptr = reinterpret_cast<const char*>(data.data());
                for(auto i = 0;i < height;++i) {
                  in.write(char_ptr + i*linesize, 3*width);
                }
                break;
              }
            case thalamus_grpc::Image::Format::Image_Format_YUYV422:
              {
                auto data = image.data(0);
                auto width = image.width();
                auto height = image.height();
                auto linesize = data.size()/height;
                auto char_ptr = reinterpret_cast<const char*>(data.data());
                for(auto i = 0;i < height;++i) {
                  in.write(char_ptr + i*linesize, 2*width);
                }
                break;
              }
            case thalamus_grpc::Image::Format::Image_Format_YUV420P:
              {
                for (auto i = 0; i < 3; ++i) {
                  auto data = image.data(i);
                  auto width = image.width();
                  auto height = image.height();
                  if (i) {
                    width /= 2;
                    height /= 2;
                  }
                  auto linesize = data.size() / height;
                  auto char_ptr = reinterpret_cast<const char*>(data.data());
                  for (auto i = 0; i < height; ++i) {
                    in.write(char_ptr + i * linesize, width);
                  }
                }
                break;
              }
            case thalamus_grpc::Image::Format::Image_Format_YUVJ420P:
              {
                for (auto i = 0; i < 3; ++i) {
                  auto data = image.data(i);
                  auto width = image.width();
                  auto height = image.height();
                  if (i) {
                    width /= 2;
                    height /= 2;
                  }
                  auto linesize = data.size() / height;
                  auto char_ptr = reinterpret_cast<const char*>(data.data());
                  for (auto i = 0; i < height; ++i) {
                    in.write(char_ptr + i * linesize, width);
                  }
                }
                break;
              }
            case thalamus_grpc::Image::Format::Image_Format_Gray16:
              {
                auto data = image.data(0);
                auto width = image.width();
                auto height = image.height();
                auto linesize = data.size()/height;
                auto char_ptr = reinterpret_cast<const char*>(data.data());
                for(auto i = 0;i < height;++i) {
                  in.write(char_ptr + i*linesize, 2*width);
                }
                break;
              }
            case thalamus_grpc::Image::Format::Image_Format_RGB16:
              {
                auto data = image.data(0);
                auto width = image.width();
                auto height = image.height();
                auto linesize = data.size()/height;
                auto char_ptr = reinterpret_cast<const char*>(data.data());
                for(auto i = 0;i < height;++i) {
                  in.write(char_ptr + i*linesize, 6*width);
                }
                break;
              }
            default:
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

  int generate_csv(boost::program_options::variables_map& vm) {
    auto csv = vm.contains("csv") ? vm["csv"].as<std::string>() : std::string();
    std::string input = vm["input"].as<std::string>();

    std::set<std::string> channels;
    if(vm.contains("channels")) {
      auto text = vm["channels"].as<std::string>();
      auto tokens = absl::StrSplit(text, ',');
      channels.insert(tokens.begin(), tokens.end());
    }

    std::string output;
    if(vm.count("output")) {
      output = vm["output"].as<std::string>();
    } else {
      output = input + "_" + csv + ".csv";
    }

    std::map<std::string, std::string> tmpnames;
    std::map<std::string, std::ofstream> column_outputs;
    std::ifstream input_stream(input, std::ios::binary);
    std::optional<thalamus_grpc::StorageRecord> record;
    double progress;
    auto last_time = std::chrono::steady_clock::now();
    auto line_count = 0l;

    std::cout << "Extracting Channel CSVs" << std::endl;
    while((record = read_record(input_stream, &progress))) {
      auto now = std::chrono::steady_clock::now();
      if(now - last_time >= 5s) {
        std::cout << progress << "%" << std::endl;
        last_time = now;
      }

      auto node_name = record->node();
      if(node_name != csv) {
        continue;
      }
      switch(record->body_case()) {
        case thalamus_grpc::StorageRecord::kAnalog: {
          auto analog = record->analog();
          auto spans = analog.spans();

          for(auto& span : spans) {
            auto span_name = span.name().empty() ? "" : span.name();
            if(!channels.empty() && !channels.contains(span_name)) {
              continue;
            }
            if(!tmpnames.contains(span_name)) {
              tmpnames[span_name] = std::tmpnam(nullptr);
              column_outputs[span_name] = std::ofstream(tmpnames[span_name]);
              column_outputs[span_name] << "Time (ns)," << span_name << "," << std::endl;
              ++line_count;
            }

            for(auto i = span.begin();i < span.end();++i) {
              column_outputs[span_name] << record->time() << "," << analog.data(i) << "," << std::endl;
            }
            line_count += span.end() - span.begin();
          }
          break;
        }
      default:
        break;
      }
    }
    column_outputs.clear();

    std::cout << "Merging Channel CSVs" << std::endl;
    std::ofstream total_output = std::ofstream(output);
    std::map<std::string, std::ifstream> column_inputs;
    for(auto& pair : tmpnames) {
      column_inputs[pair.first] = std::ifstream(pair.second);
    }
    auto working = true;
    std::string line;
    auto merged_lines = 0;
    last_time = std::chrono::steady_clock::now();
    while(working) {
      auto now = std::chrono::steady_clock::now();
      if(now - last_time >= 5s) {
        std::cout << (100.0*merged_lines/line_count) << "%" << std::endl;
        last_time = now;
      }

      working = false;
      for (auto& pair : column_inputs) {
        if(pair.second.eof()) {
          total_output << ",,";
        } else {
          std::getline(pair.second, line);
          line = absl::StripAsciiWhitespace(line);
          if(line.empty()) {
            total_output << ",,";
            continue;
          }
          total_output << line;
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

    boost::program_options::options_description desc("Thalamus capture file parsing, version " GIT_COMMIT_HASH);
    desc.add_options()
      ("help,h", "produce help message")
      ("trace,t", "Enable tracing")
      ("stats,s", "Just print stats")
      ("gzip,g", boost::program_options::value<size_t>(), "GZIP compression level (default is no compression)")
      ("chunk,c", boost::program_options::value<size_t>(), "Chunk size")
      ("repeat,r", boost::program_options::value<size_t>(), "Run indefinitely, executing every N ms")
      ("input,i", boost::program_options::value<std::string>(), "Input file")
      ("csv", boost::program_options::value<std::string>(), "Output this node's data as csv")
      ("video", boost::program_options::value<std::string>(), "Output this node's data as video")
      ("channels", boost::program_options::value<std::string>(), "Channels to output")
      ("slash-replace", boost::program_options::value<std::string>(), "Text to replace slashes with")
      ("output,o", boost::program_options::value<std::string>(), "Output file");

    boost::program_options::variables_map vm;
    try {
      boost::program_options::store(boost::program_options::command_line_parser(argc, argv).options(desc).positional(p).run(), vm);
    } catch(std::exception& e) {
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
    auto video = vm.contains("video") ? vm["video"].as<std::string>() : std::string();
    auto csv = vm.contains("csv") ? vm["csv"].as<std::string>() : std::string();
    auto chunk_size = vm.contains("chunk") ? vm["chunk"].as<size_t>() : std::numeric_limits<size_t>::max();

    if(!vm.count("input")) {
      std::cout << desc << std::endl;
      return 1;
    }

    std::chrono::milliseconds interval = 0ms;
    if(vm.count("repeat")) {
      interval = std::chrono::milliseconds(vm["repeat"].as<size_t>());
    }

    auto just_stats = vm.contains("stats");

    std::string input = vm["input"].as<std::string>();
    std::string output;

    if(!csv.empty()) {
      return generate_csv(vm);
    }
    else if (!video.empty()) {
      return generate_video(vm);
    }

    if(vm.count("output")) {
      output = vm["output"].as<std::string>();
    } else {
      output = input + ".h5";
    }

    bool running = true;
    while(running) {
      auto start = std::chrono::steady_clock::now();

      std::cout << "Measuring Capture File" << std::endl;
      DataCount data_count = count_data(input, slash_replace);
      auto& dataset_counts = data_count.counts;
      std::map<std::string, H5Handle> datasets;
      std::map<std::string, size_t> written;
      for(const auto& pair : dataset_counts) {
        std::vector<std::string> tokens = absl::StrSplit(pair.first, '/');
        THALAMUS_ASSERT(tokens.size() > 0);
        if(tokens.back() == "received") {
          continue;
        }
        std::cout << pair.first << " " << pair.second << std::endl;
      }
      if(just_stats) {
        auto duration = std::chrono::steady_clock::now() - start;
        std::cout << "Duration: " << std::chrono::duration_cast<std::chrono::milliseconds>(duration).count() << " ms" << std::endl;
        if(interval != 0ms) {
          running = true;
          if(interval > duration) {
            std::this_thread::sleep_for(interval-duration);
          }
        } else {
          running = false;
        }
        continue;
      }

      std::cout << "Writing H5 file: " << output << std::endl;
      H5Handle fid = H5Fcreate(output.c_str(), H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
      if (!fid) {
        std::cout << "Failed to open output file" << std::endl;
        return 1;
      }
      
      H5Handle segment_type = createH5Segment(0);

      H5Handle blob_type =  H5Tvlen_create(H5T_NATIVE_UCHAR);
      THALAMUS_ASSERT(blob_type, "H5Tvlen_create(H5T_NATIVE_UCHAR) failed");

      H5Handle event_type = H5Tcreate(H5T_COMPOUND, sizeof(Event));
      THALAMUS_ASSERT(event_type, "H5Tcreate(H5T_COMPOUND, sizeof(Event)) failed");
      auto error = H5Tinsert(event_type, "time", HOFFSET(Event, time), H5T_NATIVE_UINT64);
      THALAMUS_ASSERT(error >= 0, "Failed to create Event.time");
      error = H5Tinsert(event_type, "payload", HOFFSET(Event, payload), blob_type);
      THALAMUS_ASSERT(error >= 0, "Failed to create Event.payload");

      H5Handle str_type = H5Tcopy(H5T_C_S1);
      error = H5Tset_size(str_type, H5T_VARIABLE);
      THALAMUS_ASSERT(error >= 0);
      error = H5Tset_strpad(str_type, H5T_STR_NULLTERM);
      THALAMUS_ASSERT(error >= 0);
      error = H5Tset_cset(str_type, H5T_CSET_UTF8);
      THALAMUS_ASSERT(error >= 0);

      H5Handle link_plist = H5Pcreate(H5P_LINK_CREATE);
      error = H5Pset_create_intermediate_group(link_plist, 1);
      THALAMUS_ASSERT(error >= 0);

      for(const auto& pair : dataset_counts) {
        std::vector<std::string> tokens = absl::StrSplit(pair.first, '/');
        THALAMUS_ASSERT(tokens.size() > 0);
        if(tokens.back() == "received") {
          hsize_t dims[] = { pair.second, 2 };
          hsize_t max_dims[] = { pair.second, 2 };
          H5Handle file_space = H5Screate_simple(2, dims, max_dims);
          THALAMUS_ASSERT(file_space);

          H5Handle plist_id = H5P_DEFAULT;
          if(gzip) {
            plist_id = H5Pcreate(H5P_DATASET_CREATE);

            hsize_t chunk[] = {dims[0], 2};
            error = H5Pset_chunk(plist_id, 2, chunk);
            THALAMUS_ASSERT(error >= 0);

            error = H5Pset_deflate(plist_id, gzip);
            THALAMUS_ASSERT(error >= 0);
          }

          datasets[pair.first] = H5Handle(
              H5Dcreate(fid, pair.first.c_str(), H5T_NATIVE_UINT64,
                        file_space, link_plist, plist_id, H5P_DEFAULT));
          THALAMUS_ASSERT(datasets[pair.first]);
        } else {
          hid_t type;
          hsize_t dims[] = { pair.second, 0, 0, 0 };
          hsize_t max_dims[] = { pair.second, 0, 0, 0 };
          int rank = 1;

          if(tokens.front() == "analog") {
            type = H5T_NATIVE_DOUBLE;
          } else if(tokens.front() == "xsens") {
            type = segment_type;
          } else if(tokens.front() == "events") {
            type = event_type;
          } else if(tokens.front() == "text") {
            type = str_type;
          } else if(tokens.front() == "log") {
            type = str_type;
          } else if(tokens.front() == "image") {
            type = data_count.datatypes[pair.first];
            auto image_dims = data_count.dimensions[pair.first];
            if(std::get<0>(image_dims) != 0) {
              dims[1] = std::get<0>(image_dims);
              max_dims[1] = std::get<0>(image_dims);
              ++rank;
            }
            if(std::get<1>(image_dims) != 0) {
              dims[2] = std::get<1>(image_dims);
              max_dims[2] = std::get<1>(image_dims);
              ++rank;
            }
            if(std::get<2>(image_dims) != 0) {
              dims[3] = std::get<2>(image_dims);
              max_dims[3] = std::get<2>(image_dims);
              ++rank;
            }
          }
          H5Handle file_space = H5Screate_simple(rank, dims, max_dims);
          THALAMUS_ASSERT(file_space);

          H5Handle plist_id = H5P_DEFAULT;
          if(gzip && dims[0] > 0) {
            plist_id = H5Pcreate(H5P_DATASET_CREATE);

            std::vector<hsize_t> chunk_dims(std::begin(dims), std::end(dims));
            chunk_dims[0] = std::min(chunk_dims[0], hsize_t(chunk_size));
            error = H5Pset_chunk(plist_id, chunk_dims.size(), chunk_dims.data());
            THALAMUS_ASSERT(error >= 0);

            error = H5Pset_deflate(plist_id, gzip);
            THALAMUS_ASSERT(error >= 0);
          }

          datasets[pair.first] = H5Handle(
              H5Dcreate(fid, pair.first.c_str(), type,
                        file_space, link_plist, plist_id, H5P_DEFAULT));
          THALAMUS_ASSERT(datasets[pair.first]);
        }
      }

      std::ifstream input_stream(input, std::ios::binary);

      std::optional<thalamus_grpc::StorageRecord> record;
      double progress;
      auto last_time = std::chrono::steady_clock::now();
      std::map<hid_t, std::vector<double>> data_caches;
      std::map<hid_t, std::vector<Segment>> segment_caches;
      std::map<hid_t, std::vector<char*>> text_caches;
      std::map<hid_t, std::vector<unsigned char>> image_caches;
      std::map<hid_t, std::vector<unsigned short>> short_image_caches;
      std::vector<Event> event_cache;
      std::map<hid_t, std::vector<size_t>> received_caches;

      while((record = read_record(input_stream, &progress))) {
        auto now = std::chrono::steady_clock::now();
        if(now - last_time >= 5s) {
          std::cout << progress << "%" << std::endl;
          last_time = now;
        }
        auto node_name = record->node();
        if(slash_replace) {
          node_name = absl::StrReplaceAll(node_name, {{"/", *slash_replace}});
        }

        switch(record->body_case()) {
          case thalamus_grpc::StorageRecord::kAnalog:
            {
              auto analog = record->analog();
              auto spans = analog.spans();
              hsize_t num_channels = spans.size();

              for(auto& span : spans) {
                hsize_t span_size = span.end() - span.begin();
                auto span_name = span.name().empty() ? "" : span.name();
                if(slash_replace) {
                  span_name = absl::StrReplaceAll(span_name, {{"/", *slash_replace}});
                }
                auto data_path = "analog/" + node_name + "/" + span_name + "/data";
                auto received_path = "analog/" + node_name + "/" + span_name + "/received";
                auto data = datasets[data_path];
                auto received = datasets[received_path];
                auto& data_written = written[data_path];
                auto& received_written = written[received_path];
                auto data_chunk = gzip ? std::min(dataset_counts[data_path] - data_written, chunk_size) : span_size;
                auto received_chunk = gzip ? std::min(dataset_counts[received_path] - received_written, chunk_size) : 1;
                write_data(record->time(), span_size, data, received, data_written, received_written, H5T_NATIVE_DOUBLE, analog.data().data() + span.begin(),
                           data_caches[data], received_caches[received], data_chunk, received_chunk);
              }
            }
            break;
          case thalamus_grpc::StorageRecord::kXsens:
            {
              auto xsens = record->xsens();

              auto data_path = "xsens/" + node_name + "/data";
              auto received_path = "xsens/" + node_name + "/received";
              auto data = datasets[data_path];
              auto received = datasets[received_path];
              auto& data_written = written[data_path];
              auto& received_written = written[received_path];
              auto data_count = gzip ? std::min(dataset_counts[data_path] - data_written, chunk_size) : xsens.segments().size();
              auto received_count = gzip ? std::min(dataset_counts[received_path] - received_written, chunk_size) : 1;
              std::string pose = xsens.pose_name();
              auto pose_cstr = pose.c_str();

              std::vector<Segment> segments;
              for(auto& s : xsens.segments()) {
                segments.emplace_back();
                auto& segment = segments.back();
                segment.frame = s.frame();
                segment.segment_id = s.id();
                segment.time = s.time();
                segment.actor = s.actor();
                segment.position = boost::qvm::vec<float, 3>{s.x(), s.y(), s.z()};
                segment.rotation = boost::qvm::quat<float>{s.q0(), s.q1(), s.q2(), s.q3()};
                segment.pose = pose_cstr;
              }
              hsize_t num_segments = xsens.segments().size();
              write_data(record->time(), num_segments, data, received, data_written, received_written, segment_type, segments.data(),
                         segment_caches[data], received_caches[received], data_count, received_count);
            }
            break;
          case thalamus_grpc::StorageRecord::kText:
            {
              auto text = record->text().text();
              auto text_data = new char[text.size()+1];
              strcpy(text_data, text.data());

              auto data_path = record->node().empty() ? "log/data" : "text/" + node_name + "/data";
              auto received_path = record->node().empty() ? "log/received" : "text/" + node_name + "/received";
              auto data = datasets[data_path];
              auto received = datasets[received_path];
              auto& data_written = written[data_path];
              auto& received_written = written[received_path];
              auto data_count = gzip ? std::min(dataset_counts[data_path] - data_written, chunk_size) : 1;
              auto received_count = gzip ? std::min(dataset_counts[received_path] - received_written, chunk_size) : 1;

              write_data(record->time(), 1, data, received, data_written, received_written, str_type, &text_data,
                         text_caches[data], received_caches[received], data_count, received_count);
            }
            break;
          case thalamus_grpc::StorageRecord::kImage:
            {
              auto image = record->image();

              const unsigned char* bytes_pointer;
              std::vector<unsigned char> bytes_vector;
              std::vector<unsigned short> shorts_vector;

              switch(image.format()) {
                case thalamus_grpc::Image::Format::Image_Format_Gray: 
                  {
                    auto data_path = "image/" + node_name + "/data";
                    auto received_path = "image/" + node_name + "/received";
                    auto data = datasets[data_path];
                    auto received = datasets[received_path];
                    auto& data_written = written[data_path];
                    auto& received_written = written[received_path];
                    auto data_count = gzip ? std::min(dataset_counts[data_path] - data_written, chunk_size) : 1;
                    auto received_count = gzip ? std::min(dataset_counts[received_path] - received_written, chunk_size) : 1;
                    auto length = image.width()*image.height();
                    auto linesize = image.data(0).size() / image.height();
                    data_count *= length;

                    bytes_pointer = reinterpret_cast<const unsigned char*>(image.data(0).data());
                    if (image.width() != linesize) {
                      bytes_vector.clear();
                      for (auto i = 0; i < image.height(); ++i) {
                        bytes_vector.insert(bytes_vector.end(), bytes_pointer, bytes_pointer + linesize * i + image.width());
                      }
                      bytes_pointer = bytes_vector.data();
                    }

                    write_data(record->time(), length, data, received, data_written, received_written, H5T_NATIVE_UCHAR, bytes_pointer,
                        image_caches[data], received_caches[received], data_count, received_count, {image.width(), image.height()});
                    break;
                  }
                case thalamus_grpc::Image::Format::Image_Format_RGB:
                  {
                    auto data_path = "image/" + node_name + "/data";
                    auto received_path = "image/" + node_name + "/received";
                    auto data = datasets[data_path];
                    auto received = datasets[received_path];
                    auto& data_written = written[data_path];
                    auto& received_written = written[received_path];
                    auto data_count = gzip ? std::min(dataset_counts[data_path] - data_written, chunk_size) : 1;
                    auto received_count = gzip ? std::min(dataset_counts[received_path] - received_written, chunk_size) : 1;
                    auto length = image.width()*image.height()*3;
                    auto linesize = image.data(0).size() / image.height();
                    data_count *= length;

                    bytes_pointer = reinterpret_cast<const unsigned char*>(image.data(0).data());
                    if (3*image.width() != linesize) {
                      bytes_vector.clear();
                      for (auto i = 0; i < image.height(); ++i) {
                        bytes_vector.insert(bytes_vector.end(), bytes_pointer, bytes_pointer + linesize * i + 3*image.width());
                      }
                      bytes_pointer = bytes_vector.data();
                    }

                    write_data(record->time(), length, data, received, data_written, received_written, H5T_NATIVE_UCHAR, bytes_pointer,
                        image_caches[data], received_caches[received], data_count, received_count, {image.width(), image.height(), 3});
                    break;
                  }
                case thalamus_grpc::Image::Format::Image_Format_YUYV422:
                  {
                    auto data_path = "image/" + node_name + "/data";
                    auto received_path = "image/" + node_name + "/received";
                    auto data = datasets[data_path];
                    auto received = datasets[received_path];
                    auto& data_written = written[data_path];
                    auto& received_written = written[received_path];
                    auto data_count = gzip ? std::min(dataset_counts[data_path] - data_written, chunk_size) : 1;
                    auto received_count = gzip ? std::min(dataset_counts[received_path] - received_written, chunk_size) : 1;
                    auto length = 2*image.width()*image.height();
                    auto linesize = image.data(0).size() / image.height();
                    data_count *= length;

                    bytes_pointer = reinterpret_cast<const unsigned char*>(image.data(0).data());
                    if (2*image.width() != linesize) {
                      bytes_vector.clear();
                      for (auto i = 0; i < image.height(); ++i) {
                        bytes_vector.insert(bytes_vector.end(), bytes_pointer, bytes_pointer + linesize * i + 2*image.width());
                      }
                      bytes_pointer = bytes_vector.data();
                    }

                    write_data(record->time(), length, data, received, data_written, received_written, H5T_NATIVE_UCHAR, bytes_pointer,
                        image_caches[data], received_caches[received], data_count, received_count, {2*image.width(), image.height()});
                    break;
                  }
                case thalamus_grpc::Image::Format::Image_Format_YUV420P:
                case thalamus_grpc::Image::Format::Image_Format_YUVJ420P:
                  {
                    std::vector<std::string> data_paths = {
                      "image/" + node_name + "/y",
                      "image/" + node_name + "/u",
                      "image/" + node_name + "/v",
                    };
                    auto update_received = true;
                    auto i = 0;
                    for(auto& data_path : data_paths) {
                      auto received_path = "image/" + node_name + "/received";
                      auto data = datasets[data_path];
                      auto received = datasets[received_path];
                      auto& data_written = written[data_path];
                      auto& received_written = written[received_path];
                      auto data_count = gzip ? std::min(dataset_counts[data_path] - data_written, chunk_size) : 1;
                      auto received_count = gzip ? std::min(dataset_counts[received_path] - received_written, chunk_size) : 1;
                      auto width = image.width();
                      auto height = image.height();
                      if(i > 0) {
                        width /= 2;
                        height /= 2;
                      }
                      auto length = width*height;
                      data_count *= length;
                      auto linesize = image.data(i).size() / height;

                      bytes_pointer = reinterpret_cast<const unsigned char*>(image.data(i).data());
                      if (width != linesize) {
                        bytes_vector.clear();
                        for (auto j = 0; j < height; ++j) {
                          bytes_vector.insert(bytes_vector.end(), bytes_pointer + linesize * j, bytes_pointer + linesize * j + width);
                        }
                        bytes_pointer = bytes_vector.data();
                      }

                      write_data(record->time(), length, data, received, data_written, received_written, H5T_NATIVE_UCHAR, bytes_pointer,
                          image_caches[data], received_caches[received], data_count, received_count, {width, height}, update_received);
                      update_received = false;
                      ++i;
                    }
                    break;
                  }
                case thalamus_grpc::Image::Format::Image_Format_Gray16:
                  {
                    auto data_path = "image/" + node_name + "/data";
                    auto received_path = "image/" + node_name + "/received";
                    auto data = datasets[data_path];
                    auto received = datasets[received_path];
                    auto& data_written = written[data_path];
                    auto& received_written = written[received_path];
                    auto data_count = gzip ? std::min(dataset_counts[data_path] - data_written, chunk_size) : 1;
                    auto received_count = gzip ? std::min(dataset_counts[received_path] - received_written, chunk_size) : 1;
                    auto length = image.width()*image.height();
                    data_count *= length;

                    bytes_pointer = reinterpret_cast<const unsigned char*>(image.data(0).data());
                    shorts_vector.clear();
                    for(auto j = 0;j < length;++j) {
                      shorts_vector.push_back((bytes_pointer[2*j] << 8) | bytes_pointer[2*j+1]);
                      if(image.bigendian()) {
                        boost::endian::big_to_native_inplace(shorts_vector.back());
                      } else {
                        boost::endian::little_to_native_inplace(shorts_vector.back());
                      }
                    }

                    write_data(record->time(), length, data, received, data_written, received_written, H5T_NATIVE_USHORT, shorts_vector.data(),
                        short_image_caches[data], received_caches[received], data_count, received_count, {image.width(), image.height()});
                    break;
                  }
                case thalamus_grpc::Image::Format::Image_Format_RGB16:
                  {
                    auto data_path = "image/" + node_name + "/data";
                    auto received_path = "image/" + node_name + "/received";
                    auto data = datasets[data_path];
                    auto received = datasets[received_path];
                    auto& data_written = written[data_path];
                    auto& received_written = written[received_path];
                    auto data_count = gzip ? std::min(dataset_counts[data_path] - data_written, chunk_size) : 1;
                    auto received_count = gzip ? std::min(dataset_counts[received_path] - received_written, chunk_size) : 1;
                    auto length = 3*image.width()*image.height();
                    data_count *= length;

                    bytes_pointer = reinterpret_cast<const unsigned char*>(image.data(0).data());
                    shorts_vector.clear();
                    for(auto j = 0;j < length;++j) {
                      shorts_vector.push_back((bytes_pointer[2*j] << 8) | bytes_pointer[2*j+1]);
                      if(image.bigendian()) {
                        boost::endian::big_to_native_inplace(shorts_vector.back());
                      } else {
                        boost::endian::little_to_native_inplace(shorts_vector.back());
                      }
                    }

                    write_data(record->time(), length, data, received, data_written, received_written, H5T_NATIVE_USHORT, shorts_vector.data(),
                        short_image_caches[data], received_caches[received], data_count, received_count, {image.width(), image.height(), 3});
                    break;
                  }
                default:
                  THALAMUS_ASSERT(false, "Unexpected image format %d", image.format());
              }
            }
            break;
          case thalamus_grpc::StorageRecord::kEvent:
            {
              auto event = record->event();
              auto data = datasets["events/data"];
              auto received = datasets["events/received"];
              auto& data_written = written["events/data"];
              auto& received_written = written["events/received"];
              auto data_count = gzip ? std::min(dataset_counts["events/data"] - data_written, chunk_size) : 1;
              auto received_count = gzip ? std::min(dataset_counts["events/received"] - received_written, chunk_size) : 1;
              
              Event h5_event;
              h5_event.time = event.time();
              h5_event.payload.len = event.payload().size();
              h5_event.payload.p = const_cast<void*>(static_cast<const void*>(event.payload().data()));
              write_data(record->time(), 1, data, received, data_written, received_written, event_type, &h5_event,
                         event_cache, received_caches[received], data_count, received_count);
            }
            break;
          default:
            break;
            //std::cout << "Unhandled record type " << record->body_case() << std::endl;
        }
      }

      auto duration = std::chrono::steady_clock::now() - start;
      std::cout << "Duration: " << std::chrono::duration_cast<std::chrono::milliseconds>(duration).count() << " ms" << std::endl;
      if(interval != 0ms) {
        running = true;
        if(interval > duration) {
          std::this_thread::sleep_for(interval-duration);
        }
      } else {
        running = false;
      }
    }
    return 0;
  }
}
