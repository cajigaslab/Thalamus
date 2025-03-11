#include <state.hpp>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
#include <boost/asio.hpp>
#include <thalamus.grpc.pb.h>
#ifdef __clang__
#pragma clang diagnostic pop
#endif

namespace thalamus {
class StateManager {
  struct Impl;
  std::unique_ptr<Impl> impl;

public:
  StateManager(thalamus_grpc::Thalamus::Stub *stub,
               ObservableCollection::Value state,
               boost::asio::io_context &io_context);
  ~StateManager();
};
} // namespace thalamus
