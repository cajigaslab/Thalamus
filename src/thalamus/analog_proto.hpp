#pragma once

// Conversions between AnalogNode and the AnalogResponse/Span protobuf
// messages, shared by everything that serializes or reads analog data.

#include <optional>
#include <thalamus/analog_node.hpp>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
#include <thalamus.pb.h>
#ifdef __clang__
#pragma clang diagnostic pop
#endif

namespace thalamus {

inline thalamus_grpc::Span::Format to_proto(AnalogNode::AnalogFormat format) {
  switch (format) {
  case AnalogNode::AnalogFormat::Double:
    return thalamus_grpc::Span::Format::Span_Format_Double;
  case AnalogNode::AnalogFormat::Short:
    return thalamus_grpc::Span::Format::Span_Format_Short;
  case AnalogNode::AnalogFormat::Int:
    return thalamus_grpc::Span::Format::Span_Format_Int;
  case AnalogNode::AnalogFormat::ULong:
    return thalamus_grpc::Span::Format::Span_Format_ULong;
  case AnalogNode::AnalogFormat::Encoded:
    return thalamus_grpc::Span::Format::Span_Format_Encoded;
  }
  return thalamus_grpc::Span::Format::Span_Format_Double;
}

/// nullopt for values from a newer Thalamus this build doesn't know. (An if
/// chain rather than a switch: protobuf enums are open, with sentinel values
/// a switch would have to list.)
inline std::optional<AnalogNode::AnalogFormat> from_proto(thalamus_grpc::Span::Format format) {
  if (format == thalamus_grpc::Span::Format::Span_Format_Double) {
    return AnalogNode::AnalogFormat::Double;
  } else if (format == thalamus_grpc::Span::Format::Span_Format_Short) {
    return AnalogNode::AnalogFormat::Short;
  } else if (format == thalamus_grpc::Span::Format::Span_Format_Int) {
    return AnalogNode::AnalogFormat::Int;
  } else if (format == thalamus_grpc::Span::Format::Span_Format_ULong) {
    return AnalogNode::AnalogFormat::ULong;
  } else if (format == thalamus_grpc::Span::Format::Span_Format_Encoded) {
    return AnalogNode::AnalogFormat::Encoded;
  }
  return std::nullopt;
}

inline thalamus_grpc::AnalogResponse::Encoding to_proto(AnalogNode::Encoding encoding) {
  switch (encoding) {
  case AnalogNode::Encoding::None:
    return thalamus_grpc::AnalogResponse::Encoding::AnalogResponse_Encoding_None;
  case AnalogNode::Encoding::AAC:
    return thalamus_grpc::AnalogResponse::Encoding::AnalogResponse_Encoding_AAC;
  }
  return thalamus_grpc::AnalogResponse::Encoding::AnalogResponse_Encoding_None;
}

/// nullopt for values from a newer Thalamus this build doesn't know.
inline std::optional<AnalogNode::Encoding> from_proto(thalamus_grpc::AnalogResponse::Encoding encoding) {
  if (encoding == thalamus_grpc::AnalogResponse::Encoding::AnalogResponse_Encoding_None) {
    return AnalogNode::Encoding::None;
  } else if (encoding == thalamus_grpc::AnalogResponse::Encoding::AnalogResponse_Encoding_AAC) {
    return AnalogNode::Encoding::AAC;
  }
  return std::nullopt;
}

/// The format of `span`'s samples in `response`. Messages written before
/// Span.format existed say Double for every span and mark int or ulong data
/// with is_int_data/is_ulong_data. nullopt for formats this build doesn't
/// know.
inline std::optional<AnalogNode::AnalogFormat> span_format(const thalamus_grpc::AnalogResponse &response,
                                                     const thalamus_grpc::Span &span) {
  if (span.format() == thalamus_grpc::Span::Format::Span_Format_Double) {
    if (response.is_int_data()) {
      return AnalogNode::AnalogFormat::Int;
    } else if (response.is_ulong_data()) {
      return AnalogNode::AnalogFormat::ULong;
    }
  }
  return from_proto(span.format());
}

/// Appends `channel` of `node` to `response` in its own format: its samples
/// to the matching array and a span pointing at them (an empty span for
/// encoded channels). Returns the span.
inline thalamus_grpc::Span *append_native_channel(thalamus_grpc::AnalogResponse &response,
                                                  const AnalogNode &node, int channel) {
  auto span = response.add_spans();
  auto name = node.name(channel);
  span->set_name(name.data(), name.size());
  span->set_format(to_proto(node.analog_format(channel)));
  if (node.is_transformed()) {
    span->set_scale(node.scale(channel));
    span->set_offset(node.offset(channel));
  }
  response.add_sample_intervals(uint64_t(node.sample_interval(channel).count()));

  visit_channel(&node, channel, [&](auto data) {
    using T = typename decltype(data)::value_type;
    if constexpr (std::is_same_v<T, uint64_t>) {
      span->set_begin(uint32_t(response.ulong_data_size()));
      response.mutable_ulong_data()->Add(data.begin(), data.end());
      span->set_end(uint32_t(response.ulong_data_size()));
    } else if constexpr (std::is_same_v<T, short> || std::is_same_v<T, int>) {
      span->set_begin(uint32_t(response.int_data_size()));
      response.mutable_int_data()->Add(data.begin(), data.end());
      span->set_end(uint32_t(response.int_data_size()));
    } else {
      span->set_begin(uint32_t(response.data_size()));
      response.mutable_data()->Add(data.begin(), data.end());
      span->set_end(uint32_t(response.data_size()));
    }
  });
  return span;
}

/// Copies `node`'s encoded buffer, encoding and encoded_count to `response`.
inline void set_encoded(thalamus_grpc::AnalogResponse &response, const AnalogNode &node) {
  auto buffer = node.buffer();
  response.set_buffer(buffer.data(), buffer.size());
  response.set_encoding(to_proto(node.encoding()));
  response.set_encoded_count(node.encoded_count());
}

/// Finishes a response built with append_native_channel: the encoded buffer
/// and, for readers that predate Span.format, is_int_data/is_ulong_data when
/// every array-backed channel is in that array.
inline void finish_native(thalamus_grpc::AnalogResponse &response, const AnalogNode &node) {
  bool all_int = true;
  bool all_ulong = true;
  bool any_array = false;
  for (const auto &span : response.spans()) {
    auto format = from_proto(span.format());
    if (format == AnalogNode::AnalogFormat::Encoded) {
      continue;
    }
    any_array = true;
    all_int &= format == AnalogNode::AnalogFormat::Short || format == AnalogNode::AnalogFormat::Int;
    all_ulong &= format == AnalogNode::AnalogFormat::ULong;
  }
  response.set_is_int_data(any_array && all_int);
  response.set_is_ulong_data(any_array && all_ulong);
  response.set_is_transformed(node.is_transformed());
  set_encoded(response, node);
}

} // namespace thalamus
