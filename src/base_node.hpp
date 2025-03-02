#pragma once

#include <chrono>
#include <functional>
#include <state.hpp>
#include <string>
#include <util.hpp>
#include <vector>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include <boost/asio.hpp>
#include <boost/json.hpp>
#include <boost/signals2.hpp>
#include <grpcpp/channel.h>
#include <thalamus.pb.h>

#ifdef __clang__
#pragma clang diagnostic pop
#endif

namespace thalamus {
using namespace std::chrono_literals;
class Service;
class ThreadPool;

class Node : public std::enable_shared_from_this<Node> {
public:
  virtual ~Node();
  boost::signals2::signal<void(Node *)> ready;
  std::map<size_t, boost::signals2::scoped_connection> connections;
  virtual size_t modalities() const = 0;
  virtual boost::json::value process(const boost::json::value &) {
    return boost::json::value();
  }
};

class NodeGraph {
public:
  virtual ~NodeGraph();
  virtual std::weak_ptr<Node> get_node(const std::string &) = 0;
  virtual std::weak_ptr<Node> get_node(const thalamus_grpc::NodeSelector &) = 0;
  virtual void get_node(const std::string &,
                        std::function<void(std::weak_ptr<Node>)>) = 0;
  virtual void get_node(const thalamus_grpc::NodeSelector &,
                        std::function<void(std::weak_ptr<Node>)>) = 0;
  using NodeConnection = boost::signals2::scoped_connection;
  virtual NodeConnection
  get_node_scoped(const std::string &,
                  std::function<void(std::weak_ptr<Node>)>) = 0;
  virtual NodeConnection
  get_node_scoped(const thalamus_grpc::NodeSelector &,
                  std::function<void(std::weak_ptr<Node>)>) = 0;
  virtual Service &get_service() = 0;
  virtual std::optional<std::string> get_type_name(const std::string &) = 0;
  virtual std::shared_ptr<grpc::Channel> get_channel(const std::string &) = 0;
  virtual std::chrono::system_clock::time_point get_system_clock_at_start() = 0;
  virtual std::chrono::steady_clock::time_point get_steady_clock_at_start() = 0;
  virtual ThreadPool &get_thread_pool() = 0;
};

class NoneNode : public Node {
public:
  NoneNode(ObservableDictPtr, boost::asio::io_context &, NodeGraph *);
  size_t modalities() const override;

  static std::string type_name() { return "NONE"; }
};

std::vector<std::weak_ptr<ObservableDict>>
get_nodes(ObservableList *nodes, const std::vector<std::string> &names);
} // namespace thalamus
