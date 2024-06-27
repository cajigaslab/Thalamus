#include <base_node.h>

namespace thalamus {
  class TaskControllerNode : public Node {
    struct Impl;
    std::unique_ptr<Impl> impl;
  public:
    TaskControllerNode(ObservableDictPtr, boost::asio::io_context&, NodeGraph*);
    ~TaskControllerNode();
    std::span<const double> data(int channel) const;
    int num_channels() const;
    std::chrono::nanoseconds sample_interval(int channel) const;
    std::chrono::nanoseconds time() const;
    void inject(const thalamus::vector<std::span<double const>>&, const thalamus::vector<std::chrono::nanoseconds>&);
    std::chrono::nanoseconds ping() const;
    static std::string type_name();
  };
}
