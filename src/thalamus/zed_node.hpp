#pragma once

#include <thalamus/modalities_util.hpp>
#include <thalamus/xsens_node.hpp>
#include <thalamus/analog_node.hpp>

namespace thalamus {

class ZedNode : public Node,
                public MotionCaptureNode,
                public AnalogNode {
  struct Impl;
  std::unique_ptr<Impl> impl;

public:
  ZedNode(ObservableDictPtr state, boost::asio::io_context &io_context,
          NodeGraph *graph);
  ~ZedNode() override;

  static std::string type_name();

  // MotionCaptureNode
  std::span<MotionCaptureNode::Segment const> segments() const override;
  const std::string_view pose_name() const override;
  void inject(const std::span<Segment const> &segments) override;

  // Node timing
  std::chrono::nanoseconds time() const override;
  std::chrono::nanoseconds sample_interval(int channel) const override;

  // AnalogNode — exposes joint positions as named channels (JOINT_x/y/z)
  std::span<const double> data(int channel) const override;
  int num_channels() const override;
  std::string_view name(int channel) const override;
  bool has_analog_data() const override;
  bool has_motion_data() const override;

  void inject(const thalamus::vector<std::span<double const>> &spans,
              const thalamus::vector<std::chrono::nanoseconds> &,
              const thalamus::vector<std::string_view> &) override;

  size_t modalities() const override;
};

} // namespace thalamus
