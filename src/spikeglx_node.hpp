#pragma once

#include <boost/asio.hpp>
#include <string>
#include <base_node.hpp>
#include <analog_node.hpp>
#include <absl/strings/str_split.h>
#include <state.hpp>

namespace thalamus {
  using namespace std::chrono_literals;
  using namespace std::placeholders;

  class SpikeGlxNode : public Node, public AnalogNode {
    struct Impl;
    std::unique_ptr<Impl> impl;
  public:
    SpikeGlxNode(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph* graph);
    ~SpikeGlxNode();

    static std::string type_name();

    std::span<const double> data(int index) const override;

    int num_channels() const override;

    void inject(const thalamus::vector<std::span<double const>>& data, const thalamus::vector<std::chrono::nanoseconds>& sample_intervals, const thalamus::vector<std::string_view>&) override;

    std::chrono::nanoseconds sample_interval(int) const override;
    std::chrono::nanoseconds time() const override;
    std::string_view name(int channel) const override;
    size_t modalities() const override;
    virtual boost::json::value process(const boost::json::value&) override;
    bool is_short_data() const override;
    std::span<const short> short_data(int index) const override;
  };
}
