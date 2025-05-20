#pragma once

#include <base_node.hpp>
#include <chrono>
#include <condition_variable>
#include <future>
#include <state.hpp>
#include <util.hpp>
#include <xsens_node.hpp>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include <boost/asio.hpp>
#include <boost/json.hpp>
#include <boost/signals2.hpp>
#include <thalamus.grpc.pb.h>

#ifdef __clang__
#pragma clang diagnostic pop
#endif

namespace thalamus {
class Service : public thalamus_grpc::Thalamus::Service {
  struct Impl;
  std::unique_ptr<Impl> impl;

public:
  boost::signals2::signal<void(::thalamus_grpc::Event &)> events_signal;
  boost::signals2::signal<void(const ::thalamus_grpc::Text &)> log_signal;
  // boost::signals2::signal<void(::thalamus_grpc::ObservableChange&)>
  // change_signal;

  Service(ObservableCollection::Value state,
          boost::asio::io_context &io_context, NodeGraph &node_graph,
          std::string observable_bridge_redirect);
  ~Service() override;

  ::grpc::Status
  get_type_name(::grpc::ServerContext *context,
                const ::thalamus_grpc::StringMessage *request,
                ::thalamus_grpc::StringMessage *response) override;
  ::grpc::Status node_request(::grpc::ServerContext *context,
                              const ::thalamus_grpc::NodeRequest *request,
                              ::thalamus_grpc::NodeResponse *response) override;
  ::grpc::Status
  node_request_stream(::grpc::ServerContext *context,
                      ::grpc::ServerReaderWriter<::thalamus_grpc::NodeResponse,
                                                 ::thalamus_grpc::NodeRequest>
                          *stream) override;
  ::grpc::Status events(::grpc::ServerContext *context,
                        ::grpc::ServerReader<::thalamus_grpc::Event> *reader,
                        ::thalamus_grpc::Empty *) override;
  ::grpc::Status log(::grpc::ServerContext *context,
                     ::grpc::ServerReader<::thalamus_grpc::Text> *reader,
                     ::thalamus_grpc::Empty *) override;
  ::grpc::Status logout(::grpc::ServerContext *context,
                      const ::thalamus_grpc::Empty *,
                      ::grpc::ServerWriter<::thalamus_grpc::Text> *writer) override;
  ::grpc::Status observable_bridge(
      ::grpc::ServerContext *context,
      ::grpc::ServerReaderWriter<::thalamus_grpc::ObservableChange,
                                 ::thalamus_grpc::ObservableChange> *stream)
      override;
  ::grpc::Status observable_bridge_v2(
      ::grpc::ServerContext *context,
      ::grpc::ServerReaderWriter<::thalamus_grpc::ObservableTransaction,
                                 ::thalamus_grpc::ObservableTransaction>
          *stream) override;
  ::grpc::Status observable_bridge_read(
      ::grpc::ServerContext *context,
      const ::thalamus_grpc::ObservableReadRequest *request,
      ::grpc::ServerWriter<::thalamus_grpc::ObservableTransaction> *stream)
      override;
  ::grpc::Status
  observable_bridge_write(::grpc::ServerContext *context,
                          const ::thalamus_grpc::ObservableTransaction *request,
                          ::thalamus_grpc::Empty *response) override;
  ::grpc::Status
  get_redirect(::grpc::ServerContext *context,
           const ::thalamus_grpc::Empty *request,
           ::thalamus_grpc::Redirect *response) override;

  ::grpc::Status
  graph(::grpc::ServerContext *context,
        const ::thalamus_grpc::GraphRequest *request,
        ::grpc::ServerWriter<::thalamus_grpc::GraphResponse> *writer) override;
  ::grpc::Status get_recommended_channels(
      ::grpc::ServerContext *context,
      const ::thalamus_grpc::NodeSelector *request,
      ::thalamus_grpc::StringListMessage *response) override;
  ::grpc::Status analog(
      ::grpc::ServerContext *context,
      const ::thalamus_grpc::AnalogRequest *request,
      ::grpc::ServerWriter<::thalamus_grpc::AnalogResponse> *writer) override;
  ::grpc::Status
  spectrogram(::grpc::ServerContext *context,
              const ::thalamus_grpc::SpectrogramRequest *request,
              ::grpc::ServerWriter<::thalamus_grpc::SpectrogramResponse>
                  *writer) override;
  ::grpc::Status channel_info(
      ::grpc::ServerContext *context,
      const ::thalamus_grpc::AnalogRequest *request,
      ::grpc::ServerWriter<::thalamus_grpc::AnalogResponse> *writer) override;
  ::grpc::Status
  xsens(::grpc::ServerContext *context,
        const ::thalamus_grpc::NodeSelector *request,
        ::grpc::ServerWriter<::thalamus_grpc::XsensResponse> *writer) override;
  ::grpc::Status
  motion_capture(::grpc::ServerContext *context,
        const ::thalamus_grpc::NodeSelector *request,
        ::grpc::ServerWriter<::thalamus_grpc::XsensResponse> *writer) override;
  ::grpc::Status
  image(::grpc::ServerContext *context,
        const ::thalamus_grpc::ImageRequest *request,
        ::grpc::ServerWriter<::thalamus_grpc::Image> *writer) override;
  ::grpc::Status replay(::grpc::ServerContext *context,
                        const ::thalamus_grpc::ReplayRequest *request,
                        ::thalamus_grpc::Empty *response) override;
  ::grpc::Status eval(::grpc::ServerContext *context,
                      ::grpc::ServerReaderWriter<::thalamus_grpc::EvalRequest,
                                                 ::thalamus_grpc::EvalResponse>
                          *stream) override;
  ::grpc::Status
  remote_node(::grpc::ServerContext *context,
              ::grpc::ServerReaderWriter<::thalamus_grpc::RemoteNodeMessage,
                                         ::thalamus_grpc::RemoteNodeMessage>
                  *stream) override;
  ::grpc::Status notification(
      ::grpc::ServerContext *context, const ::thalamus_grpc::Empty *request,
      ::grpc::ServerWriter<::thalamus_grpc::Notification> *writer) override;
  ::grpc::Status inject_analog(
      ::grpc::ServerContext *context,
      ::grpc::ServerReader<::thalamus_grpc::InjectAnalogRequest> *reader,
      ::thalamus_grpc::Empty *) override;
  ::grpc::Status inject_motion_capture(
      ::grpc::ServerContext *context,
      ::grpc::ServerReader<::thalamus_grpc::InjectMotionCaptureRequest> *reader,
      ::thalamus_grpc::Empty *) override;
  ::grpc::Status
  get_modalities(::grpc::ServerContext *context,
                 const ::thalamus_grpc::NodeSelector *request,
                 ::thalamus_grpc::ModalitiesMessage *response) override;
  ::grpc::Status
  ping(::grpc::ServerContext *context,
       ::grpc::ServerReaderWriter<::thalamus_grpc::Pong, ::thalamus_grpc::Ping>
           *stream) override;
  ::grpc::Status stim(::grpc::ServerContext *context,
                      ::grpc::ServerReaderWriter<::thalamus_grpc::StimResponse,
                                                 ::thalamus_grpc::StimRequest>
                          *reader) override;

  std::future<ObservableCollection::Value> evaluate(const std::string &code);
  bool send_change(ObservableCollection::Action action,
                   const std::string &address,
                   ObservableCollection::Value value,
                   std::function<void()> callback);
  void warn(const std::string &title, const std::string &message);
  void stop();
  void wait();
};
} // namespace thalamus
