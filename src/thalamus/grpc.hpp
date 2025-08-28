#pragma once

#include <mutex>
#include <functional>
#include <condition_variable>


#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
#include <boost/asio.hpp>
#include <grpcpp/grpcpp.h>
#ifdef __clang__
#pragma clang diagnostic pop
#endif

namespace thalamus {
  template<typename T>
  class ReadReactor : public grpc::ClientReadReactor<T> {
  public:
    std::mutex mutex;
    std::condition_variable condition;
    T in;
    grpc::ClientContext context;
    bool done = false;
    boost::asio::io_context& io_context;
    std::function<void(const T&)> callback;
    ReadReactor(boost::asio::io_context& _io_context, const std::function<void(const T&)>& _callback) : io_context(_io_context), callback(_callback) {}
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
    void OnReadDone(bool ok) override {
      if(!ok) {
        {
          std::lock_guard<std::mutex> lock(mutex);
          done = true;
        }
        condition.notify_all();
        return;
      }
      boost::asio::post(io_context, [&,c_in=std::move(in)] {
        //std::cout << "POST" << std::endl;
        callback(c_in);
      });
      grpc::ClientReadReactor<T>::StartRead(&in);
    }
    void OnDone(const grpc::Status&) override {
      //std::cout << "OnDone" << std::endl;
      {
        std::lock_guard<std::mutex> lock(mutex);
        done = true;
      }
      condition.notify_all();
    }
  };
}
