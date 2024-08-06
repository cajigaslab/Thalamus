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
#include <base_node.h>
#include <xsens_node.h>
#include <h5handle.h>
#include <boost/qvm/vec.hpp>
#include <boost/qvm/quat.hpp>

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
    size_t max_pose_length = 0;
  };

  DataCount count_data(const std::string& filename) {
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
      switch(record->body_case()) {
        case thalamus_grpc::StorageRecord::kAnalog:
          {
            auto analog = record->analog();
            auto spans = analog.spans();
            hsize_t num_channels = spans.size();
            for(auto& span : spans) {
              hsize_t span_size = span.end() - span.begin();
              auto span_name = span.name().empty() ? "" : span.name();
              counts["analog/" + record->node() + "/" + span_name + "/data"] += span_size;
              ++counts["analog/" + record->node() + "/" + span_name + "/received"];
            }
          }
          break;
        case thalamus_grpc::StorageRecord::kXsens:
          {
            auto xsens = record->xsens();
            result.max_pose_length = std::max(result.max_pose_length, xsens.pose_name().size());
            auto key = std::pair<std::string, std::string>(record->node(), "");
            counts["xsens/" + key.first + "/data"] += xsens.segments().size();
            ++counts["xsens/" + key.first + "/received"];
          }
          break;
        case thalamus_grpc::StorageRecord::kText:
          {
            auto text = record->text();
            auto key = std::pair<std::string, std::string>(record->node(), "");
            if(key.first.empty()) {
              ++counts["log/data"];
              ++counts["log/received"];
            } else {
              ++counts["text/" + key.first + "/data"];
              ++counts["text/" + key.first + "/received"];
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
  void write_data(size_t time, size_t length, hid_t data, hid_t received, size_t& data_written, size_t& received_written, hid_t h5_type, const T* data_buffer, std::vector<T>& data_cache, std::vector<size_t>& received_cache, size_t data_chunk, size_t received_chunk) {
    herr_t error;
    {
      auto& cache = data_cache;
      cache.insert(cache.end(), data_buffer, data_buffer + length);
      if(cache.size() >= data_chunk) {
        hsize_t hlength = data_chunk;
        H5Handle mem_space = H5Screate_simple(1, &hlength, NULL);
        THALAMUS_ASSERT(mem_space);

        H5Handle file_space = H5Dget_space(data);
        THALAMUS_ASSERT(file_space);
        hsize_t start[] = { data_written };
        error = H5Sselect_hyperslab(file_space, H5S_SELECT_SET, start, nullptr, &hlength, NULL);
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
    {
      auto& cache = received_cache;
      cache.push_back(time);
      cache.push_back(data_written + data_cache.size());
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

    if(vm.count("output")) {
      output = vm["output"].as<std::string>();
    } else {
      output = input + ".h5";
    }

    bool running = true;
    while(running) {
      auto start = std::chrono::steady_clock::now();

      std::cout << "Measuring Capture File" << std::endl;
      DataCount data_count = count_data(input);
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
          }
          hsize_t dims[] = { pair.second };
          hsize_t max_dims[] = { pair.second };
          H5Handle file_space = H5Screate_simple(1, dims, max_dims);
          THALAMUS_ASSERT(file_space);

          H5Handle plist_id = H5P_DEFAULT;
          if(gzip && dims[0] > 0) {
            plist_id = H5Pcreate(H5P_DATASET_CREATE);

            hsize_t chunk = std::min(dims[0], hsize_t(chunk_size));
            error = H5Pset_chunk(plist_id, 1, &chunk);
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
      std::vector<Event> event_cache;
      std::map<hid_t, std::vector<size_t>> received_caches;

      while((record = read_record(input_stream, &progress))) {
        auto now = std::chrono::steady_clock::now();
        if(now - last_time >= 5s) {
          std::cout << progress << "%" << std::endl;
          last_time = now;
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
                auto data_path = "analog/" + record->node() + "/" + span_name + "/data";
                auto received_path = "analog/" + record->node() + "/" + span_name + "/received";
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

              auto data_path = "xsens/" + record->node() + "/data";
              auto received_path = "xsens/" + record->node() + "/received";
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

              auto data_path = record->node().empty() ? "log/data" : "text/" + record->node() + "/data";
              auto received_path = record->node().empty() ? "log/received" : "text/" + record->node() + "/received";
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
