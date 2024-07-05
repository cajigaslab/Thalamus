#pragma once

#include <thalamus_asio.h>

#include <functional>
#include <string>
#include <base_node.h>
#include <state.h>

namespace thalamus {
  class NidaqNode : public Node, public AnalogNode {
    struct Impl;
    std::unique_ptr<Impl> impl;
  public:
    NidaqNode(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph* graph);
    ~NidaqNode();
    static std::string type_name();
    std::span<const double> data(int channel) const override;
    int num_channels() const override;
    std::chrono::nanoseconds sample_interval(int i) const override;
    std::chrono::nanoseconds time() const override;
    void inject(const thalamus::vector<std::span<double const>>& spans, const thalamus::vector<std::chrono::nanoseconds>& sample_intervals, const thalamus::vector<std::string_view>& names) override;
    static int get_num_channels(const std::string& channel);
    std::string_view name(int channel) const override;
    std::span<const std::string> get_recommended_channels() const override;

    static bool prepare();
  };

  class NidaqOutputNode : public Node {
    struct Impl;
    std::unique_ptr<Impl> impl;
  public:
    NidaqOutputNode(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph* graph);
    ~NidaqOutputNode();
    static std::string type_name();

    static bool prepare();
  };
}