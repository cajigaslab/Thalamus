#pragma once

#include <vector>
#include <list>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <functional>
#include <base_node.h>

namespace thalamus {
  class ThreadPool : public std::enable_shared_from_this<ThreadPool> {
    bool running;
    std::vector<std::thread> threads;
    std::list<std::function<void()>> jobs;
    std::condition_variable condition;
    mutable std::mutex mutex;
    const std::string name;
  public:
    const unsigned int num_threads;
    unsigned int num_busy_threads;
    void thread_target(std::string);
    ThreadPool(const std::string& name, unsigned int num_threads = 0)
      : running(false)
      , name(name.empty() ? "ThreadPool" : name)
      , num_threads(num_threads ? num_threads : std::thread::hardware_concurrency())
      , num_busy_threads(this->num_threads) {}
    ~ThreadPool() {
      stop();
    }
  
    bool full() const {
      std::lock_guard<std::mutex> lock(mutex);
      return num_busy_threads == num_threads;
    }

    int idle() const {
      std::lock_guard<std::mutex> lock(mutex);
      return num_threads - num_busy_threads;
    }
  
    void push(std::function<void()>&& job) {
      std::lock_guard<std::mutex> lock(mutex);
      jobs.push_back(job);
      condition.notify_one();
    }
  
    void start() {
      std::lock_guard<std::mutex> lock(mutex);
      if(running) {
        return;
      }
      running = true;
      for(auto i = 0u;i < num_threads;++i) {
        auto thread_name = absl::StrFormat("%s[%d]", name, i);
        threads.emplace_back([&,thread_name] { thread_target(thread_name); });
      }
    }
  
    void stop() {
      {
        std::lock_guard<std::mutex> lock(mutex);
        if(!running) {
          return;
        }
        running = false;
        condition.notify_all();
      }
      for(auto& t : threads) {
        t.join();
      }
      jobs.clear();
    }
  };

  class ThreadPoolNode : public Node, public AnalogNode {
    struct Impl;
    std::unique_ptr<Impl> impl;
  public:
    ThreadPoolNode(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph*);
    ~ThreadPoolNode();
    static std::string type_name();
    boost::json::value process(const boost::json::value&) override;

    std::span<const double> data(int channel) const override;
    int num_channels() const override;
    std::chrono::nanoseconds sample_interval(int channel) const override;
    std::string_view name(int channel) const override;
    std::span<const std::string> get_recommended_channels() const override;
    void inject(const thalamus::vector<std::span<double const>>&, const thalamus::vector<std::chrono::nanoseconds>&, const thalamus::vector<std::string_view>& names) override;
    bool has_analog_data() const override;
    std::chrono::nanoseconds time() const override;
    size_t modalities() const override;
  };
}
