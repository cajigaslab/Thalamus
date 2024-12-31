#include <stim_printer_node.hpp>
#include <modalities_util.hpp>
#include <google/protobuf/util/json_util.h>

using namespace thalamus;

struct StimPrinterNode::Impl {
  Impl(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph* graph, StimPrinterNode* outer) {
    using namespace std::placeholders;
  }
  std::vector<thalamus_grpc::StimDeclaration> stims;
};

StimPrinterNode::StimPrinterNode(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph* graph) : impl(new Impl(state, io_context, graph, this)) {}
StimPrinterNode::~StimPrinterNode() {}

std::string StimPrinterNode::type_name() {
  return "STIM_PRINTER";
}

std::future<thalamus_grpc::StimResponse> StimPrinterNode::stim(thalamus_grpc::StimRequest&& request) {
  std::promise<thalamus_grpc::StimResponse> promise;
  thalamus_grpc::StimResponse response;
  std::string text;
  google::protobuf::util::JsonPrintOptions options;
  options.add_whitespace = true;
  auto status = google::protobuf::util::MessageToJsonString(request, &text, options);
  THALAMUS_LOG(info) << text;
  if(request.has_declaration()) {
    impl->stims.resize(std::max(impl->stims.size(), size_t(request.declaration().id())+1));
    impl->stims[request.declaration().id()] = request.declaration();
  } else if (request.has_retrieve()) {
    impl->stims.resize(std::max(impl->stims.size(), size_t(request.retrieve())+1));
    *response.mutable_declaration() = impl->stims[request.retrieve()];
  }
  promise.set_value(response);
  return promise.get_future();
}

size_t StimPrinterNode::modalities() const { return infer_modalities<StimPrinterNode>(); }
