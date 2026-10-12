#pragma once
#include <thalamus/analog_node.hpp>

namespace thalamus {
class WallClockNode : public AnalogNode, public Node {
  struct Impl;
  std::unique_ptr<Impl> impl;

public:
  WallClockNode(ObservableDictPtr state,
                    boost::asio::io_context &io_context, NodeGraph *graph);

  static std::string type_name();

  std::span<const double> data(int index) const override;
  
  std::span<const uint64_t> ulong_data(int) const override;
  bool is_ulong_data() const override;

  int num_channels() const override;
  bool channels_changed() const override;

  void
  inject_analog(const thalamus::vector<std::span<double const>> &data,
         const thalamus::vector<std::chrono::nanoseconds> &sample_intervals,
         const thalamus::vector<std::string_view> &, bool) override;

  std::chrono::nanoseconds sample_interval(int) const override;
  std::chrono::nanoseconds time() const override;
  std::string_view name(int channel) const override;
  size_t modalities() const override;
};
}
