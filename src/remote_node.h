#include <base_node.h>
#include <xsens_node.h>

namespace thalamus {

  class RemoteNode : public Node, public AnalogNode, public MotionCaptureNode {
    struct Impl;
    std::unique_ptr<Impl> impl;
  public:
    RemoteNode(ObservableDictPtr, boost::asio::io_context&, NodeGraph*);
    ~RemoteNode();
    std::span<const double> data(int channel) const;
    int num_channels() const;
    std::chrono::nanoseconds sample_interval(int channel) const;
    std::chrono::nanoseconds time() const;
    std::string_view name(int channel) const;
    std::span<const std::string> get_recommended_channels() const;
    void inject(const thalamus::vector<std::span<double const>>&, const thalamus::vector<std::chrono::nanoseconds>&, const thalamus::vector<std::string_view>&);
    bool has_analog_data() const;

    std::span<Segment const> segments() const;
    const std::string& pose_name() const;
    void inject(const std::span<Segment const>& segments);
    bool has_motion_data() const;

    static std::string type_name();
  };
}
