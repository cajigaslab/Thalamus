#pragma once

#include <thalamus/base_node.hpp>
#include <span>
#include <string>
#include <thalamus/util.hpp>
#include <thalamus/text_node.hpp>

namespace thalamus {
template <typename T> double interval_to_frequency(T interval) {
  using frequency = std::ratio_divide<std::ratio<1, 1>, typename T::period>;
  auto result = 1.0 * frequency::num / frequency::den / interval.count();
  return result;
}

class AnalogNode {
public:
  enum class Encoding {
    None,
    AAC,
    Opus
  };
  /// How a channel's samples are stored, i.e. which *data function reads
  /// them. Encoded channels have no samples in any of those: their samples
  /// are in buffer(), encoded_count() of them per channel.
  enum class AnalogFormat {
    Double,
    Short,
    Int,
    ULong,
    Encoded
  };
  virtual ~AnalogNode();
  /// Whether this message's channels differ from the previous message's:
  /// their count, names, formats or sample intervals. Only the first message
  /// with analog data after a change reports it, so a subscriber checks it
  /// on every message and treats the first message it sees as changed too.
  virtual bool channels_changed() const { return false; }
  virtual std::span<const double> data(int channel) const = 0;
  virtual std::span<const short> short_data(int) const {
    THALAMUS_ASSERT(false, "AnalogNode::short_data unimplemented");
    return std::span<const short>();
  }
  virtual std::span<const int> int_data(int) const {
    THALAMUS_ASSERT(false, "AnalogNode::int_data unimplemented");
    return std::span<const int>();
  }
  virtual std::span<const uint64_t> ulong_data(int) const {
    THALAMUS_ASSERT(false, "AnalogNode::ulong_data unimplemented");
    return std::span<const uint64_t>();
  }
  virtual int num_channels() const = 0;
  virtual std::chrono::nanoseconds sample_interval(int channel) const = 0;
  virtual std::chrono::nanoseconds time() const = 0;
  virtual std::chrono::nanoseconds remote_time() const { return 0ns; }
  virtual std::string_view name(int channel) const = 0;
  virtual std::span<const std::string> get_recommended_channels() const {
    return std::span<const std::string>();
  }
  /// `channels_changed` says whether the injected channels differ from the
  /// previous message's (count, names, formats or sample intervals); nodes
  /// that send the injected data report it through channels_changed().
  /// Does nothing by default.
  virtual void inject_analog(const thalamus::vector<std::span<double const>> &,
                      const thalamus::vector<std::chrono::nanoseconds> &,
                      const thalamus::vector<std::string_view> &,
                      bool /* channels_changed */ = false) {}
  virtual bool has_analog_data() const { return true; }
  virtual bool is_short_data() const { return false; }
  virtual bool is_int_data() const { return false; }
  virtual bool is_ulong_data() const { return false; }

  virtual bool is_transformed() const { return false; }
  virtual double scale(int) const { return 1.0; }
  virtual double offset(int) const { return 0.0; }
  virtual Encoding encoding() const { return Encoding::None; }
  virtual std::span<const uint8_t> buffer() const { return std::span<const uint8_t>(); }

  /// The format of `channel`. The default gives every channel the format the
  /// is_* functions select, so only nodes with mixed formats or encoded
  /// channels need to override it.
  virtual AnalogFormat analog_format(int) const {
    if (is_short_data()) {
      return AnalogFormat::Short;
    } else if (is_int_data()) {
      return AnalogFormat::Int;
    } else if (is_ulong_data()) {
      return AnalogFormat::ULong;
    }
    return AnalogFormat::Double;
  }
  /// The number of samples per encoded channel in buffer(): what decoding
  /// it will eventually produce for this message, even if the encoder hasn't
  /// output them yet.
  virtual size_t encoded_count() const { return 0; }
};

/// Calls `callable` with `channel`'s samples as a span of its format's type.
/// Encoded channels have no samples to pass: `callable` isn't called and
/// false is returned.
template <typename T> bool visit_channel(const AnalogNode *node, int channel, T &&callable) {
  switch (node->analog_format(channel)) {
  case AnalogNode::AnalogFormat::Double:
    callable(node->data(channel));
    return true;
  case AnalogNode::AnalogFormat::Short:
    callable(node->short_data(channel));
    return true;
  case AnalogNode::AnalogFormat::Int:
    callable(node->int_data(channel));
    return true;
  case AnalogNode::AnalogFormat::ULong:
    callable(node->ulong_data(channel));
    return true;
  case AnalogNode::AnalogFormat::Encoded:
    return false;
  }
  return false;
}

template <typename T> class AnalogNodeWrapper {
private:
  AnalogNode *underlying;

public:
  using value_type = T;
  AnalogNodeWrapper(AnalogNode *_underlying) : underlying(_underlying) {}
  std::span<const T> data(int channel) const {
    if constexpr (std::is_same<T, short>::value) {
      return underlying->short_data(channel);
    } else if constexpr (std::is_same<T, int>::value) {
      return underlying->int_data(channel);
    } else if constexpr (std::is_same<T, uint64_t>::value) {
      return underlying->ulong_data(channel);
    } else {
      return underlying->data(channel);
    }
  }
  int num_channels() const { return underlying->num_channels(); }
  std::chrono::nanoseconds sample_interval(int channel) const {
    return underlying->sample_interval(channel);
  }
  std::chrono::nanoseconds time() const { return underlying->time(); }
  std::string_view name(int channel) const { return underlying->name(channel); }

  bool is_transformed() const { return underlying->is_transformed(); }
  double scale(int i) const { return underlying->scale(i); }
  double offset(int i) const { return underlying->offset(i); }
};

template <typename T> void visit_node(AnalogNode *node, T callable) {
  if (node->is_short_data()) {
    AnalogNodeWrapper<short> wrapper(node);
    callable(&wrapper);
  } else if (node->is_int_data()) {
    AnalogNodeWrapper<int> wrapper(node);
    callable(&wrapper);
  } else if (node->is_ulong_data()) {
    AnalogNodeWrapper<uint64_t> wrapper(node);
    callable(&wrapper);
  } else {
    AnalogNodeWrapper<double> wrapper(node);
    callable(&wrapper);
  }
}

class AnalogNodeImpl : public Node, public AnalogNode {
  struct Impl;
  std::unique_ptr<Impl> impl;

public:
  AnalogNodeImpl(ObservableDictPtr state, boost::asio::io_context &,
                 NodeGraph *graph);
  AnalogNodeImpl();
  ~AnalogNodeImpl() override;
  virtual std::span<const double> data(int channel) const override;
  virtual int num_channels() const override;
  virtual std::chrono::nanoseconds sample_interval(int channel) const override;
  virtual std::chrono::nanoseconds time() const override;
  std::string_view name(int channel) const override;
  std::span<const std::string> get_recommended_channels() const override;
  /// The channels_changed argument of the inject_analog() that sent this
  /// message.
  bool channels_changed() const override;
  virtual void inject_analog(const thalamus::vector<std::span<double const>> &,
                      const thalamus::vector<std::chrono::nanoseconds> &,
                      const thalamus::vector<std::string_view> &,
                      bool channels_changed = false) override;
  virtual void inject_analog(const thalamus::vector<std::span<double const>> &,
                      const thalamus::vector<std::chrono::nanoseconds> &,
                      const thalamus::vector<std::string_view> &,
                      std::chrono::nanoseconds, bool channels_changed = false);
  static std::string type_name();
  size_t modalities() const override;
};

class WaveGeneratorNode : public AnalogNode, public TextNode, public Node {
  struct Impl;
  std::unique_ptr<Impl> impl;

public:
  WaveGeneratorNode(ObservableDictPtr state,
                    boost::asio::io_context &io_context, NodeGraph *graph);

  ~WaveGeneratorNode() override;

  static std::string type_name();

  std::span<const double> data(int index) const override;

  int num_channels() const override;

  void
  inject_analog(const thalamus::vector<std::span<double const>> &data,
         const thalamus::vector<std::chrono::nanoseconds> &sample_intervals,
         const thalamus::vector<std::string_view> &, bool) override;

  std::chrono::nanoseconds sample_interval(int) const override;
  std::chrono::nanoseconds time() const override;
  std::string_view name(int channel) const override;
  std::span<const std::string> get_recommended_channels() const override;
  size_t modalities() const override;
  
  std::string_view text() const override;
  bool has_text_data() const override;
  bool has_analog_data() const override;
  bool channels_changed() const override;
};

class ToggleNode : public AnalogNode, public Node {
  struct Impl;
  std::unique_ptr<Impl> impl;

public:
  ToggleNode(ObservableDictPtr state, boost::asio::io_context &io_context,
             NodeGraph *graph);
  ~ToggleNode() override;

  static std::string type_name();

  std::span<const double> data(int i) const override;
  int num_channels() const override;

  void
  inject_analog(const thalamus::vector<std::span<double const>> &data,
         const thalamus::vector<std::chrono::nanoseconds> &sample_intervals,
         const thalamus::vector<std::string_view> &, bool) override;

  std::chrono::nanoseconds sample_interval(int) const override;
  std::chrono::nanoseconds time() const override;
  std::string_view name(int channel) const override;
  std::span<const std::string> get_recommended_channels() const override;
  size_t modalities() const override;
  bool channels_changed() const override;
};
}; // namespace thalamus
