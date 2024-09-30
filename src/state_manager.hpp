#include <thalamus.grpc.pb.h>

#include <boost/asio.hpp>
#include <state.hpp>
#include <thalamus.pb.h>

namespace thalamus {
  class StateManager {
    struct Impl;
    std::unique_ptr<Impl> impl;
  public:
    StateManager(thalamus_grpc::Thalamus::Stub* stub, ObservableCollection::Value state, boost::asio::io_context& io_context);
    ~StateManager();
  };
}
