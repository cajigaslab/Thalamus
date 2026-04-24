#include "boost/asio/detached.hpp"
#include <chrono>
#include <memory>
#include <thalamus/http_server.hpp>
#include <thalamus/grpc.hpp>
#include <concepts>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
#include "boost/asio.hpp"
#include "boost/beast.hpp"
#include "thalamus.pb.h"
#ifdef __clang__
#pragma clang diagnostic pop
#endif

using namespace thalamus;

using TcpSocket = boost::asio::basic_stream_socket<boost::asio::ip::tcp>;
using WebSocket = boost::beast::websocket::stream<TcpSocket>;

struct HttpException : public std::exception {
  const boost::beast::http::status status;
  const std::string message;
  HttpException(boost::beast::http::status _status, const std::string& _message) : status(_status), message(_message) {}
  ~HttpException() override;

  const char* what() const noexcept override {
    return message.c_str();
  }
};

HttpException::~HttpException() {}

struct WebSocketException : public std::exception {
  const boost::beast::websocket::close_code status;
  WebSocket&& ws;
  WebSocketException(boost::beast::websocket::close_code _status, WebSocket&& _ws) : status(_status), ws(std::move(_ws)) {}
  ~WebSocketException() override;
};

WebSocketException::~WebSocketException() {}

static void http_assert(bool cond, boost::beast::http::status status) {
  if(!cond) {
    throw HttpException(status, "");
  }
}

#define websocket_assert(cond, ws, status) do { \
  if(!cond) { \
    throw WebSocketException(status, std::move(ws)); \
  } \
} while(0)

using namespace std::chrono_literals;

struct HttpServer::Impl {
  boost::asio::io_context& io_context;
  std::unique_ptr<thalamus_grpc::Thalamus::Stub> stub;
  Impl(boost::asio::io_context& _io_context, std::unique_ptr<thalamus_grpc::Thalamus::Stub>&& _stub)
  : io_context(_io_context), stub(std::move(_stub)) {}

  template<ProtobufMessage REQUEST, ProtobufMessage RESPONSE, std::invocable<grpc::ClientContext*, REQUEST*, grpc::ClientReadReactor<RESPONSE>*> CALL>
  boost::asio::awaitable<void> stream_to_client(boost::beast::http::request<boost::beast::http::string_body>& req, TcpSocket& socket, CALL call) {
    http_assert(boost::beast::websocket::is_upgrade(req), boost::beast::http::status::upgrade_required);
    WebSocket websocket(std::move(socket));
    websocket.binary(true);
    co_await websocket.async_accept(req);
    boost::beast::flat_buffer ws_buffer;
    
    ws_buffer.clear();
    auto [read_ec, count] = co_await websocket.async_read(ws_buffer, boost::asio::as_tuple(boost::asio::use_awaitable));
    if(read_ec) {
      THALAMUS_LOG(error) << read_ec.message();
      co_return;
    }
    websocket_assert(websocket.got_binary(), websocket, boost::beast::websocket::close_code::bad_payload);
    REQUEST request;
    auto parsed = request.ParseFromArray(ws_buffer.data().data(), int(ws_buffer.data().size()));
    websocket_assert(parsed, websocket, boost::beast::websocket::close_code::bad_payload);

    auto client_connected = true;
    grpc::ClientContext context;
    ReadReactor<RESPONSE, true> reactor(io_context, [&](std::string&& response) {
      auto ptr = std::make_shared<std::string>(std::move(response));
      websocket.async_write(boost::asio::buffer(*ptr), [&,ptr](const auto& ec, auto) {
        if(ec) {
          THALAMUS_LOG(info) << ec.message();
          client_connected = false;
          context.TryCancel();
        }
      });
    });

    call(&context, &request, &reactor);
    reactor.start();

    auto done = false;
    while(!done) {
      done = co_await reactor.wait(1s);
      if(client_connected) {
        auto [ec] = co_await websocket.async_ping({}, boost::asio::as_tuple(boost::asio::use_awaitable));
        if(ec) {
          THALAMUS_LOG(error) << ec.message();
          client_connected = false;
          context.TryCancel();
        }
      }
    }
    THALAMUS_LOG(info) << "HTTP stream done";
  }

  boost::asio::awaitable<void> session(TcpSocket socket) {
    boost::beast::flat_buffer buffer;
    boost::beast::http::request<boost::beast::http::string_body> req;
    auto [ec, bytes] = co_await boost::beast::http::async_read(socket, buffer, req, boost::asio::as_tuple(boost::asio::use_awaitable));
    if (ec) {
      THALAMUS_LOG(error) << ec.message();
      co_return;
    }

    try {
      if(req.target() == "/graph") {
        co_await stream_to_client<thalamus_grpc::GraphRequest, thalamus_grpc::GraphResponse>(req, socket, [&](auto context, auto request, auto reactor) {
          stub->async()->graph(context, request, reactor);
        });
      } else if(req.target() == "/analog") {
        co_await stream_to_client<thalamus_grpc::AnalogRequest, thalamus_grpc::AnalogResponse>(req, socket, [&](auto context, auto request, auto reactor) {
          stub->async()->analog(context, request, reactor);
        });
      } else if(req.target() == "/image") {
        co_await stream_to_client<thalamus_grpc::ImageRequest, thalamus_grpc::Image>(req, socket, [&](auto context, auto request, auto reactor) {
          stub->async()->image(context, request, reactor);
        });
      }
    } catch(HttpException& e) {
      boost::beast::http::response<boost::beast::http::string_body> res{e.status, req.version()};
      res.prepare_payload();
      boost::beast::http::write(socket, res);
    } catch(WebSocketException& e) {
      e.ws.close(e.status);
    }
  }


  boost::asio::awaitable<void> listen(const std::string ip, uint16_t port) {
    auto executor = co_await boost::asio::this_coro::executor;

    auto address = boost::asio::ip::make_address(ip);
    boost::asio::ip::tcp::endpoint endpoint{ address, port };
    auto acceptor = boost::asio::ip::tcp::acceptor{ executor, endpoint };

    while(true) {
      THALAMUS_LOG(info) << "Accepting HTTP " << ip << ":" << port;
      auto socket = co_await acceptor.async_accept();
      boost::asio::co_spawn(io_context, session(std::move(socket)), boost::asio::detached);
    }
  }
};

HttpServer::HttpServer(boost::asio::io_context& io_context, std::unique_ptr<thalamus_grpc::Thalamus::Stub>&& stub, const std::string& ip, uint16_t port)
: impl(new Impl(io_context, std::move(stub))) {
  boost::asio::co_spawn(io_context, impl->listen(ip, port), boost::asio::detached);
}

HttpServer::~HttpServer() {}
