#include <oculomatic_node.h>
#include <thread_pool.h>
#include <boost/pool/object_pool.hpp>

#include "opencv2/core.hpp"
#include "opencv2/imgproc.hpp"
#include "opencv2/imgcodecs.hpp"
#include <modalities_util.h>

namespace thalamus {
  using namespace std::chrono_literals;

  static const double AOUT_MIN = -10;
  static const double AOUT_MAX = 10;
  static const double AOUT_RANGE = AOUT_MAX - AOUT_MIN;

  struct OculomaticNode::Impl {
    boost::asio::io_context& io_context;
    ObservableDictPtr state;
    boost::signals2::scoped_connection state_connection;
    boost::signals2::scoped_connection options_connection;
    boost::signals2::scoped_connection source_connection;
    ImageNode* image_source;
    bool is_running = false;
    OculomaticNode* outer;
    std::chrono::nanoseconds time;
    std::thread oculomatic_thread;
    bool running = false;
    bool computing = false;
    thalamus_grpc::Image image;
    std::atomic_bool frame_pending;
    std::vector<unsigned char> intermediate;
    thalamus::vector<Plane> data;
    Format format;
    size_t width;
    size_t height;
    bool need_recenter;
    std::pair<double, double> centering_offset;
    std::pair<int, int> centering_pix;
    NodeGraph* graph;
    size_t threshold;
    size_t min_area;
    size_t max_area;
    double x_gain;
    double y_gain;
    bool invert_x;
    bool invert_y;
    size_t next_input_frame = 0;
    size_t next_output_frame = 0;
    std::set<cv::Mat*> mat_pool;

    struct Result {
      double x;
      double y;
      double diameter;
      cv::Mat image;
      bool has_image;
      bool has_analog;
      std::chrono::nanoseconds interval;
    };
    std::map<size_t, Result> output_frames;
    Result current_result;
    ThreadPool& pool;

    Impl(ObservableDictPtr state, boost::asio::io_context& io_context, OculomaticNode* outer, NodeGraph* graph)
      : io_context(io_context)
      , state(state)
      , outer(outer)
      , graph(graph)
      , current_result(Result{0, 0, 0, cv::Mat(), false, false, 0ns})
      , pool(graph->get_thread_pool()) {
      using namespace std::placeholders;
      state_connection = state->changed.connect(std::bind(&Impl::on_change, this, _1, _2, _3));
      this->state->recap(std::bind(&Impl::on_change, this, _1, _2, _3));
    }

    ~Impl() {
      (*state)["Running"].assign(false, [&] {});
      for(auto mat : mat_pool) {
        delete mat;
      }
    }

    void stop() {
      if(oculomatic_thread.joinable()) {
        oculomatic_thread.join();
      }
    }

    static std::pair<double, double> normalize_center(
      int pix_x, int pix_y, 
      const std::pair<int, int>& dimensions,
      const std::pair<int, int>& centering_pix,
      const std::pair<double, double>& centering_offset,
      double x_gain,
      double y_gain,
      bool invert_x,
      bool invert_y) {
        auto [center_pix_x, center_pix_y] = centering_pix;
        auto [center_offset_x, center_offset_y] = centering_offset;
    
        std::pair<double, double> result;
        auto x_denominator = dimensions.first - 2 * x_gain;
        auto y_denominator = dimensions.second - 2 * y_gain;
        x_denominator = x_denominator == 0 ? dimensions.first - 2 * x_gain : x_denominator;
        y_denominator = y_denominator == 0 ? dimensions.second - 2 * y_gain : y_denominator;
        if(invert_x && invert_y) {
          result = std::make_pair((center_pix_x - pix_x) / x_denominator*AOUT_RANGE + AOUT_MIN - center_offset_x,
                                  (center_pix_y - pix_y) / y_denominator*AOUT_RANGE + AOUT_MIN - center_offset_y);
        } else if( invert_x && ! invert_y) {
          result = std::make_pair((center_pix_x - pix_x) / x_denominator*AOUT_RANGE + AOUT_MIN - center_offset_x,
                                  (pix_y - center_pix_y) / y_denominator*AOUT_RANGE + AOUT_MIN - center_offset_y);
        } else if( ! invert_x && invert_y) {
          result = std::make_pair((pix_x - center_pix_x) / x_denominator*AOUT_RANGE + AOUT_MIN - center_offset_x,
                                  (center_pix_y - pix_y) / y_denominator*AOUT_RANGE + AOUT_MIN - center_offset_y);
        } else {
          result = std::make_pair((pix_x - center_pix_x) / x_denominator*AOUT_RANGE + AOUT_MIN - center_offset_x,
                                  (pix_y - center_pix_y) / y_denominator*AOUT_RANGE + AOUT_MIN - center_offset_y);
        }
    
        return result;
    }

    void on_data(Node*) {
      if(image_source->format() != ImageNode::Format::Gray || pool.full()) {
        return;
      }

      unsigned char* data = const_cast<unsigned char*>(image_source->plane(0).data());
      int width = image_source->width();
      int height = image_source->height();
      auto frame_interval = image_source->frame_interval();

      auto need_recenter = this->need_recenter;
      this->need_recenter = false;
      if(mat_pool.empty()) {
        mat_pool.insert(new cv::Mat());
      }
      auto out = *mat_pool.begin();
      mat_pool.erase(out);

      cv::Mat in = cv::Mat(height, width, CV_8UC1, data).clone();
      pool.push([width,
                  height,
                  frame_id=next_input_frame++,
                  need_recenter,
                  out,
                  frame_interval,
                  &current_result=current_result,
                  &mat_pool=this->mat_pool,
                  &output_frames=this->output_frames,
                  &next_output_frame=this->next_output_frame,
                  &io_context=io_context,
                  centering_pix=this->centering_pix,
                  centering_offset=this->centering_offset,
                  &ref_centering_pix=this->centering_pix,
                  &ref_centering_offset=this->centering_offset,
                  x_gain=this->x_gain,
                  y_gain=this->y_gain,
                  computing=this->computing,
                  min_area=this->min_area,
                  max_area=this->max_area,
                  invert_x=this->invert_x,
                  invert_y=this->invert_y,
                  threshold=this->threshold,
                  in=std::move(in),
                  outer=outer->shared_from_this()] {
        std::vector<std::vector<cv::Point> > contours;
        std::vector<cv::Vec4i> hierarchy;
        

        cv::threshold(in, in, threshold, 255, cv::THRESH_BINARY_INV);
        cv::cvtColor(in, *out, cv::COLOR_GRAY2RGB);
        if(!computing) {
          boost::asio::post(io_context, [&current_result,&next_output_frame,&output_frames,&mat_pool,out,frame_id,outer,frame_interval] {
            output_frames[frame_id] = Result{ 0, 0, 0, *out, true, false, frame_interval };
            mat_pool.insert(out);
            for(auto i = output_frames.begin();i != output_frames.end();) {
              if(i->first == next_output_frame) {
                ++next_output_frame;
                current_result = i->second;
                outer->ready(outer.get());
                i = output_frames.erase(i);
              } else {
                ++i;
              }
            }
          });
          return;
        }
        cv::findContours(in, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
        std::vector<double> areas(contours.size(), 0);
        std::transform(contours.begin(), contours.end(), areas.begin(), [&](const std::vector<cv::Point>& contour) {
          return cv::contourArea(contour);
        });
      
        auto frame_size = width*height;
        auto min_area_pixels = min_area*frame_size/100;
        auto max_area_pixels = max_area*frame_size/100;

        int selected = -1;
        for(size_t i = 0;i < contours.size();++i) {
          if(areas[i] <= max_area_pixels && min_area_pixels <= areas[i] && (selected == -1 || areas[i] > areas[selected])) {
            selected = i;
          }
        }
      
        auto diameter = 0.0;
        std::pair<double, double> gaze;
        if (selected > -1) {
          auto m = cv::moments(contours[selected]);
          auto center = std::make_pair(static_cast<int>(m.m10/(m.m00+1e-6)), static_cast<int>(m.m01/(m.m00+1e-6)));
          if(need_recenter) {
            auto new_centering_offset = std::pair<double, double>(0.0, 0.0);
            auto new_centering_pix = std::pair<int, int>(center.first, center.second);
            new_centering_offset = normalize_center(center.first, 
              center.second, std::make_pair(width, height), 
              new_centering_pix, 
              new_centering_offset,
              x_gain, y_gain, invert_x, invert_y);
            boost::asio::post(io_context, [new_centering_pix, new_centering_offset, &ref_centering_pix, &ref_centering_offset] {
              ref_centering_pix = new_centering_pix;
              ref_centering_offset = new_centering_offset;
            });
          }
          gaze = normalize_center(center.first, 
              center.second, std::make_pair(width, height), 
              centering_pix, 
              centering_offset,
              x_gain, y_gain, invert_x, invert_y);
          auto bounds = cv::boundingRect(contours[selected]);
          diameter = bounds.width;
             
          cv::drawContours(*out, contours, selected, cv::Scalar(255, 0, 0));
          cv::circle(*out, cv::Point(static_cast<int>(center.first), static_cast<int>(center.second)), 10,
                     cv::Scalar(0, 0, 255), -1);
        } else {
          gaze = std::make_pair(1e6, 1e6);
        } 

        boost::asio::post(io_context, [&current_result,&next_output_frame,&output_frames,&mat_pool,out,frame_id,diameter,gaze,outer,frame_interval] {
          output_frames[frame_id] = Result{ gaze.first, gaze.second, diameter, *out, true, true, frame_interval };
          mat_pool.insert(out);
          for(auto i = output_frames.begin();i != output_frames.end();) {
            if(i->first == next_output_frame) {
              ++next_output_frame;
              current_result = i->second;
              outer->ready(outer.get());
              i = output_frames.erase(i);
            } else {
              ++i;
            }
          }
        });
      });
    }

    void on_change(ObservableCollection::Action, const ObservableCollection::Key& k, const ObservableCollection::Value& v) {
      auto key_str = std::get<std::string>(k);
      if(key_str == "Computing") {
        computing = std::get<bool>(v);
      } else if(key_str == "Threshold") {
        threshold = std::get<long long int>(v);
      } else if(key_str == "Min Area") {
        min_area = std::get<long long int>(v);
      } else if(key_str == "Max Area") {
        max_area = std::get<long long int>(v);
      } else if(key_str == "X Gain") {
        x_gain = std::get<double>(v);
      } else if(key_str == "Y Gain") {
        y_gain = std::get<double>(v);
      } else if(key_str == "Invert X") {
        invert_x = std::get<bool>(v);
      } else if(key_str == "Invert Y") {
        invert_y = std::get<bool>(v);
      } else if(key_str == "Source") {
        std::string source_str = state->at("Source");
        auto token = std::string(absl::StripAsciiWhitespace(source_str));

        graph->get_node(token, [this,token](auto source) {
          auto locked_source = source.lock();
          if (!locked_source) {
            return;
          }
          
          if (node_cast<ImageNode*>(locked_source.get()) != nullptr) {
            image_source = node_cast<ImageNode*>(locked_source.get());
            source_connection = locked_source->ready.connect(std::bind(&Impl::on_data, this, _1));
          }
        });
      }
    }
  };

  OculomaticNode::OculomaticNode(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph* graph)
    : impl(new Impl(state, io_context, this, graph)) {}

  OculomaticNode::~OculomaticNode() {}

  std::string OculomaticNode::type_name() {
    return "OCULOMATIC";
  }

  ImageNode::Plane OculomaticNode::plane(int) const {
    auto image = impl->current_result.image;
    return ImageNode::Plane(image.data, image.data+image.rows*image.cols*image.channels());
  }

  size_t OculomaticNode::num_planes() const {
    return 1;
  }

  ImageNode::Format OculomaticNode::format() const {
    return impl->current_result.image.channels() == 1 ? ImageNode::Format::Gray : ImageNode::Format::RGB;
  }

  size_t OculomaticNode::width() const {
    return impl->current_result.image.cols;
  }

  size_t OculomaticNode::height() const {
    return impl->current_result.image.rows;
  }

  void OculomaticNode::inject(const thalamus_grpc::Image& image) {
    auto const_data = reinterpret_cast<const unsigned char*>(image.data(0).data());
    auto data = const_cast<unsigned char*>(const_data);
    impl->current_result.image = cv::Mat(image.height(), image.width(), CV_8UC3, data);
    impl->current_result.has_analog = false;
    impl->current_result.has_image = true;
    impl->current_result.interval = std::chrono::nanoseconds(image.frame_interval());
    this->ready(this);
  }

  std::chrono::nanoseconds OculomaticNode::time() const {
    return impl->time;
  }

  std::span<const double> OculomaticNode::data(int channel) const {
    switch(channel) {
      case 0: return std::span<const double>(&impl->current_result.x, &impl->current_result.x+1);
      case 1: return std::span<const double>(&impl->current_result.y, &impl->current_result.y+1);
      case 2: return std::span<const double>(&impl->current_result.diameter, &impl->current_result.diameter+1);
      default: return std::span<const double>();
    }
  }

  int OculomaticNode::num_channels() const {
    return 3;
  }

  std::chrono::nanoseconds OculomaticNode::sample_interval(int) const {
    return impl->current_result.interval;
  }

  std::chrono::nanoseconds OculomaticNode::frame_interval() const {
    return impl->current_result.interval;
  }

  static const std::string EMPTY = "";
  static const std::string X = "X";
  static const std::string Y = "Y";
  static const std::string DIAMETER = "Diameter";
  static std::vector<std::string> names = {X, Y, DIAMETER};

  std::string_view OculomaticNode::name(int channel) const {
    switch(channel) {
      case 0: return X;
      case 1: return Y;
      case 2: return DIAMETER;
      default: return thalamus::EMPTY;
    }
  }

  void OculomaticNode::inject(const thalamus::vector<std::span<double const>>& data, const thalamus::vector<std::chrono::nanoseconds>& interval, const thalamus::vector<std::string_view>&) {
    THALAMUS_ASSERT(data.size() >= 3);
    THALAMUS_ASSERT(data[0].size() >= 1);
    THALAMUS_ASSERT(data[1].size() >= 1);
    THALAMUS_ASSERT(data[2].size() >= 1);
    THALAMUS_ASSERT(interval.size() >= 1);
    this->impl->current_result.x = data[0][0];
    this->impl->current_result.y = data[1][0];
    this->impl->current_result.diameter = data[2][0];
    this->impl->current_result.has_analog = true;
    this->impl->current_result.has_image = false;
    this->impl->current_result.interval = interval[0];
    this->ready(this);
  }

  bool OculomaticNode::prepare() {
    return true;
  }

  bool OculomaticNode::has_image_data() const {
    return impl->current_result.has_image;
  }

  bool OculomaticNode::has_analog_data() const {
    return impl->current_result.has_analog;
  }

  std::span<const std::string> OculomaticNode::get_recommended_channels() const {
    return std::span<const std::string>(names.begin(), names.end());
  }

  boost::json::value OculomaticNode::process(const boost::json::value&) {
    impl->need_recenter = true;
    return boost::json::value();
  }
  size_t OculomaticNode::modalities() const { return infer_modalities<OculomaticNode>(); }
}
