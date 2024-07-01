//#include <QApplication>
//#include <QScreen>
#include "node_graph_impl.h"
#include <boost/asio.hpp>
#include <boost/program_options.hpp>
#include <state.h>
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

namespace thalamus {
  using namespace std::chrono_literals;

  int main(int argc, char **argv) {
#ifdef _WIN32
    timeBeginPeriod(1);
#endif
    std::srand(std::time(nullptr));

    std::string temp = "hello";

    auto steady_start = std::chrono::steady_clock::now();
    auto system_start = std::chrono::system_clock::now();

    const auto start_time = absl::FromChrono(system_start);
    auto start_time_str = absl::FormatTime("%Y%m%dT%H%M%SZ%z", start_time, absl::LocalTimeZone());
    auto log_filename = absl::StrFormat("thalamus_%s.log", start_time_str);
    auto log_absolute_filename = get_home() / std::filesystem::path(log_filename);
    auto good_log_file = can_write_file(log_absolute_filename);
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

    boost::program_options::positional_options_description p;

    boost::program_options::options_description desc("Hexascope registration program, version " GIT_COMMIT_HASH);
    desc.add_options()
      ("help,h", "produce help message")
      ("trace,t", "Enable tracing")
      ("port,p", boost::program_options::value<size_t>()->default_value(50050), "GRPC Port")
      ("slave,s", "Defer state management to remote process");

    boost::program_options::variables_map vm;
    boost::program_options::store(boost::program_options::command_line_parser(argc, argv).options(desc).run(), vm);
    boost::program_options::notify(vm);

    if (vm.count("help")) {
      std::cout << desc << std::endl;
      return 0;
    }

    auto slave = vm.count("slave") > 0;
    auto port = vm["port"].as<size_t>();
    
    boost::asio::io_context io_context;

    //QApplication app (argc, argv); 

    SystemClock clock;
    if (vm.count("trace")) {
      tracing::SetClock(&clock);
      tracing::Enable(20, "thalamus_trace");
      tracing::Start();
      tracing::SetCurrentThreadName("main");
    }

    std::shared_ptr<ObservableDict> state = std::make_shared<ObservableDict>();
    ObservableListPtr nodes = std::make_shared<ObservableList>();
    (*state)["nodes"].assign(nodes);

    std::string server_address = absl::StrFormat("0.0.0.0:%d", port);

    grpc::EnableDefaultHealthCheckService(true);
    grpc::reflection::InitProtoReflectionServerBuilderPlugin();
    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    NodeGraphImpl node_graph(nodes, io_context, system_start, steady_start);
    Service service(slave ? state : std::make_shared<ObservableDict>(), io_context, node_graph);
    node_graph.set_service(&service);
    builder.RegisterService(&service);
    auto server = builder.BuildAndStart();

    //std::cout << "Server listening on " << server_address << std::endl;
    std::thread grpc_thread([&] {
      server->Wait();
    });

    if (slave) {
      service.wait();
    }

    if(!good_log_file) {
      service.warn("Log creation failed", std::string("Unable to create log file ") + log_absolute_filename.string());
    }

    boost::asio::high_resolution_timer timer(io_context);
    std::function<void(const boost::system::error_code&)> poll_function = [&](const boost::system::error_code& error) {
      BOOST_ASSERT_MSG(!error, "async_wait failed");
      //QApplication::processEvents();
      timer.expires_after(32ms);
      timer.async_wait(poll_function);
    };
    poll_function(boost::system::error_code());

    io_context.run();

    service.stop();
    server->Shutdown();
    grpc_thread.join();

    if (vm.count("trace")) {
      tracing::Stop();
      tracing::Wait();
    }

    return 0;
  }
}
