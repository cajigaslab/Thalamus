#include <pupil_node.h>
#include <thread_pool.h>
#include <boost/pool/object_pool.hpp>
#include <cairo.h>

namespace thalamus {
  using namespace std::chrono_literals;

  struct PupilNode::Impl {
    boost::asio::io_context& io_context;
    ObservableDictPtr state;
    boost::signals2::scoped_connection state_connection;
    boost::signals2::scoped_connection options_connection;
    boost::signals2::scoped_connection source_connection;
    ImageNode* image_source;
    bool is_running = false;
    PupilNode* outer;
    std::chrono::nanoseconds time;
    std::thread pupil_thread;
    bool running = false;
    bool computing = false;
    thalamus_grpc::Image image;
    std::atomic_bool frame_pending;
    std::vector<unsigned char> intermediate;
    thalamus::vector<Plane> data;
    Format format;
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

    ThreadPool& pool;
    boost::asio::steady_timer timer;

    struct DeleteCairoSurface {
      void operator()(cairo_surface_t* p) {
        cairo_surface_destroy(p);
      }
    };

    struct DeleteCairo {
      void operator()(cairo_t* p) {
        cairo_destroy(p);
      }
    };

    struct DeleteCairoPattern {
      void operator()(cairo_pattern_t* p) {
        cairo_pattern_destroy(p);
      }
    };

    std::unique_ptr<cairo_surface_t, DeleteCairoSurface> surface;
    std::unique_ptr<cairo_t, DeleteCairo> cairo;
    std::unique_ptr<cairo_pattern_t, DeleteCairoPattern> pattern;

    int width = 512;
    int height = 512;
    int stride = cairo_format_stride_for_width(CAIRO_FORMAT_A8, width);
    std::vector<unsigned char> cairo_data;

    double x = 256;
    double y = 256;
    int target_x = 256;
    int target_y = 256;

    std::chrono::steady_clock::time_point last_saccade = std::chrono::steady_clock::now();

    Impl(ObservableDictPtr state, boost::asio::io_context& io_context, PupilNode* outer, NodeGraph* graph)
      : io_context(io_context)
      , state(state)
      , outer(outer)
      , graph(graph)
      , pool(graph->get_thread_pool())
      , timer(io_context) {
      using namespace std::placeholders;
      state_connection = state->changed.connect(std::bind(&Impl::on_change, this, _1, _2, _3));
      this->state->recap(std::bind(&Impl::on_change, this, _1, _2, _3));
      cairo_data.assign(stride*height, 0);
      surface.reset(cairo_image_surface_create_for_data(cairo_data.data(), CAIRO_FORMAT_A8, width, height, stride));
      cairo.reset(cairo_create(surface.get()));
      pattern.reset(cairo_pattern_create_radial(0, 0, 8, 0, 0, 64));
      cairo_pattern_add_color_stop_rgba(pattern.get(), 0, 0, 0, 0, 0);
      cairo_pattern_add_color_stop_rgba(pattern.get(), 1, 0, 0, 0, 1);
    }

    ~Impl() {
      (*state)["Running"].assign(false, [&] {});
    }

    void on_timer(const boost::system::error_code& error) {
      if (error.value() == boost::asio::error::operation_aborted) {
        return;
      }
      BOOST_ASSERT(!error);

      auto start = std::chrono::steady_clock::now();
      if(start - last_saccade > 1s) {
        target_x = (rand() % (512-128)) + 64;
        target_y = (rand() % (512-128)) + 64;
        last_saccade = start;
      }
      x += (target_x - x)/3;
      y += (target_y - y)/3;
      time = start.time_since_epoch();

      cairo_identity_matrix(cairo.get());
      cairo_set_operator(cairo.get(), CAIRO_OPERATOR_SOURCE);
      cairo_rectangle(cairo.get(), 0, 0, width, height);
      cairo_set_source_rgba(cairo.get(), 0, 0, 0, 1);
      cairo_fill(cairo.get());

      cairo_translate(cairo.get(), x, y);
      cairo_set_source(cairo.get(), pattern.get());
      cairo_arc(cairo.get(), 0, 0, 128, 0, 2*M_PI);
      cairo_fill(cairo.get());

      outer->ready(outer);

      if (!is_running) {
        return;
      }

      auto end = std::chrono::steady_clock::now();
      auto elapsed = end - start;
      if(elapsed < 16ms) {
        timer.expires_after(16ms - elapsed);
      } else {
        timer.expires_after(1ms);
      }

      timer.async_wait(std::bind(&Impl::on_timer, this, _1));
    }

    void on_change(ObservableCollection::Action, const ObservableCollection::Key& k, const ObservableCollection::Value& v) {
      auto key_str = std::get<std::string>(k);
      if(key_str == "Running") {
        is_running = std::get<bool>(v);
        timer.expires_after(16ms);
        timer.async_wait(std::bind(&Impl::on_timer, this, _1));
      }
    }
  };

  PupilNode::PupilNode(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph* graph)
    : impl(new Impl(state, io_context, this, graph)) {}

  PupilNode::~PupilNode() {}

  std::string PupilNode::type_name() {
    return "PUPIL";
  }

  ImageNode::Plane PupilNode::plane(int i) const {
    THALAMUS_ASSERT(i == 0);
    return ImageNode::Plane(impl->cairo_data.begin(), impl->cairo_data.end());
  }

  size_t PupilNode::num_planes() const {
    return 1;
  }

  ImageNode::Format PupilNode::format() const {
    return ImageNode::Format::Gray;
  }

  size_t PupilNode::width() const {
    return impl->width;
  }

  size_t PupilNode::height() const {
    return impl->height;
  }

  void PupilNode::inject(const thalamus_grpc::Image&) {
    THALAMUS_ASSERT(false);
  }

  std::chrono::nanoseconds PupilNode::time() const {
    return impl->time;
  }

  std::chrono::nanoseconds PupilNode::frame_interval() const {
    return 16ms;
  }

  bool PupilNode::prepare() {
    return true;
  }

  bool PupilNode::has_image_data() const {
    return true;
  }

  boost::json::value PupilNode::process(const boost::json::value&) {
    return boost::json::value();
  }
}
