#pragma once

#include <image_node.h>

namespace thalamus {
  class GenicamNode : public Node, public ImageNode, public AnalogNode {
    struct Impl;
    std::unique_ptr<Impl> impl;
  public:
    GenicamNode(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph*);
    ~GenicamNode();
    static std::string type_name();
    static bool prepare();
    Plane plane(int) const override;
    size_t num_planes() const override;
    Format format() const override;
    size_t width() const override;
    size_t height() const override;
    std::chrono::nanoseconds frame_interval() const override;
    std::chrono::nanoseconds time() const override;
    void inject(const thalamus_grpc::Image&) override;
    bool has_image_data() const override;

    std::span<const double> data(int channel) const override;
    int num_channels() const override;
    std::chrono::nanoseconds sample_interval(int channel) const override;
    std::string_view name(int channel) const override;
    std::span<const std::string> get_recommended_channels() const override;
    void inject(const thalamus::vector<std::span<double const>>&, const thalamus::vector<std::chrono::nanoseconds>&, const thalamus::vector<std::string_view>&) override;
    bool has_analog_data() const override;
    boost::json::value process(const boost::json::value&) override;
    size_t modalities() const override;
  };
}

