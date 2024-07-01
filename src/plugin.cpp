//#include <QApplication>
//#include <QScreen>
#include "node_graph_impl.h"
#include <boost/asio.hpp>
#include <boost/program_options.hpp>
#include <state.h>
#include <future>
#ifdef _WIN32
#include <timeapi.h>
#endif
#include <thalamus_config.h>
#include <fstream>
#include <chrono>
#include <absl/strings/str_format.h>
#include <absl/time/time.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

#include "grpc_impl.h"
#include <tracing/systemclock.h>
#include <format>
#include <boost/log/trivial.hpp>
#include <thalamus/file.h>

#ifdef __clang__
  #pragma clang diagnostic push
  #pragma clang diagnostic ignored "-Wnested-anon-types"
  #pragma clang diagnostic ignored "-Wgnu-anonymous-struct"
  #pragma clang diagnostic ignored "-Wlanguage-extension-token"
    #include <boost/log/utility/setup/console.hpp>
  #pragma clang diagnostic pop
#else
  #include <boost/log/utility/setup/console.hpp>
#endif


#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#ifdef __clang__
  #pragma clang diagnostic push
  #pragma clang diagnostic ignored "-Wmicrosoft-cpp-macro"
    #include <boost/log/expressions.hpp>
  #pragma clang diagnostic pop
#else
  #include <boost/log/expressions.hpp>
#endif

#include <boost/log/support/date_time.hpp>

#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif

namespace thalamus { extern int main(int argc, char ** argv); }
namespace hydrate { extern int main(int argc, char ** argv); }

extern "C" {
  static std::unique_ptr<thalamus::NodeGraphImpl> node_graph;
  static std::unique_ptr<thalamus::Service> service;
  static std::unique_ptr<grpc::Server> server;
  static boost::asio::io_context io_context;
  using WorkGuard = boost::asio::executor_work_guard<decltype(io_context.get_executor())>;
  static std::unique_ptr<WorkGuard> work_guard;
  static bool trace = false;
  static std::thread grpc_thread;
  static std::thread main_thread;
  static std::shared_ptr<thalamus::AnalogNode> analog_node;

  EXPORT int thalamus_start(const char* config_filename, const char* target_node, int port, bool trace) {
#ifdef _WIN32
    timeBeginPeriod(1);
#endif
    std::srand(std::time(nullptr));
    ::trace = trace;
    port = port ? port : 50050;

    auto steady_start = std::chrono::steady_clock::now();
    auto system_start = std::chrono::system_clock::now();
 
    const auto start_time = absl::FromChrono(system_start);
    auto start_time_str = absl::FormatTime("%Y%m%dT%H%M%SZ%z", start_time, absl::LocalTimeZone());
    auto log_filename = absl::StrFormat("thalamus_%s.log", start_time_str);
    auto log_absolute_filename = thalamus::get_home() / std::filesystem::path(log_filename);
    auto good_log_file = thalamus::can_write_file(log_absolute_filename);
    std::cout << "Log file: " << log_absolute_filename.string() << " " << std::filesystem::exists(log_absolute_filename) << std::endl;

    auto format = (
      boost::log::expressions::stream <<
      "[" << boost::log::expressions::format_date_time< boost::posix_time::ptime >("TimeStamp", "%Y-%m-%d %H:%M:%S") <<
      "] [" << boost::log::expressions::attr<boost::log::attributes::current_thread_id::value_type>("ThreadID") <<
      "] [" << std::left << std::setw(7) << std::setfill(' ') << boost::log::trivial::severity <<
      "] (" << boost::log::expressions::attr<std::string_view>("File") <<
      ":" << boost::log::expressions::attr<int>("Line") <<
      ":" << boost::log::expressions::attr<std::string>("Function") <<
      ") " << boost::log::expressions::smessage
      );

    boost::log::add_console_log(std::cout,
      boost::log::keywords::format = format);

    if(good_log_file) {
      auto file_log = boost::log::add_file_log(
        boost::log::keywords::file_name = log_absolute_filename.string(),
        boost::log::keywords::auto_flush = true,
        boost::log::keywords::format = format);
    } else {
      std::cout << "Failed to create log file: " << log_absolute_filename << std::endl;
    }

    boost::log::core::get()->set_filter
    (
      boost::log::trivial::severity >= boost::log::trivial::trace
    );
    boost::log::add_common_attributes();

    SystemClock clock;
    if (trace) {
      tracing::SetClock(&clock);
      tracing::Enable(20, "thalamus_trace");
      tracing::Start();
      tracing::SetCurrentThreadName("main");
    }

    std::unique_ptr<std::istream> json_buffer;
    if(config_filename != nullptr) {
      json_buffer.reset(new std::ifstream(config_filename));
    } else {
      json_buffer.reset(new std::stringstream("{\"nodes\":[{\"type\":\"ANALOG\", \"name\":\"ANALOG\"}]}"));
    }
    target_node = target_node ? target_node : "ANALOG";
    auto parsed = boost::json::parse(*json_buffer);
    auto state = std::get<thalamus::ObservableDictPtr>(thalamus::ObservableCollection::from_json(parsed));
    auto nodes = std::get<thalamus::ObservableListPtr>(state->at("nodes").get());
    
    std::string server_address = absl::StrFormat("0.0.0.0:%d", port);

    grpc::EnableDefaultHealthCheckService(true);
    grpc::reflection::InitProtoReflectionServerBuilderPlugin();
    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    node_graph.reset(new thalamus::NodeGraphImpl(nodes, io_context, system_start, steady_start));
    service.reset(new thalamus::Service(std::make_shared<thalamus::ObservableDict>(), io_context, *node_graph));
    node_graph->set_service(service.get());
    builder.RegisterService(service.get());
    server = builder.BuildAndStart();

    THALAMUS_LOG(info) << "Server listening on " << server_address << std::endl;
    grpc_thread = std::thread([] {
      server->Wait();
    });

    if(!good_log_file) {
      service->warn("Log creation failed", std::string("Unable to create log file ") + log_absolute_filename.string());
    }

    work_guard.reset(new WorkGuard{io_context.get_executor()});
    main_thread = std::thread([] {
      io_context.run();
    });

    std::promise<std::shared_ptr<thalamus::AnalogNode>> promise;
    auto future = promise.get_future();
    thalamus::NodeGraph::NodeConnection connection;
    thalamus_grpc::NodeSelector selector;
    selector.set_name(target_node);
    boost::asio::post(io_context, [&] {
      THALAMUS_LOG(info) << "Requesting Analog Node";
      connection = node_graph->get_node_scoped(selector, [&] (std::weak_ptr<thalamus::Node> node) {
        THALAMUS_LOG(info) << "Received Analog Node";
        auto locked = node.lock();
        auto analog = std::dynamic_pointer_cast<thalamus::AnalogNode>(locked);
        promise.set_value(analog);
      });
    });
    THALAMUS_LOG(info) << "Getting Analog Node";
    analog_node = future.get();
    THALAMUS_LOG(info) << "Got Analog Node";

    return 0;
  }

  EXPORT int thalamus_stop() {
    boost::asio::post(io_context, [] {
      work_guard.reset();
      io_context.stop();
    });
    main_thread.join();

    service->stop();
    server->Shutdown();
    grpc_thread.join();

    if(trace) {
      tracing::Stop();
      tracing::Wait();
    }

    service.reset();
    server.reset();
    node_graph.reset();

    return 0;
  }

  EXPORT int thalamus_push(size_t num_channels, const double* samples, const size_t* counts, const size_t* sample_intervals_ns, const char** channel_names) {
    thalamus::vector<std::span<const double>> spans;
    thalamus::vector<std::chrono::nanoseconds> sample_intervals;
    thalamus::vector<std::string_view> names;
    size_t offset = 0;
    for(auto i = 0;i < num_channels;++i) {
      spans.emplace_back(samples + offset, samples + offset + counts[i]);
      offset += counts[i];

      sample_intervals.emplace_back(sample_intervals_ns[i]);
      names.emplace_back(channel_names[i]);
    }

    std::promise<void> promise;
    auto future = promise.get_future();
    boost::asio::post(io_context, [&] {
      analog_node->inject(spans, sample_intervals, names);
      promise.set_value();
    });
    future.get();

    return 0;
  }

  EXPORT int thalamus_main(int argc, char** argv) {
    return thalamus::main(argc, argv);
  }
  
  EXPORT int hydrate_main(int argc, char** argv) {
    return hydrate::main(argc, argv);
  }
}

