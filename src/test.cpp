#include <chrono>
#include <modalities.h>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
#include <gtest/gtest.h>
#include <boost/asio.hpp>

#include <boost/log/expressions.hpp>
#include <boost/log/support/date_time.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/utility/setup/console.hpp>
#include <boost/log/utility/setup/file.hpp>
#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include "node_graph_impl.hpp"
#include <wallclock_node.hpp>
#include <thalamus/async.hpp>

using namespace std::chrono_literals;
using namespace thalamus;

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wglobal-constructors"
#endif

TEST(WallClockNodeTest, Test) {
  auto steady_start = std::chrono::steady_clock::now();
  auto system_start = std::chrono::system_clock::now();
  boost::asio::io_context io_context;

  ObservableDictPtr node_config = std::make_shared<ObservableDict>();
  (*node_config)["name"].assign("wall");
  (*node_config)["type"].assign("WALLCLOCK");

  ObservableListPtr nodes = std::make_shared<ObservableList>();
  nodes->push_back(node_config);
  std::unique_ptr<NodeGraphImpl> node_graph(
      new NodeGraphImpl(nodes, io_context, system_start, steady_start));

  auto node = std::static_pointer_cast<WallClockNode>(node_graph->get_node("wall").lock());
  std::vector<double> system_times;
  std::vector<std::chrono::nanoseconds> steady_times;
  node->ready.connect([&](auto*) {
    ASSERT_EQ(node->num_channels(), 1);
    ASSERT_EQ(node->name(0), "Epoch (ns)");
    auto data = node->data(0);
    ASSERT_EQ(data.size(), 1);
    system_times.push_back(node->data(0)[0]);
    steady_times.push_back(node->time());
    io_context.stop();
  });

  SteadyClockGuard clock_guard;
  MovableSteadyClock::move(1s);
  MovableSystemClock::move(1s);
  io_context.run_for(1s);
  ASSERT_EQ(system_times.size(), 1);
  MovableSteadyClock::move(1s);
  MovableSystemClock::move(1s);
  io_context.restart();
  io_context.run_for(1s);
  ASSERT_EQ(system_times.size(), 2);

  ASSERT_NEAR((system_times[1] - system_times[0])/1e9, 1, .01);
  ASSERT_NEAR(double(steady_times[1].count() - steady_times[0].count())/1e9, 1, .01);
  ASSERT_EQ(node->sample_interval(0), 1s);
  ASSERT_EQ(node->type_name(), "WALLCLOCK");
  ASSERT_EQ(node->modalities(), THALAMUS_MODALITY_ANALOG);

  std::vector<double> a = {15};
  node->inject({a}, {2s}, {"Gibberish"});
  ASSERT_EQ(system_times.size(), 3);
  ASSERT_NEAR(system_times.back(), 15, 1e-6);
}

int main(int argc, char** argv) {
  //auto steady_start = std::chrono::steady_clock::now();
  auto system_start = std::chrono::system_clock::now();

  const auto start_time = absl::FromChrono(system_start);
  std::string start_time_str =
      absl::FormatTime("%Y%m%dT%H%M%SZ%z", start_time, absl::LocalTimeZone());

  //auto log_filename = absl::StrFormat("thalamus_%s.log", start_time_str);
  //auto log_absolute_filename = std::filesystem::path(log_filename);
  //std::cout << "Log file: " << log_absolute_filename.string() << " "
  //          << std::filesystem::exists(log_absolute_filename) << std::endl;

  auto format =
      (boost::log::expressions::stream
       << "["
       << boost::log::expressions::format_date_time<boost::posix_time::ptime>(
              "TimeStamp", "%Y-%m-%d %H:%M:%S")
       << "] ["
       << boost::log::expressions::attr<
              boost::log::attributes::current_thread_id::value_type>("ThreadID")
       << "] [" << std::left << std::setw(7) << std::setfill(' ')
       << boost::log::trivial::severity << "] ("
       << boost::log::expressions::attr<std::string_view>("File") << ":"
       << boost::log::expressions::attr<int>("Line") << ":"
       << boost::log::expressions::attr<std::string>("Function") << ") "
       << boost::log::expressions::smessage);

  boost::log::add_console_log(std::cout, boost::log::keywords::format = format);

  //auto file_log = boost::log::add_file_log(
  //    boost::log::keywords::file_name = log_absolute_filename.string(),
  //    boost::log::keywords::auto_flush = true,
  //    boost::log::keywords::format = format);

  boost::log::core::get()->set_filter(boost::log::trivial::severity >=
                                      boost::log::trivial::trace);
  boost::log::add_common_attributes();

  init_movable_clocks();
  testing::InitGoogleTest(&argc, argv);
  auto result = RUN_ALL_TESTS();
  cleanup_movable_clocks();
  return result;
}
