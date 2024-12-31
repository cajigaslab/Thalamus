#include <aruco_node.hpp>
#include <image_node.hpp>
#include <modalities_util.hpp>
#include <opencv2/objdetect/aruco_detector.hpp>
#include <opencv2/core/quaternion.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/calib3d.hpp>
#include <thread_pool.hpp>
#include <distortion_node.hpp>
#include <boost/qvm/vec_access.hpp>
#include <boost/qvm/vec_operations.hpp>
#include <boost/qvm/quat_access.hpp>
#include <boost/qvm/quat_operations.hpp>
#include <boost/qvm/quat_vec_operations.hpp>

using namespace thalamus;

struct ArucoNode::Impl {
  ObservableDictPtr state;
  boost::asio::io_context& io_context;
  boost::signals2::scoped_connection state_connection;
  boost::signals2::scoped_connection boards_connection;
  std::vector<boost::signals2::scoped_connection> board_connections;
  std::vector<boost::signals2::scoped_connection> id_connections;
  NodeGraph* graph;
  ArucoNode* outer;
  std::string pose_name;
  NodeGraph::NodeConnection get_source_connection;
  ImageNode* source;
  DistortionNode* distortion_source;
  boost::signals2::scoped_connection source_connection;
  cv::aruco::PredefinedDictionaryType dict_type = cv::aruco::DICT_6X6_250;
  cv::aruco::Dictionary dict;
  cv::aruco::DetectorParameters detector_parameters;
  std::shared_ptr<cv::aruco::ArucoDetector> detector;
  bool running = false;
  ThreadPool& pool;
  size_t next_input_frame = 0;
  size_t next_output_frame = 0;
  struct Frame {
    cv::Mat mat;
    std::chrono::nanoseconds interval;
    std::vector<Segment> segments;
    std::chrono::nanoseconds time;
  };
  std::map<size_t, Frame> output_frames;
  std::chrono::nanoseconds frame_interval;

  struct Board {
    double translation_x = 0, translation_y = 0, translation_z = 0;
    cv::Vec3d rotation;
    long long rows;
    long long columns;
    double markerSize;
    double markerSeparation;
    std::vector<int> ids;
  };

  std::map<ObservableDictPtr, Board> boards;

  Impl(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph* graph, ArucoNode* outer)
    : state(state)
    , io_context(io_context)
    , graph(graph)
    , outer(outer)
    , pool(graph->get_thread_pool()) {

    dict = cv::aruco::getPredefinedDictionary(dict_type);
    detector = std::make_shared<cv::aruco::ArucoDetector>(dict, detector_parameters);

    using namespace std::placeholders;
    state_connection = state->changed.connect(std::bind(&Impl::on_change, this, _1, _2, _3));
    this->state->recap(std::bind(&Impl::on_change, this, _1, _2, _3));
  }

  void on_ids_change(ObservableDictPtr self, ObservableCollection::Action action, const ObservableCollection::Key& key, const ObservableCollection::Value& value) {
    size_t key_int = std::get<long long>(key);
    auto value_int = std::get<long long>(value);

    auto& board = boards[self];
    if(action == ObservableCollection::Action::Set) {
      while(board.ids.size() <= key_int) {
        board.ids.emplace_back();
      }
      board.ids[key_int] = value_int;
    } else {
      board.ids.erase(board.ids.begin()+key_int);
    }
  }

  void on_board_change(ObservableDictPtr self, ObservableCollection::Action, const ObservableCollection::Key& key, const ObservableCollection::Value& value) {
    auto key_str = std::get<std::string>(key);
    auto& board = boards[self];
    if(key_str == "Rows") {
      board.rows = std::get<long long>(value);
    } else if(key_str == "Columns") {
      board.columns = std::get<long long>(value);
    } else if(key_str == "Marker Size") {
      board.markerSize = std::get<double>(value);
    } else if(key_str == "Marker Separation") {
      board.markerSeparation = std::get<double>(value);
    } else if(key_str == "ids") {
      auto value_list = std::get<ObservableListPtr>(value);
      id_connections.push_back(value_list->changed.connect(std::bind(&Impl::on_ids_change, this, self, _1, _2, _3)));
      value_list->recap(std::bind(&Impl::on_ids_change, this, self, _1, _2, _3));
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

  void on_boards_change(ObservableCollection::Action action, const ObservableCollection::Key&, const ObservableCollection::Value& value) {
    if(action == ObservableCollection::Action::Set) {
      auto value_dict = std::get<ObservableDictPtr>(value);
      board_connections.push_back(value_dict->changed.connect(std::bind(&Impl::on_board_change, this, value_dict, _1, _2, _3)));
      value_dict->recap(std::bind(&Impl::on_board_change, this, value_dict, _1, _2, _3));
    }
    for(auto i = board_connections.begin();i != board_connections.end();) {
      if(i->connected()) {
        ++i;
      } else {
        i = board_connections.erase(i);
      }
    }
  }

  void on_change(ObservableCollection::Action, const ObservableCollection::Key& key, const ObservableCollection::Value& value) {
    auto key_str = std::get<std::string>(key);
    if(key_str == "Boards") {
      auto value_list = std::get<ObservableListPtr>(value);
      boards_connection = value_list->changed.connect(std::bind(&Impl::on_boards_change, this, _1, _2, _3));
      value_list->recap(std::bind(&Impl::on_boards_change, this, _1, _2, _3));
    } else if(key_str == "Source") {
      std::string source_str = std::get<std::string>(value);
      auto token = std::string(absl::StripAsciiWhitespace(source_str));

      get_source_connection = graph->get_node_scoped(token, [this,token](auto source) {
        auto locked_source = source.lock();
        if (!locked_source) {
          return;
        }
        
        if (node_cast<ImageNode*>(locked_source.get()) != nullptr) {
          this->source = node_cast<ImageNode*>(locked_source.get());
          source_connection = locked_source->ready.connect(std::bind(&Impl::on_data, this, _1));
        }

        if (dynamic_cast<DistortionNode*>(locked_source.get()) != nullptr) {
          this->distortion_source = dynamic_cast<DistortionNode*>(locked_source.get());
        } else {
          this->distortion_source = nullptr;
        }
      });
    } else if(key_str == "Dictionary") {
      auto value_str = std::get<std::string>(value);
      if(value_str == "DICT_4X4_50") {
        dict_type = cv::aruco::DICT_4X4_50;
      } else if(value_str == "DICT_4X4_100") {
        dict_type = cv::aruco::DICT_4X4_100;
      } else if(value_str == "DICT_4X4_250") {
        dict_type = cv::aruco::DICT_4X4_250;
      } else if(value_str == "DICT_4X4_1000") {
        dict_type = cv::aruco::DICT_4X4_1000;
      } else if(value_str == "DICT_5X5_50") {
        dict_type = cv::aruco::DICT_5X5_50;
      } else if(value_str == "DICT_5X5_100") {
        dict_type = cv::aruco::DICT_5X5_100;
      } else if(value_str == "DICT_5X5_250") {
        dict_type = cv::aruco::DICT_5X5_250;
      } else if(value_str == "DICT_5X5_1000") {
        dict_type = cv::aruco::DICT_5X5_1000;
      } else if(value_str == "DICT_6X6_50") {
        dict_type = cv::aruco::DICT_6X6_50;
      } else if(value_str == "DICT_6X6_100") {
        dict_type = cv::aruco::DICT_6X6_100;
      } else if(value_str == "DICT_6X6_250") {
        dict_type = cv::aruco::DICT_6X6_250;
      } else if(value_str == "DICT_6X6_1000") {
        dict_type = cv::aruco::DICT_6X6_1000;
      } else if(value_str == "DICT_7X7_50") {
        dict_type = cv::aruco::DICT_7X7_50;
      } else if(value_str == "DICT_7X7_100") {
        dict_type = cv::aruco::DICT_7X7_100;
      } else if(value_str == "DICT_7X7_250") {
        dict_type = cv::aruco::DICT_7X7_250;
      } else if(value_str == "DICT_7X7_1000") {
        dict_type = cv::aruco::DICT_7X7_250;
      } else if(value_str == "DICT_ARUCO_ORIGINAL") {
        dict_type = cv::aruco::DICT_ARUCO_ORIGINAL;
      } else if(value_str == "DICT_APRILTAG_16h5") {
        dict_type = cv::aruco::DICT_APRILTAG_16h5;
      } else if(value_str == "DICT_APRILTAG_25h9") {
        dict_type = cv::aruco::DICT_APRILTAG_25h9;
      } else if(value_str == "DICT_APRILTAG_36h10") {
        dict_type = cv::aruco::DICT_APRILTAG_36h10;
      } else if(value_str == "DICT_APRILTAG_36h11") {
        dict_type = cv::aruco::DICT_APRILTAG_36h11;
      } else if(value_str == "DICT_ARUCO_MIP_36h12") {
        dict_type = cv::aruco::DICT_ARUCO_MIP_36h12;
      }
      dict = cv::aruco::getPredefinedDictionary(dict_type);
      detector = std::make_shared<cv::aruco::ArucoDetector>(dict, detector_parameters);

    } else if(key_str == "Running") {
      frame = 0;
      running = std::get<bool>(value);
    }
  }

  Frame current_frame;
  unsigned int frame = 0;

  void on_data(Node*) {
    if(!source->has_image_data() || source->format() != ImageNode::Format::Gray) {
      return;
    }
    ++frame;
    if(pool.full()) {
      return;
    }

    unsigned char* data = const_cast<unsigned char*>(source->plane(0).data());
    int width = source->width();
    int height = source->height();
    auto frame_interval = source->frame_interval();
    cv::Mat in = cv::Mat(height, width, CV_8UC1, data).clone();
    cv::Mat camera_matrix = distortion_source ? distortion_source->camera_matrix() : cv::Mat();
    auto distortion_parameters = distortion_source ? distortion_source->distortion_coefficients() : std::span<const double>();

    pool.push([frame_id=next_input_frame++,
               in,
               boards=this->boards, 
               dict=this->dict, 
               running=this->running,
               detector=this->detector, 
               &io_context=this->io_context,
               &output_frames=this->output_frames,
               &next_output_frame=this->next_output_frame,
               &current_frame=this->current_frame,
               frame_interval,
               camera_matrix,
               frame=this->frame,
               time=source->time(),
               distortion_parameters=std::vector<double>(distortion_parameters.begin(), distortion_parameters.end()),
               outer=outer->shared_from_this()
    ] {
      cv::Mat color;
      cv::cvtColor(in, color, cv::COLOR_GRAY2RGB);
      std::vector<MotionCaptureNode::Segment> _segments;

      if(running) {
        std::vector<int> ids;
        std::vector<std::vector<cv::Point2f>> corners, rejected;
        detector->detectMarkers(in, corners, ids, rejected);
        cv::aruco::drawDetectedMarkers(color, corners, ids);

        if(!camera_matrix.empty() && !ids.empty()) {
          auto board_index = 0;
          for(auto& pair : boards) {
            auto& board = pair.second;
            cv::aruco::GridBoard grid_board(cv::Size(board.columns, board.rows), board.markerSize, board.markerSeparation, dict, board.ids);

            cv::Mat obj_points, img_points;
            grid_board.matchImagePoints(corners, ids, obj_points, img_points);
            cv::Vec3d rvec, tvec;
            if(obj_points.total() == 0) {
              continue;
            }
            try {
              cv::solvePnP(obj_points, img_points, camera_matrix, distortion_parameters, rvec, tvec);

              float axis_length = .5*std::min(board.columns, board.rows)*(board.markerSize + board.markerSeparation) + board.markerSeparation;

              cv::drawFrameAxes(color, camera_matrix, distortion_parameters, rvec, tvec, axis_length);

              std::vector<cv::Point3f> axesPoints;
              axesPoints.emplace_back(0.0f, 0.0f, 0.0f);
              axesPoints.emplace_back(axis_length, 0.0f, 0.0f);
              axesPoints.emplace_back(0.0f, axis_length, 0.0f);
              axesPoints.emplace_back(0.0f, 0.0f, axis_length);

              cv::Mat rotmat(3, 3, CV_64F);
              cv::Rodrigues(board.rotation, rotmat);
              for(auto& point : axesPoints) {
                auto old = point;
                point.x = old.x*rotmat.at<double>(0, 0) + old.y*rotmat.at<double>(0, 1) + old.z*rotmat.at<double>(0, 2) + board.translation_x;
                point.y = old.x*rotmat.at<double>(1, 0) + old.y*rotmat.at<double>(1, 1) + old.z*rotmat.at<double>(1, 2) + board.translation_y;
                point.z = old.x*rotmat.at<double>(2, 0) + old.y*rotmat.at<double>(2, 1) + old.z*rotmat.at<double>(2, 2) + board.translation_z;
              }

              std::vector<cv::Point2f> imagePoints;
              projectPoints(axesPoints, rvec, tvec, camera_matrix, distortion_parameters, imagePoints);

              // draw axes lines
              line(color, imagePoints[0], imagePoints[1], cv::Scalar(0, 0, 255), 3);
              line(color, imagePoints[0], imagePoints[2], cv::Scalar(0, 255, 0), 3);
              line(color, imagePoints[0], imagePoints[3], cv::Scalar(255, 0, 0), 3);

              _segments.emplace_back();
              _segments.back().frame = frame;
              _segments.back().segment_id = board_index;
              _segments.back().time = std::chrono::duration_cast<std::chrono::milliseconds>(time).count();

              cv::Mat rvecMat(3, 3, CV_64F);
              cv::Rodrigues(rvec, rvecMat);

              //auto boardRvecX = board.rotation[0]*rvecMat.at<double>(0, 0) + board.rotation[1]*rvecMat.at<double>(0, 1) + board.rotation[2]*rvecMat.at<double>(0, 2);
              //auto boardRvecY = board.rotation[0]*rvecMat.at<double>(1, 0) + board.rotation[1]*rvecMat.at<double>(1, 1) + board.rotation[2]*rvecMat.at<double>(1, 2);
              //auto boardRvecZ = board.rotation[0]*rvecMat.at<double>(2, 0) + board.rotation[1]*rvecMat.at<double>(2, 1) + board.rotation[2]*rvecMat.at<double>(2, 2);
              //cv::Vec3d boardRvec(boardRvecX, boardRvecY, boardRvecZ);

              auto quaternion = cv::Quat<float>::createFromRvec(rvec);
              auto boardQuaterion = cv::Quat<float>::createFromRvec(board.rotation);
              auto total_quaternion = quaternion*boardQuaterion;

              boost::qvm::S(_segments.back().rotation) = total_quaternion.w;
              boost::qvm::X(_segments.back().rotation) = total_quaternion.x;
              boost::qvm::Y(_segments.back().rotation) = total_quaternion.y;
              boost::qvm::Z(_segments.back().rotation) = total_quaternion.z;

              auto boardTvecX = board.translation_x*rvecMat.at<double>(0, 0) + board.translation_y*rvecMat.at<double>(0, 1) + board.translation_z*rvecMat.at<double>(0, 2);
              auto boardTvecY = board.translation_x*rvecMat.at<double>(1, 0) + board.translation_y*rvecMat.at<double>(1, 1) + board.translation_z*rvecMat.at<double>(1, 2);
              auto boardTvecZ = board.translation_x*rvecMat.at<double>(2, 0) + board.translation_y*rvecMat.at<double>(2, 1) + board.translation_z*rvecMat.at<double>(2, 2);
              cv::Vec3d boardTvec(boardTvecX, boardTvecY, boardTvecZ);

              boost::qvm::X(_segments.back().position) = tvec[0] + boardTvecX;
              boost::qvm::Y(_segments.back().position) = tvec[1] + boardTvecY;
              boost::qvm::Z(_segments.back().position) = tvec[2] + boardTvecZ;
            } catch(cv::Exception& e) {
              THALAMUS_LOG(error) << e.what();
            }
            ++board_index;
          }
        }
      }
      boost::asio::post(io_context, [frame_id,color,&output_frames,&next_output_frame,&current_frame,outer,frame_interval,_segments=std::move(_segments),time] {
        output_frames[frame_id] = Frame{color, frame_interval, std::move(_segments), time};
        for(auto i = output_frames.begin();i != output_frames.end();) {
          if(i->first == next_output_frame) {
            ++next_output_frame;
            current_frame = i->second;
            outer->ready(outer.get());
            i = output_frames.erase(i);
          } else {
            ++i;
          }
        }
      });
    });
  }
};

ArucoNode::ArucoNode(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph* graph)
  : impl(new Impl(state, io_context, graph, this)) {}

ArucoNode::~ArucoNode() {}

std::string ArucoNode::type_name() {
  return "ARUCO";
}

std::span<MotionCaptureNode::Segment const> ArucoNode::segments() const {
  return std::span<Segment const>(impl->current_frame.segments.begin(), impl->current_frame.segments.end());
}

const std::string_view ArucoNode::pose_name() const {
  return "";
}

void ArucoNode::inject(const std::span<Segment const>&) {
  ready(this);
}

std::chrono::nanoseconds ArucoNode::time() const {
  return impl->current_frame.time;
}

std::span<const double> ArucoNode::data(int channel) const {
  return std::span<const double>();
  THALAMUS_ASSERT(false, "Unexpected channel: %d", channel);
}

int ArucoNode::num_channels() const {
  return 0;
}

std::string_view ArucoNode::name(int) const {
  return "";
}

std::chrono::nanoseconds ArucoNode::sample_interval(int) const {
  return 0ns;
}

void ArucoNode::inject(const thalamus::vector<std::span<double const>>& spans, const thalamus::vector<std::chrono::nanoseconds>&, const thalamus::vector<std::string_view>&) {
  THALAMUS_ASSERT(spans.size() == 1, "Error");
  THALAMUS_ASSERT(spans.front().size() == 1, "Error");
}
 
bool ArucoNode::has_analog_data() const {
  return false;
}

bool ArucoNode::has_motion_data() const {
  return true;
}

boost::json::value ArucoNode::process(const boost::json::value&) {
  return boost::json::value();
}

ImageNode::Plane ArucoNode::plane(int) const {
  auto& image = impl->current_frame.mat;
  return ImageNode::Plane(image.data, image.data+image.rows*image.cols*image.channels());
}
size_t ArucoNode::num_planes() const {
  return 1;
}
ImageNode::Format ArucoNode::format() const {
  return Format::RGB;
}
size_t ArucoNode::width() const {
  return impl->current_frame.mat.cols;
}
size_t ArucoNode::height() const {
  return impl->current_frame.mat.rows;
}
std::chrono::nanoseconds ArucoNode::frame_interval() const {
  return impl->current_frame.interval;
}
void ArucoNode::inject(const thalamus_grpc::Image&) {
}
bool ArucoNode::has_image_data() const {
  return true;
}
size_t ArucoNode::modalities() const {
  return THALAMUS_MODALITY_IMAGE | THALAMUS_MODALITY_MOCAP;
}
