#include <thalamus/tracing.hpp>
#include <aruco_node.hpp>
#include <distortion_node.hpp>
#include <image_node.hpp>
#include <modalities_util.hpp>
#include <thread_pool.hpp>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
#include <boost/qvm/quat_access.hpp>
#include <boost/qvm/quat_operations.hpp>
#include <boost/qvm/quat_vec_operations.hpp>
#include <boost/qvm/vec_access.hpp>
#include <boost/qvm/vec_operations.hpp>
#include <opencv2/calib3d.hpp>
#include <opencv2/core/quaternion.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/objdetect/aruco_detector.hpp>
#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <memory>
#include <set>
#include <sstream>
#include <variant>

using namespace thalamus;

struct ArucoNode::Impl {
  ObservableDictPtr state;
  boost::asio::io_context &io_context;
  boost::signals2::scoped_connection state_connection;
  boost::signals2::scoped_connection boards_connection;
  std::vector<boost::signals2::scoped_connection> board_connections;
  std::vector<boost::signals2::scoped_connection> id_connections;
  std::vector<boost::signals2::scoped_connection> marker_connections;
  NodeGraph *graph;
  ArucoNode *outer;
  std::string pose_name;

  // One camera == one source node (typically a DistortionNode, which is both an
  // ImageNode and the intrinsics provider).  The same Boards config is detected
  // on every camera.
  struct SourceBinding {
    std::string name;
    ImageNode *image = nullptr;
    DistortionNode *distortion = nullptr;
    NodeGraph::NodeConnection get_connection;
    boost::signals2::scoped_connection ready_connection;
  };
  std::map<std::string, std::unique_ptr<SourceBinding>> source_bindings;
  std::vector<std::string> source_order; // stable camera index for outputs
  std::string single_source;             // legacy "Source" field
  ObservableListPtr sources_list;        // "Sources" list
  boost::signals2::scoped_connection sources_connection;

  cv::aruco::PredefinedDictionaryType dict_type = cv::aruco::DICT_6X6_250;
  cv::aruco::Dictionary dict;
  cv::aruco::DetectorParameters detector_parameters;
  std::shared_ptr<cv::aruco::ArucoDetector> detector;
  bool running = false;
  ThreadPool &pool;
  struct Frame {
    cv::Mat mat;
    std::chrono::nanoseconds interval;
    std::vector<Segment> segments;
    std::chrono::nanoseconds time;
    // Per-board quality-check metrics, keyed by analog channel name.  Ordered
    // (std::map) so analog channel indexing is deterministic across frames.
    std::map<std::string, double> metrics;
  };

  // Latest detection result for each camera, merged into current_frame on every
  // camera update (last-writer-wins per camera).
  struct CameraResult {
    cv::Mat color;
    std::vector<Segment> segments;
    std::map<std::string, double> metrics;
    std::chrono::nanoseconds time{0};
    std::chrono::nanoseconds interval{0};
  };
  std::map<std::string, CameraResult> camera_results;

  enum class BoardType { Grid, Layout };

  struct Marker {
    int id = 0;
    cv::Vec3d position;
    cv::Vec3d rotation;
    double size = 0;
  };

  struct Board {
    BoardType type = BoardType::Grid;
    std::string name;
    bool quality_check = false;
    double translation_x = 0, translation_y = 0, translation_z = 0;
    cv::Vec3d rotation;
    long long rows = 0;
    long long columns = 0;
    double markerSize = 0;
    double markerSeparation = 0;
    std::vector<int> ids;
    // Layout boards: each marker placed independently in board space.  Keyed
    // by the marker's ObservableDict so observers can update in place.
    std::vector<ObservableDictPtr> marker_order;
    std::map<ObservableDictPtr, Marker> markers;
  };

  std::map<ObservableDictPtr, Board> boards;

  Impl(ObservableDictPtr _state, boost::asio::io_context &_io_context,
       NodeGraph *_graph, ArucoNode *_outer)
      : state(_state), io_context(_io_context), graph(_graph), outer(_outer),
        pool(graph->get_thread_pool()) {

    dict = cv::aruco::getPredefinedDictionary(dict_type);
    detector =
        std::make_shared<cv::aruco::ArucoDetector>(dict, detector_parameters);

    using namespace std::placeholders;
    state_connection =
        state->changed.connect(std::bind(&Impl::on_change, this, _1, _2, _3));
    this->state->recap(std::bind(&Impl::on_change, this, _1, _2, _3));
  }

  void on_ids_change(ObservableDictPtr self,
                     ObservableCollection::Action action,
                     const ObservableCollection::Key &key,
                     const ObservableCollection::Value &value) {
    TRACE_EVENT("thalamus", "ArucoNode::on_ids_change");
    auto key_int = size_t(std::get<long long>(key));
    auto value_int = std::get<long long>(value);

    auto &board = boards[self];
    if (action == ObservableCollection::Action::Set) {
      while (board.ids.size() <= key_int) {
        board.ids.emplace_back();
      }
      board.ids[key_int] = int(value_int);
    } else {
      board.ids.erase(board.ids.begin() + int64_t(key_int));
    }
  }

  void on_marker_change(ObservableDictPtr board_self, ObservableDictPtr self,
                        ObservableCollection::Action,
                        const ObservableCollection::Key &key,
                        const ObservableCollection::Value &value) {
    TRACE_EVENT("thalamus", "ArucoNode::on_marker_change");
    auto key_str = std::get<std::string>(key);
    auto &marker = boards[board_self].markers[self];
    if (key_str == "id") {
      marker.id = int(std::get<long long>(value));
    } else if (key_str == "x") {
      marker.position[0] = std::get<double>(value);
    } else if (key_str == "y") {
      marker.position[1] = std::get<double>(value);
    } else if (key_str == "z") {
      marker.position[2] = std::get<double>(value);
    } else if (key_str == "rx") {
      marker.rotation[0] = std::get<double>(value);
    } else if (key_str == "ry") {
      marker.rotation[1] = std::get<double>(value);
    } else if (key_str == "rz") {
      marker.rotation[2] = std::get<double>(value);
    } else if (key_str == "size") {
      marker.size = std::get<double>(value);
    }
  }

  void on_markers_change(ObservableDictPtr board_self,
                         ObservableCollection::Action action,
                         const ObservableCollection::Key &key,
                         const ObservableCollection::Value &value) {
    TRACE_EVENT("thalamus", "ArucoNode::on_markers_change");
    auto &board = boards[board_self];
    auto index = size_t(std::get<long long>(key));
    if (action == ObservableCollection::Action::Set) {
      auto marker_dict = std::get<ObservableDictPtr>(value);
      if (index <= board.marker_order.size()) {
        board.marker_order.insert(
            board.marker_order.begin() + int64_t(index), marker_dict);
      } else {
        board.marker_order.push_back(marker_dict);
      }
      marker_connections.push_back(marker_dict->changed.connect(std::bind(
          &Impl::on_marker_change, this, board_self, marker_dict, _1, _2, _3)));
      marker_dict->recap(std::bind(&Impl::on_marker_change, this, board_self,
                                   marker_dict, _1, _2, _3));
    } else {
      if (index < board.marker_order.size()) {
        auto marker_dict = board.marker_order[index];
        board.markers.erase(marker_dict);
        board.marker_order.erase(board.marker_order.begin() + int64_t(index));
      }
    }
  }

  void on_board_change(ObservableDictPtr self, ObservableCollection::Action,
                       const ObservableCollection::Key &key,
                       const ObservableCollection::Value &value) {
    TRACE_EVENT("thalamus", "ArucoNode::on_board_change");
    auto key_str = std::get<std::string>(key);
    auto &board = boards[self];
    if (key_str == "Type") {
      board.type = std::get<std::string>(value) == "layout"
                       ? BoardType::Layout
                       : BoardType::Grid;
    } else if (key_str == "Name") {
      board.name = std::get<std::string>(value);
    } else if (key_str == "Quality Check") {
      board.quality_check = std::get<bool>(value);
    } else if (key_str == "Markers") {
      auto value_list = std::get<ObservableListPtr>(value);
      board_connections.push_back(value_list->changed.connect(
          std::bind(&Impl::on_markers_change, this, self, _1, _2, _3)));
      value_list->recap(
          std::bind(&Impl::on_markers_change, this, self, _1, _2, _3));
    } else if (key_str == "Rows") {
      board.rows = std::get<long long>(value);
    } else if (key_str == "Columns") {
      board.columns = std::get<long long>(value);
    } else if (key_str == "Marker Size") {
      board.markerSize = std::get<double>(value);
    } else if (key_str == "Marker Separation") {
      board.markerSeparation = std::get<double>(value);
    } else if (key_str == "ids") {
      auto value_list = std::get<ObservableListPtr>(value);
      id_connections.push_back(value_list->changed.connect(
          std::bind(&Impl::on_ids_change, this, self, _1, _2, _3)));
      value_list->recap(
          std::bind(&Impl::on_ids_change, this, self, _1, _2, _3));
    } else if (key_str == "translation_x") {
      board.translation_x = std::get<double>(value);
    } else if (key_str == "translation_y") {
      board.translation_y = std::get<double>(value);
    } else if (key_str == "translation_z") {
      board.translation_z = std::get<double>(value);
    } else if (key_str == "rotation_x") {
      board.rotation[0] = std::get<double>(value);
    } else if (key_str == "rotation_y") {
      board.rotation[1] = std::get<double>(value);
    } else if (key_str == "rotation_z") {
      board.rotation[2] = std::get<double>(value);
    }
  }

  void on_boards_change(ObservableCollection::Action action,
                        const ObservableCollection::Key &,
                        const ObservableCollection::Value &value) {
    TRACE_EVENT("thalamus", "ArucoNode::on_boards_change");
    if (action == ObservableCollection::Action::Set) {
      auto value_dict = std::get<ObservableDictPtr>(value);
      board_connections.push_back(value_dict->changed.connect(
          std::bind(&Impl::on_board_change, this, value_dict, _1, _2, _3)));
      value_dict->recap(
          std::bind(&Impl::on_board_change, this, value_dict, _1, _2, _3));
    }
    for (auto i = board_connections.begin(); i != board_connections.end();) {
      if (i->connected()) {
        ++i;
      } else {
        i = board_connections.erase(i);
      }
    }
  }

  void on_change(ObservableCollection::Action,
                 const ObservableCollection::Key &key,
                 const ObservableCollection::Value &value) {
    TRACE_EVENT("thalamus", "ArucoNode::on_change");
    auto key_str = std::get<std::string>(key);
    if (key_str == "Boards") {
      auto value_list = std::get<ObservableListPtr>(value);
      boards_connection = value_list->changed.connect(
          std::bind(&Impl::on_boards_change, this, _1, _2, _3));
      value_list->recap(std::bind(&Impl::on_boards_change, this, _1, _2, _3));
    } else if (key_str == "Source") {
      single_source = std::string(
          absl::StripAsciiWhitespace(std::get<std::string>(value)));
      rebuild_sources();
    } else if (key_str == "Sources") {
      sources_list = std::get<ObservableListPtr>(value);
      sources_connection = sources_list->changed.connect(
          [this](auto, auto, auto) { rebuild_sources(); });
      rebuild_sources();
    } else if (key_str == "Dictionary") {
      auto value_str = std::get<std::string>(value);
      if (value_str == "DICT_4X4_50") {
        dict_type = cv::aruco::DICT_4X4_50;
      } else if (value_str == "DICT_4X4_100") {
        dict_type = cv::aruco::DICT_4X4_100;
      } else if (value_str == "DICT_4X4_250") {
        dict_type = cv::aruco::DICT_4X4_250;
      } else if (value_str == "DICT_4X4_1000") {
        dict_type = cv::aruco::DICT_4X4_1000;
      } else if (value_str == "DICT_5X5_50") {
        dict_type = cv::aruco::DICT_5X5_50;
      } else if (value_str == "DICT_5X5_100") {
        dict_type = cv::aruco::DICT_5X5_100;
      } else if (value_str == "DICT_5X5_250") {
        dict_type = cv::aruco::DICT_5X5_250;
      } else if (value_str == "DICT_5X5_1000") {
        dict_type = cv::aruco::DICT_5X5_1000;
      } else if (value_str == "DICT_6X6_50") {
        dict_type = cv::aruco::DICT_6X6_50;
      } else if (value_str == "DICT_6X6_100") {
        dict_type = cv::aruco::DICT_6X6_100;
      } else if (value_str == "DICT_6X6_250") {
        dict_type = cv::aruco::DICT_6X6_250;
      } else if (value_str == "DICT_6X6_1000") {
        dict_type = cv::aruco::DICT_6X6_1000;
      } else if (value_str == "DICT_7X7_50") {
        dict_type = cv::aruco::DICT_7X7_50;
      } else if (value_str == "DICT_7X7_100") {
        dict_type = cv::aruco::DICT_7X7_100;
      } else if (value_str == "DICT_7X7_250") {
        dict_type = cv::aruco::DICT_7X7_250;
      } else if (value_str == "DICT_7X7_1000") {
        dict_type = cv::aruco::DICT_7X7_1000;
      } else if (value_str == "DICT_ARUCO_ORIGINAL") {
        dict_type = cv::aruco::DICT_ARUCO_ORIGINAL;
      } else if (value_str == "DICT_APRILTAG_16h5") {
        dict_type = cv::aruco::DICT_APRILTAG_16h5;
      } else if (value_str == "DICT_APRILTAG_25h9") {
        dict_type = cv::aruco::DICT_APRILTAG_25h9;
      } else if (value_str == "DICT_APRILTAG_36h10") {
        dict_type = cv::aruco::DICT_APRILTAG_36h10;
      } else if (value_str == "DICT_APRILTAG_36h11") {
        dict_type = cv::aruco::DICT_APRILTAG_36h11;
      } else if (value_str == "DICT_ARUCO_MIP_36h12") {
        dict_type = cv::aruco::DICT_ARUCO_MIP_36h12;
      }
      dict = cv::aruco::getPredefinedDictionary(dict_type);
      detector =
          std::make_shared<cv::aruco::ArucoDetector>(dict, detector_parameters);

    } else if (key_str == "Running") {
      frame = 0;
      running = std::get<bool>(value);
    }
  }

  void rebuild_sources() {
    TRACE_EVENT("thalamus", "ArucoNode::rebuild_sources");
    std::vector<std::string> desired;
    std::set<std::string> seen;
    auto add = [&](const std::string &raw) {
      auto t = std::string(absl::StripAsciiWhitespace(raw));
      if (!t.empty() && seen.insert(t).second) {
        desired.push_back(t);
      }
    };
    add(single_source);
    if (sources_list) {
      for (size_t i = 0; i < sources_list->size(); ++i) {
        auto v = sources_list->at(i).get();
        if (std::holds_alternative<std::string>(v)) {
          add(std::get<std::string>(v));
        }
      }
    }

    for (auto it = source_bindings.begin(); it != source_bindings.end();) {
      if (!seen.count(it->first)) {
        camera_results.erase(it->first);
        it = source_bindings.erase(it);
      } else {
        ++it;
      }
    }

    for (const auto &name : desired) {
      if (source_bindings.count(name)) {
        continue;
      }
      auto binding = std::make_unique<SourceBinding>();
      binding->name = name;
      auto *bp = binding.get();
      bp->get_connection =
          graph->get_node_scoped(name, [this, bp, name](auto weak) {
            auto locked = weak.lock();
            if (!locked) {
              bp->image = nullptr;
              bp->distortion = nullptr;
              return;
            }
            bp->image = node_cast<ImageNode *>(locked.get());
            bp->distortion = dynamic_cast<DistortionNode *>(locked.get());
            if (bp->image) {
              bp->ready_connection = locked->ready.connect(
                  [this, name](Node *) { this->on_data(name); });
            }
          });
      source_bindings[name] = std::move(binding);
    }

    source_order = desired;
  }

  // Tile every camera's latest annotated frame into a single grid image, merge
  // their segments (segment_id = camera_index*100 + board_index) and metrics
  // (prefixed with the camera name), then publish as the current frame.
  void combine_and_emit() {
    TRACE_EVENT("thalamus", "ArucoNode::combine_and_emit");
    std::vector<const CameraResult *> tiles;
    for (const auto &name : source_order) {
      auto it = camera_results.find(name);
      if (it != camera_results.end() && !it->second.color.empty()) {
        tiles.push_back(&it->second);
      }
    }
    if (tiles.empty()) {
      return;
    }

    int n = int(tiles.size());
    int cols = int(std::ceil(std::sqrt(double(n))));
    int rows = (n + cols - 1) / cols;
    const int cell_w = 480, cell_h = 360;
    cv::Mat canvas(rows * cell_h, cols * cell_w, CV_8UC3, cv::Scalar(0, 0, 0));
    for (int i = 0; i < n; ++i) {
      const cv::Mat &img = tiles[i]->color;
      double scale =
          std::min(double(cell_w) / img.cols, double(cell_h) / img.rows);
      int w = std::max(1, int(img.cols * scale));
      int h = std::max(1, int(img.rows * scale));
      cv::Mat resized;
      cv::resize(img, resized, cv::Size(w, h));
      int r = i / cols, c = i % cols;
      int x = c * cell_w + (cell_w - w) / 2;
      int y = r * cell_h + (cell_h - h) / 2;
      resized.copyTo(canvas(cv::Rect(x, y, w, h)));
    }

    std::vector<Segment> merged_segments;
    std::map<std::string, double> merged_metrics;
    std::chrono::nanoseconds latest_time{0}, latest_interval{0};
    int cam_index = 0;
    for (const auto &name : source_order) {
      auto it = camera_results.find(name);
      if (it == camera_results.end()) {
        ++cam_index;
        continue;
      }
      auto &res = it->second;
      for (auto seg : res.segments) {
        seg.segment_id = uint32_t(cam_index * 100) + seg.segment_id;
        merged_segments.push_back(seg);
      }
      for (const auto &kv : res.metrics) {
        merged_metrics[name + "_" + kv.first] = kv.second;
      }
      latest_time = std::max(latest_time, res.time);
      latest_interval = res.interval;
      ++cam_index;
    }

    current_frame.mat = canvas;
    current_frame.segments = std::move(merged_segments);
    current_frame.metrics = std::move(merged_metrics);
    current_frame.time = latest_time;
    current_frame.interval = latest_interval;
    outer->ready(outer);
  }

  Frame current_frame;
  unsigned int frame = 0;
  static unsigned int global_frame;

  // Flattened view of current_frame.metrics for the AnalogNode interface,
  // rebuilt from the (ordered) metrics map whenever num_channels() is queried.
  mutable std::vector<std::string> analog_names;
  mutable std::vector<double> analog_values;

  void sync_analog() const {
    analog_names.clear();
    analog_values.clear();
    for (auto &pair : current_frame.metrics) {
      analog_names.push_back(pair.first);
      analog_values.push_back(pair.second);
    }
  }

  void on_data(const std::string &source_name) {
    auto binding_it = source_bindings.find(source_name);
    if (binding_it == source_bindings.end()) {
      return;
    }
    ImageNode *source = binding_it->second->image;
    DistortionNode *distortion_source = binding_it->second->distortion;

    auto id = get_unique_id();
    TRACE_EVENT_BEGIN("thalamus", "ArucoNode::on_data",
                      perfetto::Flow::ProcessScoped(id));
    if (!source || !source->has_image_data() ||
        source->format() != ImageNode::Format::Gray) {
      TRACE_EVENT_END("thalamus");
      return;
    }
    ++frame;
    if (pool.full()) {
      TRACE_EVENT_END("thalamus");
      return;
    }

    unsigned char *data = const_cast<unsigned char *>(source->plane(0).data());
    auto width = int(source->width());
    auto height = int(source->height());
    auto frame_interval = source->frame_interval();
    cv::Mat in = cv::Mat(height, width, CV_8UC1, data).clone();
    cv::Mat camera_matrix =
        distortion_source ? distortion_source->camera_matrix() : cv::Mat();
    auto distortion_parameters =
        distortion_source ? distortion_source->distortion_coefficients()
                          : std::span<const double>();

    TRACE_EVENT_END("thalamus");
    pool.push([this, source_name, in, id, _boards = this->boards,
               _dict = this->dict, _running = this->running,
               _detector = this->detector, &_io_context = this->io_context,
               frame_interval, camera_matrix, _frame = this->frame,
               time = source->time(),
               _distortion_parameters = std::vector<double>(
                   distortion_parameters.begin(), distortion_parameters.end()),
               _outer = outer->shared_from_this()] {
      TRACE_EVENT_BEGIN("thalamus", "ArucoNode::compute",
                        perfetto::Flow::ProcessScoped(id));
      cv::Mat color;
      cv::cvtColor(in, color, cv::COLOR_GRAY2RGB);
      std::vector<MotionCaptureNode::Segment> _segments;
      std::map<std::string, double> _metrics;

      if (_running) {
        std::vector<int> ids;
        std::vector<std::vector<cv::Point2f>> corners, rejected;
        {
          TRACE_EVENT("thalamus", "cv::aruco::ArucoDetector::detectMarkers");
          _detector->detectMarkers(in, corners, ids, rejected);
        }
        {
          TRACE_EVENT("thalamus", "cv::aruco::drawDetectedMarkers");
          cv::aruco::drawDetectedMarkers(color, corners, ids);
        }

        // Compute the four 3D corner points of a layout marker in board space,
        // following OpenCV's corner order (TL, TR, BR, BL) for a marker lying in
        // its local XY plane, then rotated/translated to its placement.
        auto layout_marker_corners = [](const Marker &m) {
          float h = float(m.size) / 2.0f;
          std::vector<cv::Point3f> local = {
              {-h, h, 0.0f}, {h, h, 0.0f}, {h, -h, 0.0f}, {-h, -h, 0.0f}};
          cv::Mat rotmat;
          cv::Rodrigues(m.rotation, rotmat); // 3x3 CV_64F
          std::vector<cv::Point3f> out;
          for (auto &p : local) {
            double x = double(p.x), y = double(p.y), z = double(p.z);
            out.emplace_back(
                float(x * rotmat.at<double>(0, 0) + y * rotmat.at<double>(0, 1) +
                      z * rotmat.at<double>(0, 2) + m.position[0]),
                float(x * rotmat.at<double>(1, 0) + y * rotmat.at<double>(1, 1) +
                      z * rotmat.at<double>(1, 2) + m.position[1]),
                float(x * rotmat.at<double>(2, 0) + y * rotmat.at<double>(2, 1) +
                      z * rotmat.at<double>(2, 2) + m.position[2]));
          }
          return out;
        };

        if (!camera_matrix.empty() && !ids.empty()) {
          auto board_index = 0;
          for (auto &pair : _boards) {
            auto &board = pair.second;

            if (board.type == BoardType::Layout && board.marker_order.empty()) {
              ++board_index;
              continue;
            }

            cv::aruco::Board board_obj = [&]() -> cv::aruco::Board {
              if (board.type == BoardType::Layout) {
                std::vector<std::vector<cv::Point3f>> obj_points_3d;
                std::vector<int> board_ids;
                for (auto &marker_dict : board.marker_order) {
                  auto &marker = board.markers.at(marker_dict);
                  obj_points_3d.push_back(layout_marker_corners(marker));
                  board_ids.push_back(marker.id);
                }
                return cv::aruco::Board(obj_points_3d, _dict, board_ids);
              }
              return cv::aruco::GridBoard(
                  cv::Size(int(board.columns), int(board.rows)),
                  float(board.markerSize), float(board.markerSeparation), _dict,
                  board.ids);
            }();

            cv::Mat obj_points, img_points;
            board_obj.matchImagePoints(corners, ids, obj_points, img_points);
            cv::Vec3d rvec, tvec;
            if (obj_points.total() == 0) {
              continue;
            }
            try {
              {
                TRACE_EVENT("thalamus", "cv::solvePnP");
                cv::solvePnP(obj_points, img_points, camera_matrix,
                             _distortion_parameters, rvec, tvec);
              }

              float axis_length;
              if (board.type == BoardType::Layout) {
                double max_size = 0;
                for (auto &marker_dict : board.marker_order) {
                  max_size =
                      std::max(max_size, board.markers.at(marker_dict).size);
                }
                axis_length = float(max_size > 0 ? max_size : 0.05);
              } else {
                axis_length =
                    .5f * float(std::min(board.columns, board.rows)) *
                        float(board.markerSize + board.markerSeparation) +
                    float(board.markerSeparation);
              }

              {
                TRACE_EVENT("thalamus", "cv::drawFrameAxes");
                cv::drawFrameAxes(color, camera_matrix, _distortion_parameters,
                                  rvec, tvec, axis_length);
              }

              std::vector<cv::Point3f> axesPoints;
              axesPoints.emplace_back(0.0f, 0.0f, 0.0f);
              axesPoints.emplace_back(axis_length, 0.0f, 0.0f);
              axesPoints.emplace_back(0.0f, axis_length, 0.0f);
              axesPoints.emplace_back(0.0f, 0.0f, axis_length);

              cv::Mat rotmat(3, 3, CV_64F);
              cv::Rodrigues(board.rotation, rotmat);
              for (auto &point : axesPoints) {
                auto old = point;
                point.x = old.x * rotmat.at<float>(0, 0) +
                          old.y * rotmat.at<float>(0, 1) +
                          old.z * rotmat.at<float>(0, 2) +
                          float(board.translation_x);
                point.y = old.x * rotmat.at<float>(1, 0) +
                          old.y * rotmat.at<float>(1, 1) +
                          old.z * rotmat.at<float>(1, 2) +
                          float(board.translation_y);
                point.z = old.x * rotmat.at<float>(2, 0) +
                          old.y * rotmat.at<float>(2, 1) +
                          old.z * rotmat.at<float>(2, 2) +
                          float(board.translation_z);
              }

              std::vector<cv::Point2f> imagePoints;
              projectPoints(axesPoints, rvec, tvec, camera_matrix,
                            _distortion_parameters, imagePoints);

              // draw axes lines
              {
                TRACE_EVENT("thalamus", "cv::line");
                line(color, imagePoints[0], imagePoints[1],
                     cv::Scalar(0, 0, 255), 3);
                line(color, imagePoints[0], imagePoints[2],
                     cv::Scalar(0, 255, 0), 3);
                line(color, imagePoints[0], imagePoints[3],
                     cv::Scalar(255, 0, 0), 3);
              }

              if (board.quality_check) {
                TRACE_EVENT("thalamus", "ArucoNode::quality_check");

                // Detection coverage: how many of this board's markers were
                // matched (matchImagePoints emits 4 object points per marker).
                size_t total_markers = board.type == BoardType::Layout
                                           ? board.marker_order.size()
                                           : board.ids.size();
                int detected = int(obj_points.total()) / 4;

                // Reprojection error (RMS pixels) of the rigid board pose.
                double reproj_rms = 0.0;
                std::vector<cv::Point2f> reprojected;
                cv::projectPoints(obj_points, rvec, tvec, camera_matrix,
                                  _distortion_parameters, reprojected);
                cv::Mat img_points_2f =
                    img_points.reshape(2, int(img_points.total()));
                double sse = 0.0;
                size_t n = std::min(reprojected.size(),
                                    size_t(img_points_2f.rows));
                for (size_t k = 0; k < n; ++k) {
                  auto ip = img_points_2f.at<cv::Point2f>(int(k));
                  double dx = double(ip.x) - double(reprojected[k].x);
                  double dy = double(ip.y) - double(reprojected[k].y);
                  sse += dx * dx + dy * dy;
                }
                if (n > 0) {
                  reproj_rms = std::sqrt(sse / double(n));
                }

                // Measured-vs-known geometry (layout boards): estimate each
                // visible marker's pose independently and compare measured
                // inter-marker distances to the known config distances.
                double dist_err_mm = -1.0;
                if (board.type == BoardType::Layout) {
                  std::map<int, cv::Vec3d> measured, known;
                  for (auto &marker_dict : board.marker_order) {
                    auto &m = board.markers.at(marker_dict);
                    known[m.id] = m.position;
                    for (size_t k = 0; k < ids.size(); ++k) {
                      if (ids[k] != m.id) {
                        continue;
                      }
                      float h = float(m.size) / 2.0f;
                      std::vector<cv::Point3f> objp = {{-h, h, 0.0f},
                                                       {h, h, 0.0f},
                                                       {h, -h, 0.0f},
                                                       {-h, -h, 0.0f}};
                      cv::Vec3d mrvec, mtvec;
                      try {
                        cv::solvePnP(objp, corners[k], camera_matrix,
                                     _distortion_parameters, mrvec, mtvec, false,
                                     cv::SOLVEPNP_IPPE_SQUARE);
                        measured[m.id] = mtvec;
                      } catch (cv::Exception &e) {
                        THALAMUS_LOG(error) << e.what();
                      }
                      break;
                    }
                  }
                  double max_err = 0.0;
                  bool have_pair = false;
                  std::vector<int> seen;
                  for (auto &kv : measured) {
                    seen.push_back(kv.first);
                  }
                  for (size_t a = 0; a < seen.size(); ++a) {
                    for (size_t b = a + 1; b < seen.size(); ++b) {
                      double md = cv::norm(measured[seen[a]] - measured[seen[b]]);
                      double kd = cv::norm(known[seen[a]] - known[seen[b]]);
                      max_err = std::max(max_err, std::abs(md - kd));
                      have_pair = true;
                    }
                  }
                  if (have_pair) {
                    dist_err_mm = max_err * 1000.0;
                  }
                }

                std::string label =
                    board.name.empty()
                        ? "board" + std::to_string(board_index)
                        : board.name;
                std::ostringstream oss;
                oss << label << ": " << detected << "/" << total_markers
                    << " reproj=" << std::fixed << std::setprecision(1)
                    << reproj_rms << "px";
                if (dist_err_mm >= 0) {
                  oss << " dErr=" << std::setprecision(1) << dist_err_mm << "mm";
                }
                cv::putText(color, oss.str(),
                            imagePoints[0] + cv::Point2f(8.0f, -8.0f),
                            cv::FONT_HERSHEY_SIMPLEX, 0.5,
                            cv::Scalar(0, 255, 255), 1);

                _metrics[label + "_reproj_px"] = reproj_rms;
                _metrics[label + "_n_markers"] = double(detected);
                if (dist_err_mm >= 0) {
                  _metrics[label + "_dist_err_mm"] = dist_err_mm;
                }
              }

              _segments.emplace_back();
              _segments.back().frame = _frame;
              _segments.back().segment_id = uint32_t(board_index);
              _segments.back().time = uint32_t(
                  std::chrono::duration_cast<std::chrono::milliseconds>(time)
                      .count());

              cv::Mat rvecMat(3, 3, CV_64F);
              cv::Rodrigues(rvec, rvecMat);

              // auto boardRvecX = board.rotation[0]*rvecMat.at<double>(0, 0) +
              // board.rotation[1]*rvecMat.at<double>(0, 1) +
              // board.rotation[2]*rvecMat.at<double>(0, 2); auto boardRvecY =
              // board.rotation[0]*rvecMat.at<double>(1, 0) +
              // board.rotation[1]*rvecMat.at<double>(1, 1) +
              // board.rotation[2]*rvecMat.at<double>(1, 2); auto boardRvecZ =
              // board.rotation[0]*rvecMat.at<double>(2, 0) +
              // board.rotation[1]*rvecMat.at<double>(2, 1) +
              // board.rotation[2]*rvecMat.at<double>(2, 2); cv::Vec3d
              // boardRvec(boardRvecX, boardRvecY, boardRvecZ);

              auto quaternion = cv::Quat<float>::createFromRvec(rvec);
              auto boardQuaterion =
                  cv::Quat<float>::createFromRvec(board.rotation);
              auto total_quaternion = quaternion * boardQuaterion;

              boost::qvm::S(_segments.back().rotation) = total_quaternion.w;
              boost::qvm::X(_segments.back().rotation) = total_quaternion.x;
              boost::qvm::Y(_segments.back().rotation) = total_quaternion.y;
              boost::qvm::Z(_segments.back().rotation) = total_quaternion.z;

              auto boardTvecX = board.translation_x * rvecMat.at<double>(0, 0) +
                                board.translation_y * rvecMat.at<double>(0, 1) +
                                board.translation_z * rvecMat.at<double>(0, 2);
              auto boardTvecY = board.translation_x * rvecMat.at<double>(1, 0) +
                                board.translation_y * rvecMat.at<double>(1, 1) +
                                board.translation_z * rvecMat.at<double>(1, 2);
              auto boardTvecZ = board.translation_x * rvecMat.at<double>(2, 0) +
                                board.translation_y * rvecMat.at<double>(2, 1) +
                                board.translation_z * rvecMat.at<double>(2, 2);
              cv::Vec3d boardTvec(boardTvecX, boardTvecY, boardTvecZ);

              boost::qvm::X(_segments.back().position) =
                  float(tvec[0] + boardTvecX);
              boost::qvm::Y(_segments.back().position) =
                  float(tvec[1] + boardTvecY);
              boost::qvm::Z(_segments.back().position) =
                  float(tvec[2] + boardTvecZ);
            } catch (cv::Exception &e) {
              THALAMUS_LOG(error) << e.what();
            }
            ++board_index;
          }
        }
      }
      TRACE_EVENT_END("thalamus");
      boost::asio::post(_io_context, [this, id, source_name, color, _outer,
                                      frame_interval,
                                      returned_segments = std::move(_segments),
                                      returned_metrics = std::move(_metrics),
                                      time] {
        TRACE_EVENT("thalamus", "ArucoNode Post Main",
                    perfetto::TerminatingFlow::ProcessScoped(id));
        // _outer keeps the ArucoNode (and therefore this Impl) alive; this post
        // runs on the io_context thread so touching members is safe.
        camera_results[source_name] =
            CameraResult{color, std::move(returned_segments),
                         std::move(returned_metrics), time, frame_interval};
        combine_and_emit();
      });
    });
  }
};

ArucoNode::ArucoNode(ObservableDictPtr state,
                     boost::asio::io_context &io_context, NodeGraph *graph)
    : impl(new Impl(state, io_context, graph, this)) {}

ArucoNode::~ArucoNode() {}

std::string ArucoNode::type_name() { return "ARUCO"; }

std::span<MotionCaptureNode::Segment const> ArucoNode::segments() const {
  return std::span<Segment const>(impl->current_frame.segments.begin(),
                                  impl->current_frame.segments.end());
}

const std::string_view ArucoNode::pose_name() const { return ""; }

void ArucoNode::inject(const std::span<Segment const> &) { ready(this); }

std::chrono::nanoseconds ArucoNode::time() const {
  return impl->current_frame.time;
}

std::span<const double> ArucoNode::data(int channel) const {
  if (channel < 0 || size_t(channel) >= impl->analog_values.size()) {
    return std::span<const double>();
  }
  return std::span<const double>(&impl->analog_values[size_t(channel)], 1);
}

int ArucoNode::num_channels() const {
  impl->sync_analog();
  return int(impl->analog_names.size());
}

std::string_view ArucoNode::name(int channel) const {
  if (channel < 0 || size_t(channel) >= impl->analog_names.size()) {
    return "";
  }
  return impl->analog_names[size_t(channel)];
}

std::chrono::nanoseconds ArucoNode::sample_interval(int) const {
  return impl->current_frame.interval;
}

void ArucoNode::inject(const thalamus::vector<std::span<double const>> &spans,
                       const thalamus::vector<std::chrono::nanoseconds> &,
                       const thalamus::vector<std::string_view> &) {
  THALAMUS_ASSERT(spans.size() == 1, "Error");
  THALAMUS_ASSERT(spans.front().size() == 1, "Error");
}

bool ArucoNode::has_analog_data() const {
  return !impl->current_frame.metrics.empty();
}

bool ArucoNode::has_motion_data() const { return true; }

boost::json::value ArucoNode::process(const boost::json::value &) {
  return boost::json::value();
}

ImageNode::Plane ArucoNode::plane(int) const {
  auto &image = impl->current_frame.mat;
  return ImageNode::Plane(image.data, image.data + image.rows * image.cols *
                                                       image.channels());
}
size_t ArucoNode::num_planes() const { return 1; }
ImageNode::Format ArucoNode::format() const { return Format::RGB; }
size_t ArucoNode::width() const { return size_t(impl->current_frame.mat.cols); }
size_t ArucoNode::height() const {
  return size_t(impl->current_frame.mat.rows);
}
std::chrono::nanoseconds ArucoNode::frame_interval() const {
  return impl->current_frame.interval;
}
void ArucoNode::inject(const thalamus_grpc::Image &) {}
bool ArucoNode::has_image_data() const { return true; }
size_t ArucoNode::modalities() const {
  return THALAMUS_MODALITY_IMAGE | THALAMUS_MODALITY_MOCAP |
         THALAMUS_MODALITY_ANALOG;
}

unsigned int ArucoNode::Impl::global_frame = 0;
