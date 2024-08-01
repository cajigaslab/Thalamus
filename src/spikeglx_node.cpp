#include <spikeglx_node.h>
#include <boost/asio.hpp>
#include <vector>
#include <map>
#include <functional>
#include <string>
#include <iostream>
#include <variant>
#include <regex>
#include <thread>
//#include <plot.h>
#include <base_node.h>
#include <absl/strings/str_split.h>
#include <state.h>
#include <boost/signals2.hpp>
#include <modalities.h>

using namespace thalamus;
using namespace std::chrono_literals;
using namespace std::placeholders;

class SpikeGlxNode::Impl {
  ObservableDictPtr state;
  size_t observer_id;
  boost::signals2::scoped_connection state_connection;
  boost::asio::io_context& io_context;
  boost::asio::high_resolution_timer timer;
  boost::asio::ip::tcp::socket socket;
  size_t num_channels;
  size_t buffer_size;
  std::vector<short> short_buffer;
  std::vector<double> double_buffer;
  std::vector<int> channels;
  std::map<size_t, std::function<void(Node*)>> observers;
  //double sample_rate;
  size_t counter = 0;
  int address[6];
  unsigned char buffer[1024];
  SpikeGlxNode* outer;
  bool is_running = false;
  bool is_connected = false;
public:
  Impl(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph* graph, SpikeGlxNode* outer)
    : state(state)
    , io_context(io_context)
    , timer(io_context)
    , socket(io_context)
    , outer(outer) {
    state_connection = state->changed.connect(std::bind(&Impl::on_change, this, _1, _2, _3));
    (*state)["Running"].assign(false);
    memset(&address, 0, sizeof(address));
    state->recap(std::bind(&Impl::on_change, this, _1, _2, _3));
  }

  ~Impl() {
    (*state)["Running"].assign(false);
  }

  void on_change(ObservableCollection::Action a, const ObservableCollection::Key& k, const ObservableCollection::Value& v) {
    auto key_str = std::get<std::string>(k);
    if (key_str == "Running") {
      is_running = std::get<bool>(v);
      if (is_running) {
        std::string address_str = state->at("Address");
        std::vector<std::string> address_tokens = absl::StrSplit(address_str, ':');
        if (address_tokens.size() < 2) {
          address_tokens.push_back("4142");
        }
        boost::asio::ip::tcp::resolver resolver(io_context);
        auto endpoints = resolver.resolve(address_tokens.at(0), address_tokens.at(1));
        boost::system::error_code ec;
        boost::asio::connect(socket, endpoints, ec);
        if(ec) {
          THALAMUS_LOG(error) << ec.what();
          (*state)["Running"].assign(false);
          return;
        }

        std::string command = "GETVERSION";
        socket.send(boost::asio::const_buffer(command.data(), command.size()), 0, ec);
        if(ec) {
          THALAMUS_LOG(error) << ec.what();
          (*state)["Running"].assign(false);
          return;
        }
        socket.async_receive(boost::asio::buffer(buffer, sizeof(buffer)), std::bind(&Impl::on_receive, this, _1, _2));
        is_connected = true;
      } else if(is_connected) {
        boost::system::error_code ec;
        socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
        if(ec) {
          THALAMUS_LOG(error) << ec.what();
        }
        socket.close(ec);
        if(ec) {
          THALAMUS_LOG(error) << ec.what();
        }
        is_connected = false;
      }
    }
  }

  void on_receive(const boost::system::error_code& error, size_t length) {
    if(error) {
      THALAMUS_LOG(error) << error.what();
      (*state)["Running"].assign(false);
      return;
    }
    std::cout << std::string(reinterpret_cast<char*>(buffer), length);

    socket.async_receive(boost::asio::buffer(buffer, sizeof(buffer)), std::bind(&Impl::on_receive, this, _1, _2));
  }
};

SpikeGlxNode::SpikeGlxNode(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph* graph)
  : impl(new Impl(state, io_context, graph, this)) {}

SpikeGlxNode::~SpikeGlxNode() {}
  
std::span<const double> SpikeGlxNode::data(int channel) const {
  return std::span<const double>();
}

std::string_view SpikeGlxNode::name(int channel) const {
  return std::string_view();
}

int SpikeGlxNode::num_channels() const {
  return 1;
}

std::chrono::nanoseconds SpikeGlxNode::sample_interval(int i) const {
  return 1'000'000ns;
}

std::chrono::nanoseconds SpikeGlxNode::time() const {
  return std::chrono::nanoseconds();
}

std::string SpikeGlxNode::type_name() {
  return "SPIKEGLX";
}

void SpikeGlxNode::inject(const thalamus::vector<std::span<double const>>& data, const thalamus::vector<std::chrono::nanoseconds>& sample_intervals, const thalamus::vector<std::string_view>&) {}

size_t SpikeGlxNode::modalities() const {
  return THALAMUS_MODALITY_ANALOG;
}
