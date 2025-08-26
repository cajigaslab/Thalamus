#pragma once

#include <base_node.hpp>
#include <span>
#include <string>
#include <util.hpp>

namespace thalamus {
template <typename T> double interval_to_frequency(T interval) {
  using frequency = std::ratio_divide<std::ratio<1, 1>, typename T::period>;
  auto result = 1.0 * frequency::num / frequency::den / interval.count();
  return result;
}

class AnalogNode {
public:
  virtual ~AnalogNode();
  boost::signals2::signal<void(AnalogNode *)> channels_changed;
  virtual std::span<const double> data(int channel) const = 0;
  virtual std::span<const short> short_data(int) const {
    THALAMUS_ASSERT(false, "AnalogNode::short_data unimplemented");
    return std::span<const short>();
  }
  virtual int num_channels() const = 0;
  virtual std::chrono::nanoseconds sample_interval(int channel) const = 0;
  virtual std::chrono::nanoseconds time() const = 0;
  virtual std::chrono::nanoseconds remote_time() const { return 0ns; }
  virtual std::string_view name(int channel) const = 0;
  virtual std::span<const std::string> get_recommended_channels() const {
    return std::span<const std::string>();
  }
  virtual void inject(const thalamus::vector<std::span<double const>> &,
                      const thalamus::vector<std::chrono::nanoseconds> &,
                      const thalamus::vector<std::string_view> &) = 0;
  virtual bool has_analog_data() const { return true; }
  virtual bool is_short_data() const { return false; }
};

template <typename T> class AnalogNodeWrapper {
private:
  AnalogNode *underlying;

public:
  using value_type = T;
  AnalogNodeWrapper(AnalogNode *_underlying) : underlying(_underlying) {}
  std::span<const T> data(int channel) const {
    if constexpr (std::is_same<T, short>::value) {
      return underlying->short_data(channel);
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
};

template <typename T> void visit_node(AnalogNode *node, T callable) {
  if (node->is_short_data()) {
    AnalogNodeWrapper<short> wrapper(node);
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
  virtual void inject(const thalamus::vector<std::span<double const>> &,
                      const thalamus::vector<std::chrono::nanoseconds> &,
                      const thalamus::vector<std::string_view> &) override;
  virtual void inject(const thalamus::vector<std::span<double const>> &,
                      const thalamus::vector<std::chrono::nanoseconds> &,
                      const thalamus::vector<std::string_view> &,
                      std::chrono::nanoseconds);
  static std::string type_name();
  size_t modalities() const override;
};

class WaveGeneratorNode : public AnalogNode, public Node {
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
  inject(const thalamus::vector<std::span<double const>> &data,
         const thalamus::vector<std::chrono::nanoseconds> &sample_intervals,
         const thalamus::vector<std::string_view> &) override;

  std::chrono::nanoseconds sample_interval(int) const override;
  std::chrono::nanoseconds time() const override;
  std::string_view name(int channel) const override;
  std::span<const std::string> get_recommended_channels() const override;
  size_t modalities() const override;
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
  inject(const thalamus::vector<std::span<double const>> &data,
         const thalamus::vector<std::chrono::nanoseconds> &sample_intervals,
         const thalamus::vector<std::string_view> &) override;

  std::chrono::nanoseconds sample_interval(int) const override;
  std::chrono::nanoseconds time() const override;
  std::string_view name(int channel) const override;
  std::span<const std::string> get_recommended_channels() const override;
  size_t modalities() const override;
};
}; // namespace thalamus
