#pragma once

#include <thalamus_asio.hpp>

#include <string>
#include <base_node.hpp>
#include <analog_node.hpp>
#include <state.hpp>

namespace thalamus {
  class TouchScreenNode : public Node, public AnalogNode {
    struct Impl;
    std::unique_ptr<Impl> impl;
  public:
    TouchScreenNode(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph*);
    ~TouchScreenNode();
    static std::string type_name();
    std::chrono::nanoseconds time() const override;
    std::span<const double> data(int channel) const override;
    int num_channels() const override;
    std::string_view name(int channel) const override;
    std::chrono::nanoseconds sample_interval(int i) const override;
    void inject(const thalamus::vector<std::span<double const>>& spans, const thalamus::vector<std::chrono::nanoseconds>& sample_intervals, const thalamus::vector<std::string_view>&) override;
    bool has_analog_data() const override;
    size_t modalities() const override;
  };
}
