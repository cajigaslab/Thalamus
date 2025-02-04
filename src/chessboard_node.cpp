#include <chessboard_node.hpp>
#include <thread_pool.hpp>
#include <modalities_util.hpp>

#ifdef __clang__
  #pragma clang diagnostic push
  #pragma clang diagnostic ignored "-Weverything"
#endif
#include <boost/pool/object_pool.hpp>
extern "C" {
#include <cairo.h>
}
#ifdef __clang__
  #pragma clang diagnostic pop
#endif
 
namespace thalamus {
  using namespace std::chrono_literals;

  struct ChessBoardNode::Impl {
    boost::asio::io_context& io_context;
    ObservableDictPtr state;
    boost::signals2::scoped_connection state_connection;
    boost::signals2::scoped_connection options_connection;
    boost::signals2::scoped_connection source_connection;
    ImageNode* image_source;
    bool is_running = false;
    ChessBoardNode* outer;
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

    std::unique_ptr<cairo_surface_t, DeleteCairoSurface> surface;
    std::unique_ptr<cairo_t, DeleteCairo> cairo;

    int height = 512;
    int width = -1;
    int stride = -1;
    std::vector<unsigned char> cairo_data;

    int rows = 8;
    int columns = 8;

    std::chrono::steady_clock::time_point last_saccade = std::chrono::steady_clock::now();

    Impl(ObservableDictPtr _state, boost::asio::io_context& _io_context, ChessBoardNode* _outer, NodeGraph* _graph)
      : io_context(_io_context)
      , state(_state)
      , outer(_outer)
      , graph(_graph)
      , pool(_graph->get_thread_pool())
      , timer(_io_context) {
      using namespace std::placeholders;
      state_connection = state->changed.connect(std::bind(&Impl::on_change, this, _1, _2, _3));
      this->state->recap(std::bind(&Impl::on_change, this, _1, _2, _3));
      //cairo_data.assign(stride*height, 0);
      //surface.reset(cairo_image_surface_create_for_data(cairo_data.data(), CAIRO_FORMAT_A8, width, height, stride));
      //cairo.reset(cairo_create(surface.get()));
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
      time = start.time_since_epoch();

      int square_size = height/rows;
      int new_width = square_size*columns;
      if(new_width != width) {
        stride = cairo_format_stride_for_width(CAIRO_FORMAT_A8, new_width);
        cairo_data.assign(size_t(stride*height), 0);
        surface.reset(cairo_image_surface_create_for_data(cairo_data.data(), CAIRO_FORMAT_A8, new_width, height, stride));
        cairo.reset(cairo_create(surface.get()));
      }
      width = new_width;

      cairo_set_operator(cairo.get(), CAIRO_OPERATOR_SOURCE);
      cairo_rectangle(cairo.get(), 0, 0, width, height);
      cairo_set_source_rgba(cairo.get(), 0, 0, 0, 0);
      cairo_fill(cairo.get());

      cairo_set_source_rgba(cairo.get(), 0, 0, 0, 1);
      for(auto y = 0;y < rows;++y) {
        for(auto x = 0;x < columns;++x) {
          if(y % 2) {
            if(x % 2) {
              cairo_rectangle(cairo.get(), x*square_size, y*square_size, square_size, square_size);
              cairo_fill(cairo.get());
            }
          } else {
            if(!(x % 2)) {
              cairo_rectangle(cairo.get(), x*square_size, y*square_size, square_size, square_size);
              cairo_fill(cairo.get());
            }
          }
        }
      }

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
      } else if(key_str == "Height") {
        height = int(std::get<long long>(v));
      } else if(key_str == "Rows") {
        rows = int(std::get<long long>(v));
      } else if(key_str == "Columns") {
        columns = int(std::get<long long>(v));
      }
    }
  };

  ChessBoardNode::ChessBoardNode(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph* graph)
    : impl(new Impl(state, io_context, this, graph)) {}

  ChessBoardNode::~ChessBoardNode() {}

  std::string ChessBoardNode::type_name() {
    return "CHESSBOARD";
  }

  ImageNode::Plane ChessBoardNode::plane(int i) const {
    THALAMUS_ASSERT(i == 0);
    return ImageNode::Plane(impl->cairo_data.begin(), impl->cairo_data.end());
  }

  size_t ChessBoardNode::num_planes() const {
    return 1;
  }

  ImageNode::Format ChessBoardNode::format() const {
    return ImageNode::Format::Gray;
  }

  size_t ChessBoardNode::width() const {
    return size_t(impl->stride);
  }

  size_t ChessBoardNode::height() const {
    return size_t(impl->height);
  }

  void ChessBoardNode::inject(const thalamus_grpc::Image&) {
    THALAMUS_ASSERT(false);
  }

  std::chrono::nanoseconds ChessBoardNode::time() const {
    return impl->time;
  }

  std::chrono::nanoseconds ChessBoardNode::frame_interval() const {
    return 16ms;
  }

  bool ChessBoardNode::prepare() {
    return true;
  }

  bool ChessBoardNode::has_image_data() const {
    return true;
  }

  boost::json::value ChessBoardNode::process(const boost::json::value&) {
    return boost::json::value();
  }

  size_t ChessBoardNode::modalities() const { return infer_modalities<ChessBoardNode>(); }
}
