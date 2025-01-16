//#include <QApplication>
//#include <QScreen>
#include <thalamus/tracing.hpp>
#include "node_graph_impl.hpp"
#include <boost/asio.hpp>
#include <boost/program_options.hpp>
#include <state.hpp>
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
 
#include "grpc_impl.hpp"
#include <thalamus/thread.hpp>
#include <format>
#include <boost/log/trivial.hpp>
#include <thalamus/file.hpp>
#include <state_manager.hpp>

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

PERFETTO_TRACK_EVENT_STATIC_STORAGE();

namespace thalamus {
  using namespace std::chrono_literals;

  int main(int argc, char **argv) {
#ifdef _WIN32
    timeBeginPeriod(1);
#endif
    std::srand(std::time(nullptr));

    auto steady_start = std::chrono::steady_clock::now();
    auto system_start = std::chrono::system_clock::now();

    const auto start_time = absl::FromChrono(system_start);
    std::string start_time_str = absl::FormatTime("%Y%m%dT%H%M%SZ%z", start_time, absl::LocalTimeZone());

    std::string temp = "hello";

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
      ("state-url,s", boost::program_options::value<std::string>(), "Address of Thalamus instance that manages state");

    boost::program_options::variables_map vm;
    boost::program_options::store(boost::program_options::command_line_parser(argc, argv).options(desc).run(), vm);
    boost::program_options::notify(vm);

    if (vm.count("help")) {
      std::cout << desc << std::endl;
      return 0;
    }

    std::string state_url;
    if(vm.count("state-url") > 0) {
      state_url = vm["state-url"].as<std::string>();
    }
    auto port = vm["port"].as<size_t>();
    
    boost::asio::io_context io_context;

    //QApplication app (argc, argv); 
    set_current_thread_name("main");

    std::unique_ptr<perfetto::TracingSession> tracing_session;
    perfetto::TracingInitArgs tracing_args;
    tracing_args.backends |= perfetto::kInProcessBackend;

    perfetto::TraceConfig cfg;
    cfg.add_buffers()->set_size_kb(1024*1024);  // Record up to 1 MiB.
    cfg.set_output_path("thalamus_" + start_time_str + ".perfetto-trace");
    cfg.set_write_into_file(true);
    auto* ds_cfg = cfg.add_data_sources()->mutable_config();
    ds_cfg->set_name("track_event");

    if (vm.count("trace")) {
      perfetto::Tracing::Initialize(tracing_args);
      perfetto::TrackEvent::Register();
      tracing_session = perfetto::Tracing::NewTrace();
      tracing_session->Setup(cfg);
      tracing_session->StartBlocking();
    }

    std::shared_ptr<ObservableDict> state = std::make_shared<ObservableDict>();
    ObservableListPtr nodes = std::make_shared<ObservableList>();
    (*state)["nodes"].assign(nodes);
    
    std::optional<StateManager> state_manager;
    std::unique_ptr<thalamus_grpc::Thalamus::Stub> stub;
    if(!state_url.empty()) {
      auto channel = grpc::CreateChannel(state_url, grpc::InsecureChannelCredentials());
      while(channel->GetState(true) != GRPC_CHANNEL_READY) {
        THALAMUS_LOG(info) << "Waiting for state source";
        std::this_thread::sleep_for(1s);
      }
      stub = thalamus_grpc::Thalamus::NewStub(channel);
      state_manager.emplace(stub.get(), state, io_context);
    }

    std::string server_address = absl::StrFormat("0.0.0.0:%d", port);

    grpc::EnableDefaultHealthCheckService(true);
    grpc::reflection::InitProtoReflectionServerBuilderPlugin();
    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    std::unique_ptr<NodeGraphImpl> node_graph(new NodeGraphImpl(nodes, io_context, system_start, steady_start));
    Service service(state, io_context, *node_graph, state_url);
    node_graph->set_service(&service);
    builder.RegisterService(&service);
    auto server = builder.BuildAndStart();

    //std::cout << "Server listening on " << server_address << std::endl;
    std::thread grpc_thread([&] {
      server->Wait();
    });

    boost::asio::high_resolution_timer timer(io_context);
    std::function<void(const boost::system::error_code&)> poll_function = [&](const boost::system::error_code& error) {
      BOOST_ASSERT_MSG(!error, "async_wait failed");
      //QApplication::processEvents();
      timer.expires_after(32ms);
      timer.async_wait(poll_function);
    };
    poll_function(boost::system::error_code());

    io_context.run();
    THALAMUS_LOG(info) << "Shutting down";

    auto shutdown_success = false;
    std::condition_variable shutdown_condition;
    std::mutex shutdown_mutex;
    std::thread termination_thread([&] {
      std::unique_lock<std::mutex> lock(shutdown_mutex);
      shutdown_condition.wait_for(lock, 5s, [&] { return shutdown_success; });
      if(!shutdown_success) {
        THALAMUS_LOG(error) << "Clean shutdown taking too long, terminating" << std::endl;
        std::terminate();
      }
    });

    service.stop();
    server->Shutdown();
    grpc_thread.join();
    node_graph.reset();

    if (vm.count("trace")) {
      tracing_session->StopBlocking();
    }

    {
      std::lock_guard<std::mutex> lock(shutdown_mutex);
      shutdown_success = true;
    }
    shutdown_condition.notify_all();
    termination_thread.join();
    THALAMUS_LOG(info) << "Thalamus Ending";

    return 0;
  }
}
