#pragma once

#include <thalamus_asio.h>
#include <vector>
#include <map>
#include <functional>
#include <string>
#include <iostream>
#include <filesystem>
#include <variant>
#include <regex>
#include <thread>
//#include <plot.h>
#include <base_node.h>
#include <state.hpp>
#include <xsens_node.h>
#include <tracing/tracing.h>

#include <thalamus.pb.h>
#include <grpc_impl.h>

namespace thalamus {
  class StorageNode : public Node, public AnalogNode {
    struct Impl;
    std::unique_ptr<Impl> impl;
  public:
    StorageNode(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph* graph);
    ~StorageNode();
    static std::string type_name();
    static std::filesystem::path get_next_file(const std::filesystem::path& name, std::chrono::system_clock::time_point time = std::chrono::system_clock::now());
    static void record(std::ofstream&, const thalamus_grpc::StorageRecord&);
    static void record(std::ofstream&, const std::string&);

    std::span<const double> data(int channel) const override;
    int num_channels() const override;
    std::chrono::nanoseconds sample_interval(int channel) const override;
    std::chrono::nanoseconds time() const override;
    std::string_view name(int channel) const override;
    std::span<const std::string> get_recommended_channels() const override;
    void inject(const thalamus::vector<std::span<double const>>&, const thalamus::vector<std::chrono::nanoseconds>&, const thalamus::vector<std::string_view>&) override;
    size_t modalities() const override { return 0; }
  };
}
