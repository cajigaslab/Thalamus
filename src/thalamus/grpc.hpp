#pragma once

#include <queue>
#include <mutex>
#include <functional>
#include <condition_variable>
#include <thalamus/util.hpp>


#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
#include <boost/asio.hpp>
#include <boost/cobalt.hpp>
#include <grpcpp/grpcpp.h>
#ifdef __clang__
#pragma clang diagnostic pop
#endif

namespace thalamus {

  template<typename T>
  concept ProtobufMessage = std::derived_from<T, google::protobuf::Message>;

  template<ProtobufMessage T, bool serialize = false>
  class ReadReactor : public grpc::ClientReadReactor<T> {
  public:
    using Callback = std::function<void(std::conditional_t<serialize, std::string, T>&& value)>;

    std::mutex mutex;
    std::condition_variable condition;
    T in;
    grpc::ClientContext context;
    bool done = false;
    boost::asio::io_context& io_context;
    boost::asio::steady_timer timer;
    Callback callback;
    ReadReactor(boost::asio::io_context& _io_context, const Callback& _callback)
    : io_context(_io_context),
      timer(io_context),
      callback(_callback) {}
    ~ReadReactor() override {
      //grpc::ClientBidiReactor<task_controller_grpc::TaskResult, task_controller_grpc::TaskConfig>::StartWritesDone();
      context.TryCancel();
      std::unique_lock<std::mutex> lock(mutex);
      condition.wait(lock, [&] { return done; });
    }
    void start() {
      grpc::ClientReadReactor<T>::StartRead(&in);
      grpc::ClientReadReactor<T>::StartCall();
    }

    template<typename Duration>
    boost::asio::awaitable<bool> wait(Duration duration) {
      if(done) {
        co_return done;
      }
      timer.expires_after(duration);
      auto [ec] = co_await timer.async_wait(boost::asio::as_tuple(boost::asio::use_awaitable));
      THALAMUS_ASSERT(!ec || ec.value() == boost::asio::error::operation_aborted, "Error in Reactor %s", ec.message());
      co_return done;
    }

    void signal_done() {
      {
        std::lock_guard<std::mutex> lock(mutex);
        done = true;
        timer.cancel();
      }
      condition.notify_all();
    }
    void OnReadDone(bool ok) override {
      if(!ok) {
        signal_done();
        return;
      }
      if constexpr (serialize) {
        boost::asio::post(io_context, [&,c_in=in.SerializePartialAsString()]() mutable {
          callback(std::move(c_in));
        });
      } else {
        boost::asio::post(io_context, [&,c_in=std::move(in)]() mutable {
          callback(std::move(c_in));
        });
      }
      grpc::ClientReadReactor<T>::StartRead(&in);
    }
    void OnDone(const grpc::Status&) override {
      signal_done();
    }
  };

  template<typename REQUEST, typename RESPONSE>
  class BidiReactor : public grpc::ClientBidiReactor<REQUEST, RESPONSE> {
  public:
    std::mutex mutex;
    std::condition_variable condition;
    bool sending = false;
    RESPONSE response;
    grpc::ClientContext context;
    bool done = false;
    REQUEST current_request;
    std::queue<REQUEST> requests;
    boost::asio::io_context& io_context;
    std::function<void(RESPONSE&&)> callback;

    BidiReactor(boost::asio::io_context& _io_context, const std::function<void(RESPONSE&&)>& _callback) : io_context(_io_context), callback(_callback) {}

    ~BidiReactor() override {
      //grpc::ClientBidiReactor<task_controller_grpc::TaskResult, task_controller_grpc::TaskConfig>::StartWritesDone();
      context.TryCancel();
      std::unique_lock<std::mutex> lock(mutex);
      condition.wait(lock, [&] { return done; });
    }
    
    void signal_done() {
      {
        std::lock_guard<std::mutex> lock(mutex);
        done = true;
      }
      condition.notify_all();
    }

    void start() {
      grpc::ClientBidiReactor<REQUEST, RESPONSE>::StartRead(&response);
      grpc::ClientBidiReactor<REQUEST, RESPONSE>::StartCall();
    }

    void OnReadDone(bool ok) override {
      if(!ok) {
        signal_done();
        return;
      }
      boost::asio::post(io_context, [&,c_in=std::move(response)]() mutable {
        //std::cout << "POST" << std::endl;
        callback(std::move(c_in));
      });
      grpc::ClientBidiReactor<REQUEST, RESPONSE>::StartRead(&response);
    }

    void OnWriteDone(bool ok) override {
      if(!ok) {
        signal_done();
        return;
      }
      std::lock_guard<std::mutex> lock(mutex);
      sending = false;
      do_send();
    }

    void OnDone(const grpc::Status&) override {
      std::cout << "OnDone" << std::endl;
      signal_done();
    }

    void do_send() {
      if(sending || requests.empty()) {
        return;
      }
      sending = true;
      current_request = requests.front();
      requests.pop();
      grpc::ClientBidiReactor<REQUEST, RESPONSE>::StartWrite(&current_request);
    }

    void send(REQUEST&& result) {
      std::lock_guard<std::mutex> lock(mutex);
      requests.push(std::move(result));
      do_send();
    }
  };

  template<typename REQUEST, typename RESPONSE>
  class ServerBidiReactor : public grpc::ServerBidiReactor<REQUEST, RESPONSE> {
  public:
    std::mutex mutex;
    std::condition_variable condition;
    bool sending = false;
    REQUEST request;
    grpc::CallbackServerContext& context;
    bool done = false;
    RESPONSE current_response;
    std::queue<RESPONSE> responses;
    boost::asio::io_context& io_context;
    std::function<void(REQUEST&&)> callback;

    ServerBidiReactor(grpc::CallbackServerContext& _context, boost::asio::io_context& _io_context, const std::function<void(REQUEST&&)>& _callback)
     : context(_context), io_context(_io_context), callback(_callback) {}

    ServerBidiReactor(grpc::CallbackServerContext& _context, boost::asio::io_context& _io_context)
     : context(_context), io_context(_io_context) {}

    ~ServerBidiReactor() override {
      //grpc::ClientBidiReactor<task_controller_grpc::TaskResult, task_controller_grpc::TaskConfig>::StartWritesDone();
      context.TryCancel();
      std::unique_lock<std::mutex> lock(mutex);
      condition.wait(lock, [&] { return done; });
    }
    
    void signal_done() {
      {
        std::lock_guard<std::mutex> lock(mutex);
        done = true;
      }
      condition.notify_all();
    }

    void start() {
      grpc::ServerBidiReactor<REQUEST, RESPONSE>::StartRead(&request);
    }

    void OnReadDone(bool ok) override {
      if(!ok) {
        signal_done();
        return;
      }
      boost::asio::post(io_context, [&,c_in=std::move(request)]() mutable {
        //std::cout << "POST" << std::endl;
        callback(std::move(c_in));
      });
      grpc::ServerBidiReactor<REQUEST, RESPONSE>::StartRead(&request);
    }

    void OnWriteDone(bool ok) override {
      if(!ok) {
        signal_done();
        return;
      }
      std::lock_guard<std::mutex> lock(mutex);
      sending = false;
      do_send();
    }

    void OnDone() override {
      std::cout << "OnDone" << std::endl;
      signal_done();
    }

    void do_send() {
      if(sending || responses.empty()) {
        return;
      }
      sending = true;
      current_response = std::move(responses.front());
      responses.pop();
      grpc::ServerBidiReactor<REQUEST, RESPONSE>::StartWrite(&current_response);
    }

    void send(RESPONSE&& result) {
      std::lock_guard<std::mutex> lock(mutex);
      responses.push(std::move(result));
      do_send();
    }
  };


  template<typename RESPONSE>
  class ServerWriteReactor : public grpc::ServerWriteReactor<RESPONSE> {
  public:
    std::mutex mutex;
    std::condition_variable condition;
    RESPONSE current_response;
    std::queue<RESPONSE> responses;
    grpc::CallbackServerContext& context;
    bool done = false;
    bool sending = false;

    ServerWriteReactor(grpc::CallbackServerContext& _context)
     : context(_context) {}

    ~ServerWriteReactor() override {
      //grpc::ClientBidiReactor<task_controller_grpc::TaskResult, task_controller_grpc::TaskConfig>::StartWritesDone();
      context.TryCancel();
      std::unique_lock<std::mutex> lock(mutex);
      condition.wait(lock, [&] { return done; });
    }
    
    void signal_done() {
      {
        std::lock_guard<std::mutex> lock(mutex);
        done = true;
      }
      condition.notify_all();
    }

    void OnWriteDone(bool ok) override {
      if(!ok) {
        signal_done();
        return;
      }
      std::lock_guard<std::mutex> lock(mutex);
      sending = false;
      do_send();
    }

    void OnDone() override {
      //std::cout << "OnDone" << std::endl;
      signal_done();
    }

    void do_send() {
      if(sending || responses.empty()) {
        return;
      }
      sending = true;
      current_response = std::move(responses.front());
      responses.pop();
      grpc::ServerWriteReactor<RESPONSE>::StartWrite(&current_response);
    }

    void send(RESPONSE&& result) {
      std::lock_guard<std::mutex> lock(mutex);
      responses.push(std::move(result));
      do_send();
    }
  };
}
