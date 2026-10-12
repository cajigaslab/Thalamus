#pragma once

#include <thalamus/analog_node.hpp>
#include <thalamus/base_node.hpp>
#include <thalamus/state.hpp>
#include <string>

namespace thalamus {
using namespace std::chrono_literals;
using namespace std::placeholders;

class IntanNode : public Node, public AnalogNode {
  struct Impl;
  std::unique_ptr<Impl> impl;

public:
  IntanNode(ObservableDictPtr state, boost::asio::io_context &io_context,
            NodeGraph *graph);
  ~IntanNode() override;

  static std::string type_name();

  std::span<const double> data(int index) const override;

  int num_channels() const override;
  bool channels_changed() const override;


  std::chrono::nanoseconds sample_interval(int) const override;
  std::chrono::nanoseconds time() const override;
  std::string_view name(int channel) const override;
  size_t modalities() const override;
};
} // namespace thalamus
