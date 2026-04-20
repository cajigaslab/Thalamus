#pragma once
#include <memory>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
#include "thalamus.grpc.pb.h"
#include <boost/asio.hpp>
#ifdef __clang__
#pragma clang diagnostic pop
#endif

namespace thalamus {
struct HttpServer {
  struct Impl;
  std::unique_ptr<Impl> impl;
public:
  HttpServer(boost::asio::io_context&, std::unique_ptr<thalamus_grpc::Thalamus::Stub>&&, const std::string& ip, uint16_t port);
  ~HttpServer();

  void start();
  void stop();
};
} // namespace thalamus
