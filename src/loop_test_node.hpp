#include <base_node.hpp>
#include <analog_node.hpp>

namespace thalamus {
  class LoopTestNode : public Node, public AnalogNode {
    struct Impl;
    std::unique_ptr<Impl> impl;
  public:
    LoopTestNode(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph*);
    ~LoopTestNode();
    static std::string type_name();
    std::chrono::nanoseconds time() const override;

    std::span<const double> data(int channel) const override;
    int num_channels() const override;
    std::chrono::nanoseconds sample_interval(int channel) const override;
    std::string_view name(int channel) const override;
    void inject(const thalamus::vector<std::span<double const>>&, const thalamus::vector<std::chrono::nanoseconds>&, const thalamus::vector<std::string_view>&) override;
    bool has_analog_data() const override;
    size_t modalities() const override;
  };
}
