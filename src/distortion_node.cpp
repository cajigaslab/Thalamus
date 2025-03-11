#include <distortion_node.hpp>
#include <shared_mutex>
#include <thalamus/tracing.hpp>
#include <thread_pool.hpp>

#include <modalities_util.hpp>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include "opencv2/calib3d.hpp"
#include "opencv2/imgproc.hpp"
#include <boost/pool/object_pool.hpp>

#ifdef __clang__
#pragma clang diagnostic pop
#endif

namespace thalamus {
using namespace std::chrono_literals;

struct DistortionNode::Impl {
  std::shared_ptr<ThreadPool> pool_ref;
  boost::asio::io_context &io_context;
  ObservableDictPtr state;
  boost::signals2::scoped_connection state_connection;
  boost::signals2::scoped_connection options_connection;
  boost::signals2::scoped_connection source_connection;
  boost::signals2::scoped_connection distortion_connection;
  boost::signals2::scoped_connection mat_connection;
  boost::signals2::scoped_connection mat0_connection;
  boost::signals2::scoped_connection mat1_connection;
  boost::signals2::scoped_connection mat2_connection;
  ImageNode *image_source;
  bool is_running = false;
  bool computing = false;
  DistortionNode *outer;
  std::chrono::nanoseconds time;
  std::thread distortion_thread;
  bool running = false;
  bool collecting = false;
  bool invert = false;
  bool apply_threshold;
  thalamus_grpc::Image image;
  std::vector<unsigned char> intermediate;
  thalamus::vector<Plane> data;
  Format format;
  size_t width;
  size_t height;
  bool need_recenter;
  NodeGraph *graph;
  size_t threshold;
  size_t rows;
  size_t columns;
  double x_gain;
  double y_gain;
  bool invert_x;
  bool invert_y;
  size_t source_width = std::numeric_limits<size_t>::max();
  size_t source_height = std::numeric_limits<size_t>::max();
  size_t next_input_frame = 0;
  size_t next_output_frame = 0;
  std::set<cv::Mat *> mat_pool;
  cv::Mat map1, map2;
  double square_size = 1;

  struct Result {
    cv::Mat image = cv::Mat();
    std::chrono::nanoseconds interval = 0ns;
    bool has_image = false;
    bool has_analog = false;
    double latency = 0;
    std::chrono::nanoseconds time = 0ns;
  };
  std::map<size_t, Result> output_frames;
  Result current_result;
  std::vector<std::vector<cv::Point2f>> computations;
  std::shared_mutex mutex;
  cv::Mat camera_matrix = cv::Mat::eye(3, 3, CV_64FC1);
  std::vector<double> distortion_coefficients = std::vector<double>(5, 0);
  ThreadPool &pool;
  bool busy = false;

  Impl(ObservableDictPtr _state, boost::asio::io_context &_io_context,
       DistortionNode *_outer, NodeGraph *_graph)
      : io_context(_io_context), state(_state), outer(_outer), graph(_graph),
        pool(_graph->get_thread_pool()) {
    using namespace std::placeholders;

    state_connection =
        state->changed.connect(std::bind(&Impl::on_change, this, _1, _2, _3));
    this->state->recap(std::bind(&Impl::on_change, this, _1, _2, _3));
  }

  ~Impl() {
    (*state)["Running"].assign(false, [&] {});
    for (auto mat : mat_pool) {
      delete mat;
    }
  }

  void stop() {}

  void on_data(Node *) {
    auto id = get_unique_id();
    TRACE_EVENT_BEGIN("thalamus", "DistortionNode::on_data",
                      perfetto::Flow::ProcessScoped(id));
    if (image_source->format() != ImageNode::Format::Gray || pool.full()) {
      TRACE_EVENT_END("thalamus");
      return;
    }

    unsigned char *luma_data =
        const_cast<unsigned char *>(image_source->plane(0).data());
    if (source_width != image_source->width() ||
        source_height != image_source->height()) {
      TRACE_EVENT("thalamus", "cv::initUndistortRectifyMap");
      source_width = image_source->width();
      source_height = image_source->height();
      auto r = cv::Mat::eye(3, 3, CV_64FC1);
      auto new_camera_matrix = camera_matrix.clone();
      map1 = cv::Mat::zeros(int(source_height), int(source_width), CV_16SC2);
      map2 = cv::Mat::zeros(int(source_height), int(source_width), CV_16UC1);
      cv::initUndistortRectifyMap(
          camera_matrix, distortion_coefficients, r, new_camera_matrix,
          cv::Size(int(source_width), int(source_height)), CV_16SC2, map1,
          map2);
    }
    auto frame_interval = image_source->frame_interval();
    cv::Mat in =
        cv::Mat(int(source_height), int(source_width), CV_8UC1, luma_data);
    auto run_in_pool = collecting || computing;
    if (apply_threshold || run_in_pool) {
      TRACE_EVENT("thalamus", "cv::Mat::clone");
      in = in.clone();
    }
    busy = true;

    // if(mat_pool.empty()) {
    //   mat_pool.insert(new cv::Mat());
    // }
    // auto out = *mat_pool.begin();
    // mat_pool.erase(out);
    auto frame_id = 0;
    if (!collecting) {
      frame_id = int(next_input_frame++);
    }

    auto execution = [frame_id, run_in_pool, id, &c_busy = this->busy,
                      c_time = image_source->time(),
                      // out,
                      c_state = this->state, frame_interval,
                      c_invert = this->invert, c_rows = this->rows,
                      c_camera_matrix = this->camera_matrix,
                      c_distortion_coefficients = this->distortion_coefficients,
                      c_columns = this->columns,
                      c_collecting = this->collecting,
                      c_apply_threshold = this->apply_threshold,
                      &c_computations = this->computations,
                      &c_mutex = this->mutex, c_computing = this->computing,
                      &c_current_result = current_result,
                          //&mat_pool=this->mat_pool,
                          &c_output_frames = this->output_frames,
                      &c_next_output_frame = this->next_output_frame,
                      &c_io_context = io_context, c_threshold = this->threshold,
                      c_map1 = this->map1, c_map2 = this->map2,
                      thresholded = in, c_outer = outer->shared_from_this()] {
      TRACE_EVENT_BEGIN("thalamus", "DistortionNode::compute",
                        perfetto::Flow::ProcessScoped(id));
      auto start = std::chrono::steady_clock::now();
      std::vector<std::vector<cv::Point>> contours;
      std::vector<cv::Vec4i> hierarchy;

      if (c_apply_threshold) {
        TRACE_EVENT("thalamus", "cv::threshold");
        cv::threshold(thresholded, thresholded, double(c_threshold), 255,
                      c_invert ? cv::THRESH_BINARY_INV : cv::THRESH_BINARY);
      }

      cv::Mat out, undistorted;

      auto board_size = cv::Size(int(c_columns), int(c_rows));
      if (c_collecting) {
        {
          TRACE_EVENT("thalamus", "cv::cvtColor");
          cv::cvtColor(thresholded, out, cv::COLOR_GRAY2RGB);
        }
        undistorted = out;

        std::vector<cv::Point2f> corners;
        bool found;
        {
          TRACE_EVENT("thalamus", "cv::findChessboardCorners");
          found = cv::findChessboardCorners(thresholded, board_size, corners,
                                            cv::CALIB_CB_FAST_CHECK |
                                                cv::CALIB_CB_NORMALIZE_IMAGE |
                                                cv::CALIB_CB_ADAPTIVE_THRESH);
        }
        std::shared_lock<std::shared_mutex> lock(c_mutex);
        if (found) {
          cv::TermCriteria criteria(
              cv::TermCriteria::EPS + cv::TermCriteria::MAX_ITER, 30, .1);
          {
            TRACE_EVENT("thalamus", "cv::cornerSubPix");
            cv::cornerSubPix(thresholded, corners, cv::Size(11, 11),
                             cv::Size(-1, -1), criteria);
          }

          if (c_computations.size()) {
            auto distance = 0.0;
            for (auto i = 0u; i < corners.size(); ++i) {
              distance += cv::norm(corners[i] - c_computations.back()[i]);
            }
            distance /= double(corners.size());
            if (distance > 10) {
              lock.unlock();
              std::lock_guard<std::shared_mutex> lock2(c_mutex);
              c_computations.emplace_back(std::move(corners));
            }
          } else {
            lock.unlock();
            std::lock_guard<std::shared_mutex> lock2(c_mutex);
            c_computations.emplace_back(std::move(corners));
          }
        }
        {
          TRACE_EVENT("thalamus", "cv::drawChessboardCorners");
          for (const auto &comp : c_computations) {
            cv::drawChessboardCorners(out, board_size, comp, true);
          }
        }
      } else {
        if (c_computing) {
          TRACE_EVENT("thalamus", "cv::remap");
          cv::remap(thresholded, undistorted, c_map1, c_map2, cv::INTER_LINEAR,
                    cv::BORDER_CONSTANT);
        } else {
          undistorted = thresholded;
        }
      }
      auto elapsed = std::chrono::steady_clock::now() - start;
      auto finish = [undistorted, elapsed,
                     //&mat_pool,
                     c_time, c_collecting, &c_busy, frame_id, id,
                     &c_output_frames, &c_next_output_frame, &c_current_result,
                     frame_interval, c_outer] {
        TRACE_EVENT("thalamus", "DistortionNode Post to Main",
                    perfetto::TerminatingFlow::ProcessScoped(id));
        auto latency = double(
            std::chrono::duration_cast<std::chrono::milliseconds>(elapsed)
                .count());
        if (c_collecting) {
          c_current_result =
              Result{undistorted, frame_interval, true, true, latency, c_time};
          TRACE_EVENT("thalamus", "DistortionNode::ready");
          c_outer->ready(c_outer.get());
          return;
        }
        c_output_frames[size_t(frame_id)] =
            Result{undistorted, frame_interval, true, true, latency, c_time};
        for (auto i = c_output_frames.begin(); i != c_output_frames.end();) {
          if (i->first == c_next_output_frame) {
            ++c_next_output_frame;
            c_current_result = i->second;
            TRACE_EVENT("thalamus", "DistortionNode::ready");
            c_outer->ready(c_outer.get());
            i = c_output_frames.erase(i);
          } else {
            ++i;
          }
        }
        c_busy = false;
      };
      if (run_in_pool) {
        TRACE_EVENT_END("thalamus");
        boost::asio::post(c_io_context, std::move(finish));
      } else {
        finish();
        TRACE_EVENT_END("thalamus");
      }
    };
    if (run_in_pool) {
      TRACE_EVENT_END("thalamus");
      pool.push(std::move(execution));
    } else {
      execution();
      TRACE_EVENT_END("thalamus");
    }
  }

  void on_change(ObservableCollection::Action a,
                 const ObservableCollection::Key &k,
                 const ObservableCollection::Value &v) {
    auto key_str = std::get<std::string>(k);
    if (key_str == "Threshold") {
      threshold = size_t(std::get<long long int>(v));
    } else if (key_str == "Computing") {
      computing = std::get<bool>(v);
    } else if (key_str == "Square Size") {
      square_size = std::get<double>(v);
    } else if (key_str == "Show Threshold" || key_str == "Apply Threshold") {
      apply_threshold = std::get<bool>(v);
    } else if (key_str == "Collecting") {
      std::lock_guard<std::shared_mutex> lock(mutex);
      collecting = std::get<bool>(v);
      if (collecting) {
        computations.clear();
        output_frames.clear();
      }
      if (!collecting && !computations.empty()) {
        decltype(computations) local_computations;
        local_computations.swap(computations);
        pool.push([c_local_computations = std::move(local_computations),
                   c_columns = this->columns, c_rows = this->rows,
                   c_state = this->state, c_width = this->source_width,
                   c_square_size = this->square_size,
                   c_height = this->source_height] {
          std::vector<std::vector<cv::Point3f>> object_points;
          for ([[maybe_unused]] const auto &_ : c_local_computations) {
            object_points.emplace_back();
            for (size_t y = 0; y < c_rows; ++y) {
              for (size_t x = 0; x < c_columns; ++x) {
                object_points.back().emplace_back(
                    static_cast<float>(c_square_size * double(x)),
                    static_cast<float>(c_square_size * double(y)), 0.0f);
              }
            }
          }
          std::vector<cv::Mat> tvecs, rvecs;
          cv::Mat new_camera_matrix;
          std::vector<double> new_distortion_coefficients;
          cv::calibrateCamera(
              object_points, c_local_computations,
              cv::Size(int(c_width), int(c_height)), new_camera_matrix,
              new_distortion_coefficients, rvecs, tvecs, 0,
              cv::TermCriteria(cv::TermCriteria::COUNT + cv::TermCriteria::EPS,
                               30, DBL_EPSILON));
          THALAMUS_LOG(info) << "Calibration Done";
          auto camera_matrix_state = std::make_shared<ObservableList>();
          for (auto r = 0; r < new_camera_matrix.rows; ++r) {
            auto row = std::make_shared<ObservableList>();
            camera_matrix_state->push_back(row);
            for (auto c = 0; c < new_camera_matrix.cols; ++c) {
              row->push_back(new_camera_matrix.at<double>(r, c));
            }
          }

          auto distortion_parameters_state = std::make_shared<ObservableList>();
          for (auto dist_coef : new_distortion_coefficients) {
            distortion_parameters_state->push_back(dist_coef);
          }

          boost::asio::post(
              [c_state, camera_matrix_state, distortion_parameters_state] {
                (*c_state)["Camera Matrix"].assign(camera_matrix_state);
                (*c_state)["Distortion Coefficients"].assign(
                    distortion_parameters_state);
              });
        });
      }
    } else if (key_str == "Rows") {
      rows = size_t(std::get<long long int>(v));
    } else if (key_str == "Columns") {
      columns = size_t(std::get<long long int>(v));
    } else if (key_str == "Invert") {
      invert = std::get<bool>(v);
    } else if (key_str == "Camera Matrix") {
      auto v_rows = std::get<ObservableListPtr>(v);
      ObservableListPtr row0 = v_rows->at(0);
      ObservableListPtr row1 = v_rows->at(1);
      ObservableListPtr row2 = v_rows->at(2);
      auto update_matrix = [&](ObservableCollection::Action,
                               const ObservableCollection::Key &,
                               const ObservableCollection::Value &) {
        ObservableListPtr mat_rows = state->at("Camera Matrix");
        for (size_t r = 0; r < mat_rows->size(); ++r) {
          ObservableListPtr row = mat_rows->at(r);
          for (size_t c = 0; c < row->size(); ++c) {
            camera_matrix.at<double>(int(r), int(c)) = row->at(c);
          }
        }
        source_width = std::numeric_limits<size_t>::max();
        source_height = std::numeric_limits<size_t>::max();
      };
      mat_connection = v_rows->changed.connect(update_matrix);
      mat0_connection = row0->changed.connect(update_matrix);
      mat1_connection = row1->changed.connect(update_matrix);
      mat2_connection = row2->changed.connect(update_matrix);
      update_matrix(a, k, v);
    } else if (key_str == "Distortion Coefficients") {
      auto list = std::get<ObservableListPtr>(v);
      auto update_distortion = [&](ObservableCollection::Action,
                                   const ObservableCollection::Key &,
                                   const ObservableCollection::Value &) {
        ObservableListPtr dist_list = state->at("Distortion Coefficients");
        this->distortion_coefficients.clear();
        for (size_t i = 0; i < dist_list->size(); ++i) {
          distortion_coefficients.push_back(dist_list->at(i));
        }
        source_width = std::numeric_limits<size_t>::max();
        source_height = std::numeric_limits<size_t>::max();
      };
      distortion_connection = list->changed.connect(update_distortion);
      update_distortion(a, k, v);
    } else if (key_str == "Source") {
      std::string source_str = state->at("Source");
      auto token = std::string(absl::StripAsciiWhitespace(source_str));

      graph->get_node(token, [this, token](auto source) {
        auto locked_source = source.lock();
        if (!locked_source) {
          return;
        }

        if (node_cast<ImageNode *>(locked_source.get()) != nullptr) {
          image_source = node_cast<ImageNode *>(locked_source.get());
          source_connection =
              locked_source->ready.connect(std::bind(&Impl::on_data, this, _1));
        }
      });
    }
  }
};

DistortionNode::DistortionNode(ObservableDictPtr state,
                               boost::asio::io_context &io_context,
                               NodeGraph *graph)
    : impl(new Impl(state, io_context, this, graph)) {}

DistortionNode::~DistortionNode() {}

std::string DistortionNode::type_name() { return "DISTORTION"; }

ImageNode::Plane DistortionNode::plane(int) const {
  auto image = impl->current_result.image;
  return ImageNode::Plane(image.data, image.data + image.rows * image.cols *
                                                       image.channels());
}

size_t DistortionNode::num_planes() const { return 1; }

ImageNode::Format DistortionNode::format() const {
  return impl->current_result.image.channels() == 1 ? ImageNode::Format::Gray
                                                    : ImageNode::Format::RGB;
}

size_t DistortionNode::width() const {
  return size_t(impl->current_result.image.cols);
}

size_t DistortionNode::height() const {
  return size_t(impl->current_result.image.rows);
}

void DistortionNode::inject(const thalamus_grpc::Image &image) {
  auto const_data =
      reinterpret_cast<const unsigned char *>(image.data(0).data());
  auto data = const_cast<unsigned char *>(const_data);
  impl->current_result.image =
      cv::Mat(int(image.height()), int(image.width()), CV_8UC3, data);
  impl->current_result.interval =
      std::chrono::nanoseconds(image.frame_interval());
  impl->current_result.has_image = true;
  impl->current_result.has_analog = false;
  this->ready(this);
}

std::chrono::nanoseconds DistortionNode::frame_interval() const {
  return impl->current_result.interval;
}

std::chrono::nanoseconds DistortionNode::time() const {
  return impl->current_result.time;
}

bool DistortionNode::prepare() { return true; }

bool DistortionNode::has_image_data() const {
  return impl->current_result.has_image;
}

boost::json::value DistortionNode::process(const boost::json::value &) {
  return boost::json::value();
}

std::span<const double> DistortionNode::data(int) const {
  return std::span<const double>(&impl->current_result.latency,
                                 &impl->current_result.latency + 1);
}
int DistortionNode::num_channels() const { return 1; }
std::chrono::nanoseconds DistortionNode::sample_interval(int) const {
  return impl->current_result.interval;
}
std::string_view DistortionNode::name(int) const { return "Latency"; }
void DistortionNode::inject(
    const thalamus::vector<std::span<double const>> &data,
    const thalamus::vector<std::chrono::nanoseconds> &interval,
    const thalamus::vector<std::string_view> &) {
  THALAMUS_ASSERT(data.size() >= 1, "Error");
  THALAMUS_ASSERT(data[0].size() >= 1, "Error");
  THALAMUS_ASSERT(interval.size() >= 1, "Error");
  this->impl->current_result.latency = data[0][0];
  this->impl->current_result.has_analog = true;
  this->impl->current_result.has_image = false;
  this->impl->current_result.interval = interval[0];
  this->ready(this);
}
bool DistortionNode::has_analog_data() const {
  return impl->current_result.has_analog;
}
const cv::Mat &DistortionNode::camera_matrix() const {
  return impl->camera_matrix;
}
std::span<const double> DistortionNode::distortion_coefficients() const {
  return std::span<const double>(impl->distortion_coefficients.begin(),
                                 impl->distortion_coefficients.end());
}

size_t DistortionNode::modalities() const {
  return infer_modalities<DistortionNode>();
}
} // namespace thalamus
