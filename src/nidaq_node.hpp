#pragma once

#include <analog_node.hpp>
#include <base_node.hpp>
#include <state.hpp>
#include <stim_node.hpp>
#include <string>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include <boost/asio.hpp>

#ifdef __clang__
#pragma clang diagnostic pop
#endif

namespace thalamus {
class NidaqNode : public Node, public AnalogNode {
  struct Impl;
  std::unique_ptr<Impl> impl;

public:
  NidaqNode(ObservableDictPtr state, boost::asio::io_context &io_context,
            NodeGraph *graph);
  ~NidaqNode() override;
  static std::string type_name();
  std::span<const double> data(int channel) const override;
  int num_channels() const override;
  std::chrono::nanoseconds sample_interval(int i) const override;
  std::chrono::nanoseconds time() const override;
  void
  inject(const thalamus::vector<std::span<double const>> &spans,
         const thalamus::vector<std::chrono::nanoseconds> &sample_intervals,
         const thalamus::vector<std::string_view> &names) override;
  static int get_num_channels(const std::string &channel);
  std::string_view name(int channel) const override;

  static bool prepare();
  size_t modalities() const override;
};

class NidaqOutputNode : public Node, public StimNode {
  struct Impl;
  std::unique_ptr<Impl> impl;

public:
  NidaqOutputNode(ObservableDictPtr state, boost::asio::io_context &io_context,
                  NodeGraph *graph);
  ~NidaqOutputNode() override;
  static std::string type_name();

  std::future<thalamus_grpc::StimResponse>
  stim(thalamus_grpc::StimRequest &&) override;

  static bool prepare();
  size_t modalities() const override;
};
} // namespace thalamus
