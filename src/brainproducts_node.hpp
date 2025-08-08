#pragma once

#include <base_node.hpp>
#include <analog_node.hpp>
#include <span>
#include <string>
#include <util.hpp>

namespace thalamus {

class BrainProductsNode : public AnalogNode, public Node {
  struct Impl;
  std::unique_ptr<Impl> impl;

public:
  BrainProductsNode(ObservableDictPtr state,
                    boost::asio::io_context &io_context, NodeGraph *graph);

  ~BrainProductsNode() override;

  static std::string type_name();

  std::span<const double> data(int index) const override;

  int num_channels() const override;

  void
  inject(const thalamus::vector<std::span<double const>> &data,
         const thalamus::vector<std::chrono::nanoseconds> &sample_intervals,
         const thalamus::vector<std::string_view> &) override;

  std::chrono::nanoseconds sample_interval(int) const override;
  std::chrono::nanoseconds time() const override;
  std::string_view name(int channel) const override;
  std::span<const std::string> get_recommended_channels() const override;
  size_t modalities() const override;

  static bool prepare();
};

}; // namespace thalamus
