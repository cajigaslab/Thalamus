#include <base_node.hpp>
#include <analog_node.hpp>

namespace thalamus {

class RemoteLogNode : public Node, public AnalogNode {
  struct Impl;
  std::unique_ptr<Impl> impl;

public:
  RemoteLogNode(ObservableDictPtr, boost::asio::io_context &, NodeGraph *);
  ~RemoteLogNode() override;

  std::span<const double> data(int channel) const override;
  int num_channels() const override;
  std::chrono::nanoseconds sample_interval(int channel) const override;
  std::chrono::nanoseconds time() const override;
  std::chrono::nanoseconds remote_time() const override;
  std::string_view name(int channel) const override;
  void inject(const thalamus::vector<std::span<double const>> &,
              const thalamus::vector<std::chrono::nanoseconds> &,
              const thalamus::vector<std::string_view> &) override;
  bool has_analog_data() const override;

  static std::string type_name();
  size_t modalities() const override;
};
} // namespace thalamus
