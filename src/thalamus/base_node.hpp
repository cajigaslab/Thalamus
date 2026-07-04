#pragma once

#include <chrono>
#include <functional>
#include <thalamus/state.hpp>
#include <string>
#include <thalamus/util.hpp>
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
#include <thalamus.grpc.pb.h>

#ifdef __clang__
#pragma clang diagnostic pop
#endif

struct VkInstance_T;
typedef VkInstance_T* VkInstance;
struct VkDevice_T;
typedef VkDevice_T* VkDevice;
struct VkPhysicalDevice_T;
typedef VkPhysicalDevice_T* VkPhysicalDevice;
struct VkQueue_T;
typedef VkQueue_T* VkQueue;
struct VkCommandPool_T;
typedef VkCommandPool_T* VkCommandPool;

namespace thalamus {
using namespace std::chrono_literals;
class Service;
class ThreadPool;

class Node : public std::enable_shared_from_this<Node> {
public:
  virtual ~Node();
  boost::signals2::signal<void(Node *)> ready;
  std::optional<boost::signals2::signal<void(Node *)>> ready_multithreaded;
  virtual size_t modalities() const = 0;
  virtual boost::json::value process(const boost::json::value &) {
    return boost::json::value();
  }
  virtual void process(const boost::json::value & request, std::function<void(const boost::json::value &)> callback) {
    callback(process(request));
  }
  virtual std::string_view redirect() const {
    return "";
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
  virtual thalamus_grpc::Thalamus::Stub* get_thalamus_stub(const std::string &) = 0;
  virtual std::chrono::system_clock::time_point get_system_clock_at_start() = 0;
  virtual std::chrono::steady_clock::time_point get_steady_clock_at_start() = 0;
  virtual ThreadPool &get_thread_pool() = 0;
  virtual void dialog(const thalamus_grpc::Dialog &) = 0;
  virtual void log(const thalamus_grpc::Text &) = 0;
  virtual void log(const std::string_view & text) {
    std::chrono::nanoseconds now = std::chrono::steady_clock::now().time_since_epoch();
    thalamus_grpc::Text message;
    message.set_time(uint64_t(now.count()));
    message.set_text(text);
    log(message);
  }
  virtual VkInstance get_vulkan_instance() = 0;
  virtual VkDevice get_vulkan_device() = 0;
  virtual VkPhysicalDevice get_vulkan_physical_device() = 0;
  virtual VkQueue get_vulkan_queue() = 0;
  virtual VkCommandPool create_vulkan_command_pool() = 0;
};

class NoneNode : public Node {
public:
  NoneNode(ObservableDictPtr, boost::asio::io_context &, NodeGraph *);
  size_t modalities() const override;

  static std::string type_name() { return "NONE"; }
};

std::vector<std::weak_ptr<ObservableDict>>
get_nodes(const ObservableList *nodes, const std::vector<std::string> &names);
} // namespace thalamus
