#pragma once

#include <thalamus_asio.h>

#include <string>
#include <base_node.h>
#include <state.h>
#include <boost/qvm/vec.hpp>
#include <boost/qvm/quat.hpp>

namespace thalamus {
  using namespace std::chrono_literals;

  class MotionCaptureNode {
  public:
    struct Segment {
      unsigned int frame;
      unsigned int segment_id;
      unsigned int time;
      boost::qvm::vec<float, 3> position;
      boost::qvm::quat<float> rotation;
      static const size_t serialized_size;
      static Segment parse(unsigned char* data);
    };
    virtual ~MotionCaptureNode() {}
    virtual std::span<Segment const> segments() const = 0;
    virtual const std::string& pose_name() const = 0;
    virtual std::chrono::nanoseconds time() const = 0;
    virtual void inject(const std::span<Segment const>& segments) = 0;
    virtual bool has_motion_data() const {
      return true;
    }
  };

  class XsensNode : public Node, public MotionCaptureNode, public AnalogNode {
    struct Impl;
    std::unique_ptr<Impl> impl;
  public:
    XsensNode(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph*);
    ~XsensNode();
    static std::string type_name();
    std::span<Segment const> segments() const override;
    const std::string& pose_name() const override;
    std::chrono::nanoseconds time() const override;
    void inject(const std::span<Segment const>& segments) override;

    std::span<const double> data(int channel) const override;
    int num_channels() const override;
    std::string_view name(int channel) const override;
    std::span<const std::string> get_recommended_channels() const override;
    std::chrono::nanoseconds sample_interval(int i) const override;
    void inject(const thalamus::vector<std::span<double const>>& spans, const thalamus::vector<std::chrono::nanoseconds>& sample_intervals, const thalamus::vector<std::string_view>&) override;
    bool has_analog_data() const override;
    bool has_motion_data() const override;

    boost::json::value process(const boost::json::value&) override;
  };

  class HandEngineNode : public Node, public MotionCaptureNode, public AnalogNode {
    struct Impl;
    std::unique_ptr<Impl> impl;
  public:
    HandEngineNode(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph*);
    ~HandEngineNode();
    static std::string type_name();
    std::span<Segment const> segments() const override;
    const std::string& pose_name() const override;
    std::chrono::nanoseconds time() const override;
    void inject(const std::span<Segment const>& segments) override;

    std::span<const double> data(int channel) const override;
    int num_channels() const override;
    std::string_view name(int channel) const override;
    std::span<const std::string> get_recommended_channels() const override;
    std::chrono::nanoseconds sample_interval(int i) const override;
    void inject(const thalamus::vector<std::span<double const>>& spans, const thalamus::vector<std::chrono::nanoseconds>& sample_intervals, const thalamus::vector<std::string_view>&) override;
    bool has_analog_data() const override;
    bool has_motion_data() const override;
  };
}
