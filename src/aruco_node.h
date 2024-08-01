#include <xsens_node.h>
#include <image_node.h>

namespace thalamus {
  class ArucoNode : public Node, public MotionCaptureNode, public AnalogNode, public ImageNode {
    struct Impl;
    std::unique_ptr<Impl> impl;
  public:
    ArucoNode(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph*);
    ~ArucoNode();
    static std::string type_name();
    std::span<Segment const> segments() const override;
    const std::string& pose_name() const override;
    std::chrono::nanoseconds time() const override;
    void inject(const std::span<Segment const>& segments) override;
  
    std::span<const double> data(int channel) const override;
    int num_channels() const override;
    std::string_view name(int channel) const override;
    std::chrono::nanoseconds sample_interval(int i) const override;
    void inject(const thalamus::vector<std::span<double const>>& spans, const thalamus::vector<std::chrono::nanoseconds>& sample_intervals, const thalamus::vector<std::string_view>&) override;
    bool has_analog_data() const override;
    bool has_motion_data() const override;
  
    boost::json::value process(const boost::json::value&) override;
    size_t modalities() const override;

    Plane plane(int) const override;
    size_t num_planes() const override;
    Format format() const override;
    size_t width() const override;
    size_t height() const override;
    std::chrono::nanoseconds frame_interval() const override;
    void inject(const thalamus_grpc::Image&) override;
    bool has_image_data() const override;
  };
}
