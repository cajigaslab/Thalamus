#pragma once

#include <base_node.hpp>
#include <grpc_impl.hpp>
#include <memory>
#include <state.hpp>

#include <alpha_omega_node.hpp>
#include <nidaq_node.hpp>
#include <storage_node.hpp>
#include <xsens_node.hpp>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include <boost/asio.hpp>

#ifdef __clang__
#pragma clang diagnostic pop
#endif

namespace thalamus {
class NodeGraphImpl : public NodeGraph {
  struct Impl;
  std::unique_ptr<Impl> impl;

public:
  NodeGraphImpl(ObservableListPtr nodes, boost::asio::io_context &io_context,
                std::chrono::system_clock::time_point,
                std::chrono::steady_clock::time_point,
                thalamus_grpc::Thalamus::Stub*);
  ~NodeGraphImpl() override;
  std::optional<std::string> get_type_name(const std::string &type) override;
  void set_service(Service *service);
  Service &get_service() override;
  std::weak_ptr<Node> get_node(const std::string &query_name) override;
  std::weak_ptr<Node>
  get_node(const thalamus_grpc::NodeSelector &query_name) override;
  void get_node(const std::string &query_name,
                std::function<void(std::weak_ptr<Node>)> callback) override;
  void get_node(const thalamus_grpc::NodeSelector &query_name,
                std::function<void(std::weak_ptr<Node>)> callback) override;
  NodeConnection
  get_node_scoped(const std::string &,
                  std::function<void(std::weak_ptr<Node>)>) override;
  NodeConnection
  get_node_scoped(const thalamus_grpc::NodeSelector &,
                  std::function<void(std::weak_ptr<Node>)>) override;
  std::shared_ptr<grpc::Channel> get_channel(const std::string &) override;
  thalamus_grpc::Thalamus::Stub* get_thalamus_stub(const std::string &) override;
  std::chrono::system_clock::time_point get_system_clock_at_start() override;
  std::chrono::steady_clock::time_point get_steady_clock_at_start() override;
  ThreadPool &get_thread_pool() override;
  void dialog(const thalamus_grpc::Dialog &) override;
};
} // namespace thalamus
