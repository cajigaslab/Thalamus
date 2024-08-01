#pragma once

#include <base_node.h>
#include <boost/asio.hpp>
#include <string>
//#include <plot.h>
#include <state.h>

namespace thalamus {
  using namespace std::chrono_literals;

  class AlphaOmegaNode : public AnalogNode, public Node {
    struct Impl;
    std::unique_ptr<Impl> impl;
  public:
    AlphaOmegaNode(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph* graph);

    ~AlphaOmegaNode();

#ifdef _WIN32
    static std::string type_name() {
      return "ALPHA_OMEGA (NEURO_OMEGA)";
    }
#else
    static std::string type_name() {
      return "ALPHA_OMEGA (MOCK)";
    }
#endif

    //QWidget* create_widget() override;
    std::span<const double> data(int channel) const override;
    int num_channels() const override;
    std::chrono::nanoseconds sample_interval(int i) const override;
    std::chrono::nanoseconds time() const override;
    std::string_view name(int channel) const override;
    std::span<const std::string> get_recommended_channels() const override;
    virtual boost::json::value process(const boost::json::value&) override;
    void inject(const thalamus::vector<std::span<double const>>&, const thalamus::vector<std::chrono::nanoseconds>&, const thalamus::vector<std::string_view>&) override;

    void on_change(ObservableCollection::Action a, const ObservableCollection::Key& k, const ObservableCollection::Value& v);

    static bool prepare();
    size_t modalities() const override;
  };
}
