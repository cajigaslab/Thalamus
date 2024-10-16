#pragma once

#include <thalamus_asio.h>
#include <thalamus_signals2.h>

#include <vector>
#include <functional>
#include <string>
#include <span>
#include <chrono>
#include <state.hpp>
#include <util.h>
#include <grpcpp/channel.h>
#include <boost/json.hpp>
#include <thalamus.pb.h>

namespace thalamus {
  using namespace std::chrono_literals;
  class Service;
  class ThreadPool;

  template<typename T>
  double interval_to_frequency(T interval) {
    using frequency = std::ratio_divide<std::ratio<1, 1>, typename T::period>;
    auto result = 1.0 * frequency::num / frequency::den / interval.count();
    return result;
  }

  class Node : public std::enable_shared_from_this<Node> {
  public:
    virtual ~Node() {};
    boost::signals2::signal<void(Node*)> ready;
    std::map<size_t, boost::signals2::scoped_connection> connections;
    virtual size_t modalities() const = 0;
    virtual boost::json::value process(const boost::json::value&) {
      return boost::json::value();
    }
  };


  class NodeGraph {
  public:
    virtual ~NodeGraph() {};
    virtual std::weak_ptr<Node> get_node(const std::string&) = 0;
    virtual std::weak_ptr<Node> get_node(const thalamus_grpc::NodeSelector&) = 0;
    virtual void get_node(const std::string&, std::function<void(std::weak_ptr<Node>)>) = 0;
    virtual void get_node(const thalamus_grpc::NodeSelector&, std::function<void(std::weak_ptr<Node>)>) = 0;
    using NodeConnection = std::shared_ptr<boost::signals2::scoped_connection>;
    virtual NodeConnection get_node_scoped(const std::string&, std::function<void(std::weak_ptr<Node>)>) = 0;
    virtual NodeConnection get_node_scoped(const thalamus_grpc::NodeSelector&, std::function<void(std::weak_ptr<Node>)>) = 0;
    virtual Service& get_service() = 0;
    virtual std::optional<std::string> get_type_name(const std::string&) = 0;
    virtual std::shared_ptr<grpc::Channel> get_channel(const std::string&) = 0;
    virtual std::chrono::system_clock::time_point get_system_clock_at_start() = 0;
    virtual std::chrono::steady_clock::time_point get_steady_clock_at_start() = 0;
    virtual ThreadPool& get_thread_pool() = 0;
  };

  class NoneNode : public Node {
  public:
    NoneNode(ObservableDictPtr, boost::asio::io_context&, NodeGraph*) {}
    size_t modalities() const override {
      return 0;
    }

    static std::string type_name() {
      return "NONE";
    }
  };

  class AnalogNode {
  public:
    boost::signals2::signal<void(AnalogNode*)> channels_changed;
    static std::string EMPTY;
    virtual std::span<const double> data(int channel) const = 0;
    virtual int num_channels() const = 0;
    virtual std::chrono::nanoseconds sample_interval(int channel) const = 0;
    virtual std::chrono::nanoseconds time() const = 0;
    virtual std::string_view name(int channel) const = 0;
    virtual std::span<const std::string> get_recommended_channels() const { return std::span<const std::string>(); }
    virtual void inject(const thalamus::vector<std::span<double const>>&, const thalamus::vector<std::chrono::nanoseconds>&, const thalamus::vector<std::string_view>&) = 0;
    virtual bool has_analog_data() const {
      return true;
    }
  };

  class AnalogNodeImpl : public Node, public AnalogNode {
    struct Impl;
    std::unique_ptr<Impl> impl;
  public:
    AnalogNodeImpl(ObservableDictPtr state, boost::asio::io_context&, NodeGraph* graph);
    AnalogNodeImpl();
    ~AnalogNodeImpl();
    virtual std::span<const double> data(int channel) const override;
    virtual int num_channels() const override;
    virtual std::chrono::nanoseconds sample_interval(int channel) const override;
    virtual std::chrono::nanoseconds time() const override;
    std::string_view name(int channel) const override;
    std::span<const std::string> get_recommended_channels() const override;
    virtual void inject(const thalamus::vector<std::span<double const>>&, const thalamus::vector<std::chrono::nanoseconds>&, const thalamus::vector<std::string_view>&) override;
    virtual void inject(const thalamus::vector<std::span<double const>>&, const thalamus::vector<std::chrono::nanoseconds>&, const thalamus::vector<std::string_view>&, std::chrono::nanoseconds);
    static std::string type_name();
    size_t modalities() const override;
  };

  class StarterNode : public Node {
    struct Impl;
    std::unique_ptr<Impl> impl;
  public:
    StarterNode(ObservableDictPtr state, boost::asio::io_context&, NodeGraph* graph);
    ~StarterNode();
    static std::string type_name();
    size_t modalities() const override;
  };

  class WaveGeneratorNode : public AnalogNode, public Node {
    struct Impl;
    std::unique_ptr<Impl> impl;
  public:
    WaveGeneratorNode(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph* graph);

    ~WaveGeneratorNode();

    static std::string type_name();

    std::span<const double> data(int index) const override;

    int num_channels() const override;

    void inject(const thalamus::vector<std::span<double const>>& data, const thalamus::vector<std::chrono::nanoseconds>& sample_intervals, const thalamus::vector<std::string_view>&) override;

    std::chrono::nanoseconds sample_interval(int) const override;
    std::chrono::nanoseconds time() const override;
    std::string_view name(int channel) const override;
    std::span<const std::string> get_recommended_channels() const override;
    size_t modalities() const override;
  };

  class ToggleNode : public AnalogNode, public Node {
    struct Impl;
    std::unique_ptr<Impl> impl;

  public:
    ToggleNode(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph* graph);
    ~ToggleNode();

    static std::string type_name();

    std::span<const double> data(int i) const override;
    int num_channels() const override;

    void inject(const thalamus::vector<std::span<double const>>& data, const thalamus::vector<std::chrono::nanoseconds>& sample_intervals, const thalamus::vector<std::string_view>&) override;

    std::chrono::nanoseconds sample_interval(int) const override;
    std::chrono::nanoseconds time() const override;
    std::string_view name(int channel) const override;
    std::span<const std::string> get_recommended_channels() const override;
    size_t modalities() const override;
  };

  std::vector<std::weak_ptr<ObservableDict>> get_nodes(ObservableList* nodes, const std::vector<std::string>& names);
}
