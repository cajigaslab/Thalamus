#include <base_node.hpp>
#include <xsens_node.hpp>
#include <stim_node.hpp>

namespace thalamus {

  class RemoteNode : public Node, public AnalogNode, public MotionCaptureNode, public StimNode {
    struct Impl;
    std::unique_ptr<Impl> impl;
  public:
    RemoteNode(ObservableDictPtr, boost::asio::io_context&, NodeGraph*);
    ~RemoteNode() override;
    std::span<const double> data(int channel) const override;
    int num_channels() const override;
    std::chrono::nanoseconds sample_interval(int channel) const override;
    std::chrono::nanoseconds time() const override;
    std::string_view name(int channel) const override;
    std::span<const std::string> get_recommended_channels() const override;
    void inject(const thalamus::vector<std::span<double const>>&, const thalamus::vector<std::chrono::nanoseconds>&, const thalamus::vector<std::string_view>&) override;
    bool has_analog_data() const override;

    std::span<Segment const> segments() const override;
    const std::string_view pose_name() const override;
    void inject(const std::span<Segment const>& segments) override;
    bool has_motion_data() const override;

    std::future<thalamus_grpc::StimResponse> stim(thalamus_grpc::StimRequest&&) override;

    static std::string type_name();
    size_t modalities() const override;
  };
}
