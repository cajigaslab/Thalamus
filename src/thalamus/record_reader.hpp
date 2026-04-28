#pragma once
#include <memory>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
#include <thalamus.pb.h>
#ifdef __clang__
#pragma clang diagnostic pop
#endif

namespace thalamus {
struct RecordReader {
  struct Impl;
  std::unique_ptr<Impl> impl;
  RecordReader(std::istream &_stream, bool _do_decode_video = true);
  ~RecordReader();
  std::optional<thalamus_grpc::StorageRecord> read_record();
  double progress();
};
}
