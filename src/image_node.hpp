#pragma once

#include <string>
#include <thalamus.pb.h>
#include <thalamus_asio.hpp>
#include <base_node.hpp>
#include <analog_node.hpp>
#include <state.hpp>

namespace thalamus {
  class ImageNode {
  public:
    using Plane = std::span<const unsigned char>;
    using Planes = std::array<Plane, 3>;
    enum class Format {
      Gray,
      RGB,
      YUYV422,
      YUV420P,
      YUVJ420P,
    };
    virtual ~ImageNode() {}
    virtual Plane plane(int) const = 0;
    virtual size_t num_planes() const = 0;
    virtual Format format() const = 0;
    virtual size_t width() const = 0;
    virtual size_t height() const = 0;
    virtual std::chrono::nanoseconds frame_interval() const = 0;
    virtual std::chrono::nanoseconds time() const = 0;
    virtual void inject(const thalamus_grpc::Image&) = 0;
    virtual bool has_image_data() const {
      return true;
    }
  };

  class FfmpegNode : public Node, public ImageNode, public AnalogNode {
    struct Impl;
    std::unique_ptr<Impl> impl;
  public:
    FfmpegNode(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph*);
    ~FfmpegNode() override;
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
    size_t modalities() const override;
  };
}

