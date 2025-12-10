#pragma once

#include <base_node.hpp>
#include <analog_node.hpp>
#include <text_node.hpp>

namespace thalamus {
class DelsysNode : public Node, public AnalogNode, public TextNode {
  struct Impl;
  std::unique_ptr<Impl> impl;

public:
  DelsysNode(ObservableDictPtr state, boost::asio::io_context &, NodeGraph *graph);
  ~DelsysNode() override;

  std::chrono::nanoseconds time() const override;
  static std::string type_name();
  size_t modalities() const override;
  std::string_view redirect() const override;

  void inject(const thalamus::vector<std::span<double const>> &,
              const thalamus::vector<std::chrono::nanoseconds> &,
              const thalamus::vector<std::string_view> &) override;
  std::string_view name(int channel) const override;
  int num_channels() const override;
  std::string_view text() const override;
  bool has_text_data() const override;
  bool has_analog_data() const override;
  std::chrono::nanoseconds sample_interval(int channel) const override;
  std::span<const double> data(int channel) const override;
  void process(const boost::json::value &, std::function<void(const boost::json::value &)> callback) override;
};
} // namespace thalamus
