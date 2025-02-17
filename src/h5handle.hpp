#include <memory>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
#include <hdf5.h>
#ifdef __clang__
#pragma clang diagnostic pop
#endif

namespace thalamus {
struct H5Handle : private std::shared_ptr<hid_t> {
  H5Handle(hid_t handle);

  H5Handle();

  H5Handle &operator=(hid_t handle);

  bool operator==(hid_t handle);

  bool operator!=(hid_t handle);

  operator hid_t();

  operator bool();

  void close();
};
H5Handle createH5ReceivedEvent();
H5Handle createH5Segment();
} // namespace thalamus
