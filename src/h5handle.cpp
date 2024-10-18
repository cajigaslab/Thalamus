#include <h5handle.hpp>
#include <map>
#include <memory>
#include <functional>
#include <hdf5.h>
#include <util.hpp>
#include <xsens_node.h>

namespace thalamus {

  static const std::map<H5I_type_t, std::function<herr_t(hid_t)>> closers = std::map<H5I_type_t, std::function<herr_t(hid_t)>>({
    {H5I_GROUP, H5Gclose},
    {H5I_DATATYPE, H5Tclose},
    {H5I_DATASPACE, H5Sclose},
    {H5I_ATTR, H5Aclose},
    {H5I_FILE, H5Fclose},
    {H5I_GENPROP_LST, H5Pclose},
    {H5I_DATASET, H5Dclose}
    });

  struct H5Deleter {
    void operator()(hid_t* handle) {
      if (*handle != H5I_INVALID_HID && *handle != H5P_DEFAULT) {
        auto the_type = H5Iget_type(*handle);
        auto i = closers.find(the_type);
        THALAMUS_ASSERT(i != closers.end(), "Failed to find closer");
        auto status = i->second(*handle);
        THALAMUS_ASSERT(status >= 0, "Failed to close");
      }
      delete handle;
    }
  };

  H5Handle::H5Handle(hid_t handle)
    : std::shared_ptr<hid_t>(new hid_t(handle), H5Deleter{}) {}

  H5Handle::H5Handle() : std::shared_ptr<hid_t>(new hid_t(H5I_INVALID_HID), H5Deleter{ }) {}

  H5Handle& H5Handle::operator=(hid_t handle) {
    reset(new hid_t(handle), H5Deleter{});
    return *this;
  }

  bool H5Handle::operator==(hid_t handle) {
    return **this == handle;
  }

  bool H5Handle::operator!=(hid_t handle) {
    return !(**this == handle);
  }

  H5Handle::operator hid_t() {
    return **this;
  }

  H5Handle::operator bool() {
    return **this != H5I_INVALID_HID;
  }

  void H5Handle::close() {
    reset(new hid_t(H5I_INVALID_HID), H5Deleter{});
  }

  H5Handle createH5ReceivedEvent() {
    H5Handle received_type = H5Tcreate(H5T_COMPOUND, sizeof(ReceivedEvent));
    BOOST_ASSERT(received_type);
    auto h5_status = H5Tinsert(received_type, "time", HOFFSET(ReceivedEvent, time), H5T_NATIVE_UINT64);
    BOOST_ASSERT_MSG(h5_status >= 0, "Failed to create ReceivedEvent.time");
    h5_status = H5Tinsert(received_type, "index", HOFFSET(ReceivedEvent, index), H5T_NATIVE_UINT64);
    BOOST_ASSERT_MSG(h5_status >= 0, "Failed to create ReceivedEvent.index");

    return received_type;
  }

  using vecf3 = boost::qvm::vec<float, 3>;

  H5Handle createH5Segment() {
    H5Handle position_type = H5Tcreate(H5T_COMPOUND, sizeof(boost::qvm::vec<float, 3>));
    BOOST_ASSERT(position_type);
    auto h5_status = H5Tinsert(position_type, "x", HOFFSET(vecf3, a[0]), H5T_NATIVE_FLOAT);
    BOOST_ASSERT_MSG(h5_status >= 0, "Failed to create boost::qvm::vec<float, 3>.x");
    h5_status = H5Tinsert(position_type, "y", HOFFSET(vecf3, a[1]), H5T_NATIVE_FLOAT);
    BOOST_ASSERT_MSG(h5_status >= 0, "Failed to create boost::qvm::vec<float, 3>.y");
    h5_status = H5Tinsert(position_type, "z", HOFFSET(vecf3, a[2]), H5T_NATIVE_FLOAT);
    BOOST_ASSERT_MSG(h5_status >= 0, "Failed to create boost::qvm::vec<float, 3>.z");

    H5Handle rotation_type = H5Tcreate(H5T_COMPOUND, sizeof(boost::qvm::quat<float>));
    BOOST_ASSERT(rotation_type);
    h5_status = H5Tinsert(rotation_type, "q0", HOFFSET(boost::qvm::quat<float>, a[0]), H5T_NATIVE_FLOAT);
    BOOST_ASSERT_MSG(h5_status >= 0, "Failed to create boost::qvm::quat<float>.s");
    h5_status = H5Tinsert(rotation_type, "q1", HOFFSET(boost::qvm::quat<float>, a[1]), H5T_NATIVE_FLOAT);
    BOOST_ASSERT_MSG(h5_status >= 0, "Failed to create boost::qvm::quat<float>.x");
    h5_status = H5Tinsert(rotation_type, "q2", HOFFSET(boost::qvm::quat<float>, a[2]), H5T_NATIVE_FLOAT);
    BOOST_ASSERT_MSG(h5_status >= 0, "Failed to create boost::qvm::quat<float>.y");
    h5_status = H5Tinsert(rotation_type, "q3", HOFFSET(boost::qvm::quat<float>, a[3]), H5T_NATIVE_FLOAT);
    BOOST_ASSERT_MSG(h5_status >= 0, "Failed to create boost::qvm::quat<float>.z");

    H5Handle segment_type = H5Tcreate(H5T_COMPOUND, sizeof(MotionCaptureNode::Segment));
    BOOST_ASSERT(segment_type);
    h5_status = H5Tinsert(segment_type, "frame", HOFFSET(MotionCaptureNode::Segment, frame), H5T_NATIVE_UINT32);
    BOOST_ASSERT_MSG(h5_status >= 0, "Failed to create MotionCapture::Segment.frame");
    h5_status = H5Tinsert(segment_type, "segment_id", HOFFSET(MotionCaptureNode::Segment, segment_id), H5T_NATIVE_UINT32);
    BOOST_ASSERT_MSG(h5_status >= 0, "Failed to create MotionCapture::Segment.segment_id");
    h5_status = H5Tinsert(segment_type, "position", HOFFSET(MotionCaptureNode::Segment, position), position_type);
    BOOST_ASSERT_MSG(h5_status >= 0, "Failed to create MotionCapture::Segment.position");
    h5_status = H5Tinsert(segment_type, "rotation", HOFFSET(MotionCaptureNode::Segment, rotation), rotation_type);
    BOOST_ASSERT_MSG(h5_status >= 0, "Failed to create MotionCapture::Segment.rotation");

    return segment_type;
  }
}
