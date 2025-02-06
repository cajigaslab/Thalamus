#pragma once

#include <base_node.hpp>
#include <state.hpp>
#include <string>
#include <thalamus_asio.hpp>
#include <xsens_node.hpp>

#include <grpc_impl.hpp>
#include <thalamus.pb.h>

namespace thalamus {
class Ros2Node : public Node, public AnalogNode {
  struct Impl;
  std::unique_ptr<Impl> impl;

public:
  Ros2Node(ObservableDictPtr state, boost::asio::io_context &io_context,
           NodeGraph *graph);
  ~Ros2Node() override;
  static std::string type_name();

  std::span<const double> data(int channel) const override;
  int num_channels() const override;
  std::chrono::nanoseconds sample_interval(int channel) const override;
  std::chrono::nanoseconds time() const override;
  std::string_view name(int channel) const override;
  void inject(const thalamus::vector<std::span<double const>> &,
              const thalamus::vector<std::chrono::nanoseconds> &,
              const thalamus::vector<std::string_view> &) override;

  static bool prepare();
  static void cleanup();
  size_t modalities() const override { return 0; }
};
} // namespace thalamus
