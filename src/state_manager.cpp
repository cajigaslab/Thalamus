#include <thalamus/tracing.hpp>
#include <state_manager.hpp>
#include <thalamus/thread.hpp>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
#include <boost/signals2.hpp>
#ifdef __clang__
#pragma clang diagnostic pop
#endif

using namespace thalamus;
using namespace std::chrono_literals;

struct StateManager::Impl {
  thalamus_grpc::Thalamus::Stub *stub;
  ObservableCollection::Value state;
  ObservableCollection::Value root;
  boost::asio::io_context &io_context;
  std::atomic_bool running;
  std::atomic<
      ::grpc::ClientReaderWriter<::thalamus_grpc::ObservableTransaction,
                                 ::thalamus_grpc::ObservableTransaction> *>
      stream;
  const size_t CONNECT = 0;
  const size_t READ = 1;
  const size_t WRITE = 2;
  std::vector<thalamus_grpc::ObservableTransaction> outbox;
  std::mutex mutex;
  ::grpc::ClientContext context;
  std::map<unsigned long long, std::function<void()>> pending_changes;
  std::thread grpc_thread;
  boost::signals2::signal<void(ObservableCollection::Action action,
                               const std::string &address,
                               ObservableCollection::Value value)>
      signal;
  std::vector<boost::signals2::scoped_connection> state_connections;

  void grpc_target() {
    set_current_thread_name("StateManager");

    thalamus_grpc::ObservableTransaction in;
    thalamus_grpc::ObservableTransaction out;

    auto stream_ptr = stub->observable_bridge_v2(&context);
    this->stream = stream_ptr.get();

    while (stream_ptr->Read(&in)) {
      TRACE_EVENT("thalamus", "observable_bridge");
      if (in.acknowledged()) {
        TRACE_EVENT(
            "thalamus", "StateManager::acknowledged",
            perfetto::TerminatingFlow::ProcessScoped(in.acknowledged()));
        THALAMUS_LOG(trace) << "Acknowledged " << in.acknowledged();
        std::function<void()> callback;
        {
          std::unique_lock<std::mutex> lock(mutex);
          callback = pending_changes.at(in.acknowledged());
          pending_changes.erase(in.acknowledged());
        }
        boost::asio::post(io_context, callback);
        continue;
      }

      // std::cout << change.address() << " " << change.value() << "ACK: " <<
      // change.acknowledged() << std::endl;
      std::vector<std::promise<void>> promises;
      std::vector<std::future<void>> futures;
      for (auto &change : in.changes()) {
        THALAMUS_LOG(trace)
            << change.address() << " " << change.value() << std::endl;

        boost::json::value parsed = boost::json::parse(change.value());
        auto value = ObservableCollection::from_json(parsed);

        promises.emplace_back();
        futures.push_back(promises.back().get_future());
        boost::asio::post(
            io_context, [&promise = promises.back(), c_state = state,
                         action = change.action(), address = change.address(),
                         new_value = std::move(value)] {
              TRACE_EVENT("thalamus", "observable_bridge(post)");
              if (action == thalamus_grpc::ObservableChange_Action_Set) {
                set_jsonpath(c_state, address, new_value, true);
              } else {
                delete_jsonpath(c_state, address, true);
              }
              promise.set_value();
            });
      }
      for (auto &future : futures) {
        while (future.wait_for(1s) == std::future_status::timeout && running) {
        }
        if (!running) {
          return;
        }
      }
    }

    io_context.stop();
  }

  Impl(thalamus_grpc::Thalamus::Stub *_stub, ObservableCollection::Value _state,
       boost::asio::io_context &_io_context)
      : stub(_stub), state(_state), root(state), io_context(_io_context),
        running(true) {
    grpc_thread = std::thread(std::bind(&Impl::grpc_target, this));
    if (std::holds_alternative<ObservableListPtr>(state)) {
      auto temp = std::get<ObservableListPtr>(state);
      temp->set_remote_storage(
          std::bind(&Impl::send_change, this, _1, _2, _3, _4));
    } else if (std::holds_alternative<ObservableDictPtr>(state)) {
      auto temp = std::get<ObservableDictPtr>(state);
      temp->set_remote_storage(
          std::bind(&Impl::send_change, this, _1, _2, _3, _4));
    }
  }

  ~Impl() {
    running = false;
    context.TryCancel();
    grpc_thread.join();
  }

  bool send_change(ObservableCollection::Action action,
                   const std::string &address,
                   ObservableCollection::Value value,
                   std::function<void()> callback) {
    auto id = get_unique_id();
    TRACE_EVENT("thalamus", "StateManager::send_change",
                perfetto::Flow::ProcessScoped(id));
    if (io_context.stopped()) {
      return true;
    }

    auto loaded_stream = stream.load();
    if (loaded_stream == nullptr) {
      return false;
    }

    auto json_value = ObservableCollection::to_json(value);
    auto string_value = boost::json::serialize(json_value);
    thalamus_grpc::ObservableTransaction transaction;
    auto change = transaction.add_changes();
    change->set_address(address);
    change->set_value(string_value);
    if (action == ObservableCollection::Action::Set) {
      change->set_action(thalamus_grpc::ObservableChange_Action_Set);
    } else {
      change->set_action(thalamus_grpc::ObservableChange_Action_Delete);
    }
    transaction.set_id(id);

    THALAMUS_LOG(trace) << "Change " << transaction.id();

    {
      std::unique_lock<std::mutex> lock(mutex);
      pending_changes[transaction.id()] = callback;
    }

    loaded_stream->Write(transaction);
    return true;
  }
};

StateManager::StateManager(thalamus_grpc::Thalamus::Stub *stub,
                           ObservableCollection::Value state,
                           boost::asio::io_context &io_context)
    : impl(new Impl(stub, state, io_context)) {}

StateManager::~StateManager() {}
