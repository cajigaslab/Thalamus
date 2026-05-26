#include "thalamus/shared_library.hpp"
#include <thalamus/thordcx_node.hpp>
#include <thalamus/modalities_util.hpp>
#include <thalamus/thread_pool.hpp>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
#include <boost/pool/object_pool.hpp>
#include <uc480.h>
extern "C" {
#include <cairo.h>
}
#ifdef __clang__
#pragma clang diagnostic pop
#endif

namespace thalamus {
using namespace std::chrono_literals;

struct ThorDcxNode::Impl {
  boost::asio::io_context &io_context;
  ObservableDictPtr state;
  boost::signals2::scoped_connection state_connection;
  boost::signals2::scoped_connection options_connection;
  boost::signals2::scoped_connection source_connection;
  ImageNode *image_source;
  bool is_running = false;
  ThorDcxNode *outer;
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
  NodeGraph *graph;
  size_t threshold;
  size_t min_area;
  size_t max_area;
  double x_gain;
  double y_gain;
  bool invert_x;
  bool invert_y;
  size_t next_input_frame = 0;
  size_t next_output_frame = 0;

  ThreadPool &pool;
  boost::asio::steady_timer timer;

  struct DeleteCairoSurface {
    void operator()(cairo_surface_t *p) { cairo_surface_destroy(p); }
  };

  struct DeleteCairo {
    void operator()(cairo_t *p) { cairo_destroy(p); }
  };

  std::unique_ptr<cairo_surface_t, DeleteCairoSurface> surface;
  std::unique_ptr<cairo_t, DeleteCairo> cairo;

  int height = 512;
  int width = -1;
  int stride = -1;
  std::vector<unsigned char> cairo_data;

  int rows = 8;
  int columns = 8;

  struct Static {
    thalamus::SharedLibrary library;
    decltype(&::is_GetNumberOfCameras) is_GetNumberOfCameras;
    decltype(&::is_GetCameraList) is_GetCameraList;
    decltype(&::is_SetErrorReport) is_SetErrorReport;
    decltype(&::is_CameraStatus) is_CameraStatus;
    decltype(&::is_GetCameraInfo) is_GetCameraInfo;
    decltype(&::is_GetDLLVersion) is_GetDLLVersion;
    decltype(&::is_InitCamera) is_InitCamera;
    decltype(&::is_ExitCamera) is_ExitCamera;
    decltype(&::is_SetCameraID) is_SetCameraID;
    decltype(&::is_AllocImageMem) is_AllocImageMem;
    decltype(&::is_SetAllocatedImageMem) is_SetAllocatedImageMem;
    decltype(&::is_FreeImageMem) is_FreeImageMem;
    decltype(&::is_SetImageMem) is_SetImageMem;
    decltype(&::is_CopyImageMem) is_CopyImageMem;
    decltype(&::is_CopyImageMemLines) is_CopyImageMemLines;
    decltype(&::is_GetActiveImageMem) is_GetActiveImageMem;
    decltype(&::is_GetImageMem) is_GetImageMem;
    decltype(&::is_GetImageMemPitch) is_GetImageMemPitch;
    decltype(&::is_InquireImageMem) is_InquireImageMem;
  };

  static Static* _static;

  std::chrono::steady_clock::time_point last_saccade =
      std::chrono::steady_clock::now();

  Impl(ObservableDictPtr _state, boost::asio::io_context &_io_context,
       ThorDcxNode *_outer, NodeGraph *_graph)
      : io_context(_io_context), state(_state), outer(_outer), graph(_graph),
        pool(_graph->get_thread_pool()), timer(_io_context) {
    using namespace std::placeholders;
    state_connection =
        state->changed.connect(std::bind(&Impl::on_change, this, _1, _2, _3));
    this->state->recap(std::bind(&Impl::on_change, this, _1, _2, _3));
    // cairo_data.assign(stride*height, 0);
    // surface.reset(cairo_image_surface_create_for_data(cairo_data.data(),
    // CAIRO_FORMAT_A8, width, height, stride));
    // cairo.reset(cairo_create(surface.get()));
  }

  static void static_init() {
    if()
  }

  ~Impl() {
    (*state)["Running"].assign(false, [&] {});
  }

  void on_timer(const boost::system::error_code &error) {
    if (error.value() == boost::asio::error::operation_aborted) {
      return;
    }
    BOOST_ASSERT(!error);

    auto start = std::chrono::steady_clock::now();
    time = start.time_since_epoch();

    int square_size = height / rows;
    int new_width = square_size * columns;
    if (new_width != width) {
      stride = cairo_format_stride_for_width(CAIRO_FORMAT_A8, new_width);
      cairo_data.assign(size_t(stride * height), 0);
      surface.reset(cairo_image_surface_create_for_data(
          cairo_data.data(), CAIRO_FORMAT_A8, new_width, height, stride));
      cairo.reset(cairo_create(surface.get()));
    }
    width = new_width;

    cairo_set_operator(cairo.get(), CAIRO_OPERATOR_SOURCE);
    cairo_rectangle(cairo.get(), 0, 0, width, height);
    cairo_set_source_rgba(cairo.get(), 0, 0, 0, 0);
    cairo_fill(cairo.get());

    cairo_set_source_rgba(cairo.get(), 0, 0, 0, 1);
    for (auto y = 0; y < rows; ++y) {
      for (auto x = 0; x < columns; ++x) {
        if (y % 2) {
          if (x % 2) {
            cairo_rectangle(cairo.get(), x * square_size, y * square_size,
                            square_size, square_size);
            cairo_fill(cairo.get());
          }
        } else {
          if (!(x % 2)) {
            cairo_rectangle(cairo.get(), x * square_size, y * square_size,
                            square_size, square_size);
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
    if (elapsed < 16ms) {
      timer.expires_after(16ms - elapsed);
    } else {
      timer.expires_after(1ms);
    }

    timer.async_wait(std::bind(&Impl::on_timer, this, _1));
  }

  void on_change(ObservableCollection::Action,
                 const ObservableCollection::Key &k,
                 const ObservableCollection::Value &v) {
    auto key_str = std::get<std::string>(k);
    if (key_str == "Running") {
      is_running = std::get<bool>(v);
      timer.expires_after(16ms);
      timer.async_wait(std::bind(&Impl::on_timer, this, _1));
    } else if (key_str == "Height") {
      height = int(std::get<int64_t>(v));
    } else if (key_str == "Rows") {
      rows = int(std::get<int64_t>(v));
    } else if (key_str == "Columns") {
      columns = int(std::get<int64_t>(v));
    }
  }
};

ThorDcxNode::Impl::Static* ThorDcxNode::Impl::_static = nullptr;

ThorDcxNode::ThorDcxNode(ObservableDictPtr state,
                               boost::asio::io_context &io_context,
                               NodeGraph *graph)
    : impl(new Impl(state, io_context, this, graph)) {}

ThorDcxNode::~ThorDcxNode() {}

std::string ThorDcxNode::type_name() { return "THOR_DCX"; }

ImageNode::Plane ThorDcxNode::plane(int i) const {
  THALAMUS_ASSERT(i == 0, "Plane index out of bounds");
  return ImageNode::Plane(impl->cairo_data.begin(), impl->cairo_data.end());
}

size_t ThorDcxNode::num_planes() const { return 1; }

ImageNode::Format ThorDcxNode::format() const {
  return ImageNode::Format::Gray;
}

size_t ThorDcxNode::width() const { return size_t(impl->stride); }

size_t ThorDcxNode::height() const { return size_t(impl->height); }

void ThorDcxNode::inject(const thalamus_grpc::Image &) {
  THALAMUS_ASSERT(false, "Unimplemented");
}

std::chrono::nanoseconds ThorDcxNode::time() const { return impl->time; }

std::chrono::nanoseconds ThorDcxNode::frame_interval() const { return 16ms; }

bool ThorDcxNode::prepare() {
  SharedLibrary library("uc480_64");
  if(!library.is_valid()) {
    THALAMUS_LOG(info) << "uc480_64 shared library not found";
    return false;
  }

  Impl::_static = new Impl::Static();
  Impl::_static->is_GetNumberOfCameras = library.load<decltype(&::is_GetNumberOfCameras)>("is_GetNumberOfCameras");
  if(Impl::_static->is_GetNumberOfCameras == nullptr) { THALAMUS_LOG(info) << "Failed to load is_GetNumberOfCameras"; return false; }
  Impl::_static->is_GetCameraList = library.load<decltype(&::is_GetCameraList)>("is_GetCameraList");
  if(Impl::_static->is_GetCameraList == nullptr) { THALAMUS_LOG(info) << "Failed to load is_GetCameraList"; return false; }
  Impl::_static->is_SetErrorReport = library.load<decltype(&::is_SetErrorReport)>("is_SetErrorReport");
  if(Impl::_static->is_SetErrorReport == nullptr) { THALAMUS_LOG(info) << "Failed to load is_SetErrorReport"; return false; }
  Impl::_static->is_CameraStatus = library.load<decltype(&::is_CameraStatus)>("is_CameraStatus");
  if(Impl::_static->is_CameraStatus == nullptr) { THALAMUS_LOG(info) << "Failed to load is_CameraStatus"; return false; }
  Impl::_static->is_GetCameraInfo = library.load<decltype(&::is_GetCameraInfo)>("is_GetCameraInfo");
  if(Impl::_static->is_GetCameraInfo == nullptr) { THALAMUS_LOG(info) << "Failed to load is_GetCameraInfo"; return false; }
  Impl::_static->is_GetDLLVersion = library.load<decltype(&::is_GetDLLVersion)>("is_GetDLLVersion");
  if(Impl::_static->is_GetDLLVersion == nullptr) { THALAMUS_LOG(info) << "Failed to load is_GetDLLVersion"; return false; }
  Impl::_static->is_InitCamera = library.load<decltype(&::is_InitCamera)>("is_InitCamera");
  if(Impl::_static->is_InitCamera == nullptr) { THALAMUS_LOG(info) << "Failed to load is_InitCamera"; return false; }
  Impl::_static->is_ExitCamera = library.load<decltype(&::is_ExitCamera)>("is_ExitCamera");
  if(Impl::_static->is_ExitCamera == nullptr) { THALAMUS_LOG(info) << "Failed to load is_ExitCamera"; return false; }
  Impl::_static->is_SetCameraID = library.load<decltype(&::is_SetCameraID)>("is_SetCameraID");
  if(Impl::_static->is_SetCameraID == nullptr) { THALAMUS_LOG(info) << "Failed to load is_SetCameraID"; return false; }
  Impl::_static->is_AllocImageMem = library.load<decltype(&::is_AllocImageMem)>("is_AllocImageMem");
  if(Impl::_static->is_AllocImageMem == nullptr) { THALAMUS_LOG(info) << "Failed to load is_AllocImageMem"; return false; }
  Impl::_static->is_SetAllocatedImageMem = library.load<decltype(&::is_SetAllocatedImageMem)>("is_SetAllocatedImageMem");
  if(Impl::_static->is_SetAllocatedImageMem == nullptr) { THALAMUS_LOG(info) << "Failed to load is_SetAllocatedImageMem"; return false; }
  Impl::_static->is_FreeImageMem = library.load<decltype(&::is_FreeImageMem)>("is_FreeImageMem");
  if(Impl::_static->is_FreeImageMem == nullptr) { THALAMUS_LOG(info) << "Failed to load is_FreeImageMem"; return false; }
  Impl::_static->is_SetImageMem = library.load<decltype(&::is_SetImageMem)>("is_SetImageMem");
  if(Impl::_static->is_SetImageMem == nullptr) { THALAMUS_LOG(info) << "Failed to load is_SetImageMem"; return false; }
  Impl::_static->is_CopyImageMem = library.load<decltype(&::is_CopyImageMem)>("is_CopyImageMem");
  if(Impl::_static->is_CopyImageMem == nullptr) { THALAMUS_LOG(info) << "Failed to load is_CopyImageMem"; return false; }
  Impl::_static->is_CopyImageMemLines = library.load<decltype(&::is_CopyImageMemLines)>("is_CopyImageMemLines");
  if(Impl::_static->is_CopyImageMemLines == nullptr) { THALAMUS_LOG(info) << "Failed to load is_CopyImageMemLines"; return false; }
  Impl::_static->is_GetActiveImageMem = library.load<decltype(&::is_GetActiveImageMem)>("is_GetActiveImageMem");
  if(Impl::_static->is_GetActiveImageMem == nullptr) { THALAMUS_LOG(info) << "Failed to load is_GetActiveImageMem"; return false; }
  Impl::_static->is_GetImageMem = library.load<decltype(&::is_GetImageMem)>("is_GetImageMem");
  if(Impl::_static->is_GetImageMem == nullptr) { THALAMUS_LOG(info) << "Failed to load is_GetImageMem"; return false; }
  Impl::_static->is_GetImageMemPitch = library.load<decltype(&::is_GetImageMemPitch)>("is_GetImageMemPitch");
  if(Impl::_static->is_GetImageMemPitch == nullptr) { THALAMUS_LOG(info) << "Failed to load is_GetImageMemPitch"; return false; }
  Impl::_static->is_InquireImageMem = library.load<decltype(&::is_InquireImageMem)>("is_InquireImageMem");
  if(Impl::_static->is_InquireImageMem == nullptr) { THALAMUS_LOG(info) << "Failed to load is_InquireImageMem"; return false; }

  int num_cameras;
  if(Impl::_static->is_GetNumberOfCameras(&num_cameras) != IS_SUCCESS) {
    THALAMUS_LOG(error) << "is_GetNumberOfCameras failed";
  }

  if(num_cameras == 0) {
    THALAMUS_LOG(error) << "No Thor DCX cameras found";
    return false;
  }

  auto camera_list_memory = new uint8_t[sizeof(UC480_CAMERA_LIST) + (num_cameras-1)*sizeof(UC480_CAMERA_INFO)];
  auto camera_list = new (camera_list_memory) UC480_CAMERA_LIST();
  camera_list->dwCount = num_cameras;

  if(Impl::_static->is_GetCameraList(camera_list) != IS_SUCCESS) {
    THALAMUS_LOG(error) << "is_GetCameraList (count) failed";
  }

  for(auto i = 0;i < num_cameras;++i) {
    
  }

  return true;
}

bool ThorDcxNode::has_image_data() const { return true; }

boost::json::value ThorDcxNode::process(const boost::json::value &) {
  return boost::json::value();
}

size_t ThorDcxNode::modalities() const {
  return infer_modalities<ThorDcxNode>();
}
} // namespace thalamus
