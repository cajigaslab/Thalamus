#include <thalamus/tracing.hpp>
#include <alpha_omega_node.hpp>
#include <modalities_util.hpp>
#include <regex>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include <absl/strings/numbers.h>

#ifdef _WIN32

#include <AOSystemAPI.h>

#else
enum {
  eAO_OK,
  eAO_NOT_CONNECTED,
  eAO_CONNECTING,
  eAO_CONNECTED,
  eAO_BAD_ARG,
  eAO_ARG_NULL,
  eAO_DISCONNECTED,
  eAO_FAIL
};

struct SInformation {
  int channelID;
  char channelName[256];
};

struct MAC_ADDR {
  int addr[6];
};

using int32 = int;
using uint32 = unsigned int;
using ULONG = unsigned long;
using int16 = short;
using cChar = char;

auto MOCK_ERROR = "Mock Error";
;

int ErrorHandlingfunc(int *pErrorCount, cChar *sError, int nError) {
  if (pErrorCount == nullptr || sError == nullptr) {
    return eAO_ARG_NULL;
  }
  if (nError <= 0) {
    return eAO_BAD_ARG;
  }

  *pErrorCount = -1;
  strncpy(sError, MOCK_ERROR, nError);
  return eAO_OK;
}

#endif

#ifdef __clang__
#pragma clang diagnostic pop
#endif

namespace thalamus {
using namespace std::placeholders;

#ifdef _WIN32
static HMODULE alphaomega_handle = nullptr;

struct AlphaOmegaAPI {
  bool loaded = false;
  decltype(&::DefaultStartConnection) DefaultStartConnection;
  decltype(&::isConnected) isConnected;
  decltype(&::AddBufferChannel) AddBufferChannel;
  decltype(&::GetAlignedData) GetAlignedData;
  decltype(&::GetChannelsCount) GetChannelsCount;
  decltype(&::GetAllChannels) GetAllChannels;
  decltype(&::ErrorHandlingfunc) ErrorHandlingfunc;
  decltype(&::CloseConnection) CloseConnection;
};
static AlphaOmegaAPI *aoapi;
#endif

static std::chrono::nanoseconds channel_sample_interval(int channel_id) {
  if (10'000 <= channel_id && channel_id <= 10'127) {
    auto raw_interval = std::chrono::duration<std::chrono::nanoseconds::rep,
                                              std::ratio<1, 1'375>>(1);
    auto result =
        std::chrono::duration_cast<std::chrono::nanoseconds>(raw_interval);
    return result;
  } else if ((10'128 <= channel_id && channel_id <= 10'271) ||
             (10'384 <= channel_id && channel_id <= 10'399) ||
             (11'348 <= channel_id && channel_id <= 11'351)) {
    auto raw_interval = std::chrono::duration<std::chrono::nanoseconds::rep,
                                              std::ratio<1, 44'000>>(1);
    auto result =
        std::chrono::duration_cast<std::chrono::nanoseconds>(raw_interval);
    return result;
  } else if ((10'272 <= channel_id && channel_id <= 10'383) ||
             (10'400 <= channel_id && channel_id <= 10'511)) {
    auto raw_interval = std::chrono::duration<std::chrono::nanoseconds::rep,
                                              std::ratio<1, 22'000>>(1);
    auto result =
        std::chrono::duration_cast<std::chrono::nanoseconds>(raw_interval);
    return result;
  } else {
    return 1s;
  }
  BOOST_ASSERT_MSG(false, "channel ID outside supported range");
  return std::chrono::nanoseconds();
}

class AlphaOmega {
public:
  virtual ~AlphaOmega();
  virtual int isConnected() = 0;
  virtual int DefaultStartConnection(MAC_ADDR *pSystemMAC) = 0;
  virtual int CloseConnection() = 0;
  virtual int GetChannelsCount(uint32 *pChannelsCount) = 0;
  virtual int GetAllChannels(SInformation *pChannelsInfo,
                             int32 nChannelsInfo) = 0;
  virtual int AddBufferChannel(int nChannelID, int nBufferingTime_mSec) = 0;
  virtual int GetAlignedData(int16 *pData, int nData, int *pDataCapture,
                             int *pChannels, int nChannels,
                             ULONG *pBeginTS = nullptr) = 0;
};
AlphaOmega::~AlphaOmega() {}

#ifdef _WIN32
class RealAlphaOmega : public AlphaOmega {
  int count = 0;

public:
  RealAlphaOmega() {}
  int isConnected() override { return aoapi->isConnected(); }
  int DefaultStartConnection(MAC_ADDR *pSystemMAC) override {
    if (count) {
      ++count;
      return eAO_OK;
    } else {
      auto result = aoapi->DefaultStartConnection(pSystemMAC, nullptr);
      count += result == eAO_OK ? 1 : 0;
      return result;
    }
  }
  int CloseConnection() override {
    count = std::max(count - 1, 0);
    if (count) {
      return eAO_OK;
    } else {
      return aoapi->CloseConnection();
    }
  }
  int GetChannelsCount(uint32 *pChannelsCount) override {
    return aoapi->GetChannelsCount(pChannelsCount);
  }
  int GetAllChannels(SInformation *pChannelsInfo,
                     int32 nChannelsInfo) override {
    return aoapi->GetAllChannels(pChannelsInfo, nChannelsInfo);
  }
  int AddBufferChannel(int nChannelID, int nBufferingTime_mSec) override {
    return aoapi->AddBufferChannel(nChannelID, nBufferingTime_mSec);
  }
  int GetAlignedData(int16 *pData, int nData, int *pDataCapture, int *pChannels,
                     int nChannels, ULONG *pBeginTS = nullptr) override {
    return aoapi->GetAlignedData(pData, nData, pDataCapture, pChannels,
                                 nChannels, pBeginTS);
  }
};
#endif

static int total_captured = 0;

class MockAlphaOmega : public AlphaOmega {

  // struct Impl {
  //   typedef int (*DefaultStartConnectionType)(MAC_ADDR*, AOParseFunction);
  //   typedef int (*isConnectedType)();
  //   typedef int (*AddBufferChannelType)(int, int);
  //   typedef int (*GetAlignedDataType)(int16*, int, int*, int*, int, ULONG*);
  //   typedef int (*GetChannelsCountType)(uint32*);
  //   typedef int (*GetAllChannelsType)(SInformation* pChannelsInfo, int32
  //   nChannelsInfo);
  //
  //   DefaultStartConnectionType DefaultStartConnection = nullptr;
  //   isConnectedType isConnected = nullptr;
  //   AddBufferChannelType AddBufferChannel = nullptr;
  //   GetAlignedDataType GetAlignedData = nullptr;
  //   GetChannelsCountType GetChannelsCount = nullptr;
  //   GetAllChannelsType GetAllChannels = nullptr;
  // } impl;
public:
  std::vector<int> channels;
  std::vector<std::chrono::steady_clock::time_point> last_time;
  std::chrono::steady_clock::time_point start_time;

  std::map<int,
           std::function<double(const std::chrono::steady_clock::duration &)>>
      waves;

  MockAlphaOmega() {
    waves = {{10'000,
              [](const std::chrono::steady_clock::duration &t) {
                // auto nanoseconds =
                // std::chrono::duration_cast<std::chrono::nanoseconds>(t); auto
                // seconds = nanoseconds.count() / 1e9; auto result =
                // std::sin(seconds); return result;
                auto nanoseconds =
                    std::chrono::duration_cast<std::chrono::nanoseconds>(t);
                return (nanoseconds.count() % 3'140'000'000) / 2e9;
              }},
             {10'272, [](const std::chrono::steady_clock::duration &t) {
                auto nanoseconds =
                    std::chrono::duration_cast<std::chrono::nanoseconds>(t);
                return std::sin(double(nanoseconds.count()) / 1e9 + M_PI_4);
              }}};
  }
  int isConnected() override;
  int DefaultStartConnection(MAC_ADDR *) override {
    channels.clear();
    start_time = std::chrono::steady_clock::now();
    last_time.clear();
    return eAO_OK;
  }
  int CloseConnection() override { return eAO_OK; }
  int GetChannelsCount(uint32 *pChannelsCount) override {
    if (isConnected() == eAO_CONNECTED) {
      *pChannelsCount = 2;
      return eAO_OK;
    }
    *pChannelsCount = 0;
    return eAO_NOT_CONNECTED;
  }
  int GetAllChannels(SInformation *pChannelsInfo,
                     int32 nChannelsInfo) override {
    if (isConnected() == eAO_CONNECTED) {
      if (nChannelsInfo >= 2) {
        pChannelsInfo[0].channelID = 10'000;
        strcpy(pChannelsInfo[0].channelName, "SPK 01");
        pChannelsInfo[1].channelID = 10'272;
        strcpy(pChannelsInfo[1].channelName, "SPK 02");
        return eAO_OK;
      }
      return eAO_BAD_ARG;
    }
    return eAO_NOT_CONNECTED;
  }
  int AddBufferChannel(int nChannelID, int) override {
    if (isConnected() == eAO_CONNECTED) {
      channels.push_back(nChannelID);
      return eAO_OK;
    }
    return eAO_NOT_CONNECTED;
  }
  int GetAlignedData(int16 *pData, int nData, int *pDataCapture, int *pChannels,
                     int nChannels, ULONG *pBeginTS = nullptr) override {
    if (isConnected() == eAO_CONNECTING) {
      return eAO_NOT_CONNECTED;
    }

    auto now = std::chrono::steady_clock::now();
    if (last_time.empty()) {
      last_time.resize(2, now);
    }
    std::chrono::steady_clock::time_point &selected_last_time =
        *pChannels == 10'000 ? this->last_time[0] : this->last_time[1];
    *pBeginTS = uint32_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                             selected_last_time - start_time)
                             .count());

    *pDataCapture = 0;
    auto time_reached = selected_last_time;
    for (auto c = 0; c < nChannels; ++c) {
      auto i = 0;
      auto current_time = selected_last_time;
      auto offset = c * (*pDataCapture);
      auto channel_id = pChannels[c];
      auto time_per_sample = channel_sample_interval(channel_id);
      while (current_time <= now) {
        if ((i + 1) * nChannels > nData) {
          break;
        }
        auto sample = waves[channel_id](current_time - start_time);
        auto digitized = int16(std::numeric_limits<short>::max() * sample);
        pData[offset + i++] = digitized;
        current_time += time_per_sample;
      }
      if (c == 0) {
        *pDataCapture = i;
        time_reached = current_time;
      }
    }
    selected_last_time = time_reached;
    return eAO_OK;
  }
};

int MockAlphaOmega::isConnected() {
  return rand() % 10 == 0 ? eAO_CONNECTING : eAO_CONNECTED;
}

struct AlphaOmegaNode::Impl {
  boost::asio::io_context &io_context;
  boost::asio::steady_timer ao_timer;
  boost::asio::steady_timer timer;
  std::chrono::steady_clock::duration ao_interval = 10ms;
  uint32 channel_count;
  thalamus::vector<SInformation> ao_channels;
  ObservableDictPtr state;
  thalamus::vector<short> short_buffer;
  thalamus::vector<double> double_buffer;
  thalamus::vector<std::span<const double>> spans;
  thalamus::vector<std::vector<int>> bands;
  thalamus::vector<size_t> counts;
  thalamus::vector<double> frequencies;
  std::chrono::steady_clock::time_point last_frequency_update;
  AlphaOmegaNode *outer;
  thalamus::vector<std::pair<int, std::string>> channel_ids;
  thalamus::vector<std::string> channel_names;
  thalamus::map<std::string, int> name_to_id;
  size_t current_band;
  bool is_running = false;
  int address[6];
  int captured;
  int _num_channels = 0;
  size_t next_async_id = 1;
  boost::signals2::scoped_connection state_connection;
  static AlphaOmega *alpha_omega;
  static std::chrono::nanoseconds duration;
  static std::chrono::nanoseconds next_duration;
  thalamus::vector<std::chrono::nanoseconds> sample_interval_overrides;
  std::chrono::nanoseconds time;

  Impl(boost::asio::io_context &_io_context, ObservableDictPtr _state,
       AlphaOmegaNode *_outer)
      : io_context(_io_context), ao_timer(_io_context), timer(_io_context),
        channel_count(0), state(_state), outer(_outer) {}

  void load_channels() {
    THALAMUS_LOG(info) << "loading channels";
    auto loader = [this] {
      auto current = std::make_shared<ObservableDict>();
      THALAMUS_LOG(info) << name_to_id.size();
      for (const auto &i : name_to_id) {
        THALAMUS_LOG(info) << i.first << " " << i.second;
        auto current_channel = std::make_shared<ObservableDict>();
        (*current_channel)["Name"].assign(i.first);
        (*current_channel)["ID"].assign(i.second);
        auto interval = channel_sample_interval(i.second);
        auto frequency =
            std::chrono::nanoseconds::period::den / interval.count();
        (*current_channel)["Frequency"].assign(frequency);
        (*current_channel)["Selected"].assign(false);
        (*current)[i.second].assign(current_channel);
      }
      (*state)["all_channels"].assign(current, [current] {
        auto temp = ObservableDict::to_string(current);
        THALAMUS_LOG(info) << "AlphaOmega channels updated " << temp;
      });
    };
    if (name_to_id.empty()) {
      if (is_running) {
        loader();
      } else {
        StartConnection([this, loader] {
          GetChannelsCount([this, loader] {
            GetAllChannels([loader] {
              Impl::alpha_omega->CloseConnection();
              loader();
            });
          });
        });
      }
    } else {
      loader();
    }
  }

  void wait_for_connection(const boost::system::error_code &error,
                           std::function<void()> callback,
                           size_t async_id = 0) {
    if (error.value() == boost::asio::error::operation_aborted) {
      return;
    }
    BOOST_ASSERT(!error);
    if (!async_id) {
      async_id = next_async_id++;
      // TRACE_EVENT_ASYNC_BEGIN0("thalamus", "wait_for_connection", async_id);
    }

    if (eAO_CONNECTED == alpha_omega->isConnected()) {
      // TRACE_EVENT_ASYNC_END0("thalamus", "wait_for_connection", async_id);
      callback();
      return;
    }

    ao_timer.expires_after(16ms);
    ao_timer.async_wait(
        std::bind(&Impl::wait_for_connection, this, _1, callback, async_id));
  }

  void StartConnection(std::function<void()> callback) {
    TRACE_EVENT("thalamus", "StartConnection");
    MAC_ADDR mac_address;
    mac_address.addr[0] = address[0];
    mac_address.addr[1] = address[1];
    mac_address.addr[2] = address[2];
    mac_address.addr[3] = address[3];
    mac_address.addr[4] = address[4];
    mac_address.addr[5] = address[5];

    auto ao_error = alpha_omega->DefaultStartConnection(&mac_address);
    if (ao_error != eAO_OK) {
      show_error();
      (*state)["Running"].assign(false);
      return;
    }

    wait_for_connection(boost::system::error_code(), callback);
  }

  void GetChannelsCount(
      std::function<void()> callback,
      const boost::system::error_code &error = boost::system::error_code(),
      bool initial = true, size_t async_id = 0) {
    if (error.value() == boost::asio::error::operation_aborted) {
      return;
    }
    BOOST_ASSERT(!error);
    if (!async_id) {
      async_id = next_async_id++;
      // TRACE_EVENT_ASYNC_BEGIN0("thalamus", "GetChannelsCount", async_id);
    }
    if (initial) {
      channel_count = 0;
    }

    uint32 new_channel_count;
    auto ao_error = alpha_omega->GetChannelsCount(&new_channel_count);
    BOOST_ASSERT(ao_error == eAO_OK || ao_error == eAO_NOT_CONNECTED);
    if (ao_error == eAO_NOT_CONNECTED) {
      BOOST_ASSERT_MSG(eAO_DISCONNECTED != alpha_omega->isConnected(),
                       "AlphaOmega disconnected");
    }

    if (new_channel_count && new_channel_count == channel_count) {
      // TRACE_EVENT_ASYNC_END0("thalamus", "GetChannelsCount", async_id);
      callback();
      return;
    }
    channel_count = new_channel_count;
    ao_timer.expires_after(ao_interval);
    ao_timer.async_wait(std::bind(&Impl::GetChannelsCount, this, callback, _1,
                                  false, async_id));
  }

  void GetAllChannels(
      std::function<void()> callback,
      const boost::system::error_code &error = boost::system::error_code(),
      bool initial = true, size_t async_id = 0) {
    if (error.value() == boost::asio::error::operation_aborted) {
      return;
    }
    BOOST_ASSERT(!error);
    if (!async_id) {
      async_id = next_async_id++;
      // TRACE_EVENT_ASYNC_BEGIN0("thalamus", "GetAllChannels", async_id);
    }
    if (initial) {
      ao_channels.resize(channel_count);
    }
    auto ao_error = alpha_omega->GetAllChannels(ao_channels.data(),
                                                int(ao_channels.size()));
    BOOST_ASSERT(ao_error == eAO_OK || ao_error == eAO_NOT_CONNECTED);
    if (ao_error == eAO_NOT_CONNECTED) {
      BOOST_ASSERT_MSG(eAO_DISCONNECTED != alpha_omega->isConnected(),
                       "AlphaOmega disconnected");
    }
    if (ao_error == eAO_OK) {
      parse_channels();
      // TRACE_EVENT_ASYNC_END0("thalamus", "GetAllChannels", async_id);
      callback();
      return;
    }
    ao_timer.expires_after(ao_interval);
    ao_timer.async_wait(
        std::bind(&Impl::GetAllChannels, this, callback, _1, false, async_id));
  }

  void AddBufferChannels(
      const std::vector<std::pair<int, std::string>> &channels,
      std::function<void()> callback,
      const boost::system::error_code &error = boost::system::error_code(),
      size_t async_id = 0) {
    if (channels.empty()) {
      return;
    }
    if (error.value() == boost::asio::error::operation_aborted) {
      return;
    }
    BOOST_ASSERT(!error);
    if (!async_id) {
      async_id = next_async_id++;
      // TRACE_EVENT_ASYNC_BEGIN0("thalamus", "AddBufferChannels", async_id);
    }
    auto ao_error =
        alpha_omega->AddBufferChannel(channels.back().first, 20'000);
    BOOST_ASSERT(ao_error == eAO_OK || ao_error == eAO_NOT_CONNECTED);
    if (ao_error == eAO_NOT_CONNECTED) {
      BOOST_ASSERT_MSG(eAO_DISCONNECTED != alpha_omega->isConnected(),
                       "AlphaOmega disconnected");
    }

    auto remaining_channels = channels;
    if (ao_error == eAO_OK) {
      remaining_channels.pop_back();
      if (remaining_channels.empty()) {
        // TRACE_EVENT_ASYNC_END0("thalamus", "AddBufferChannels", async_id);
        timer.expires_after(ao_interval);
        timer.async_wait(
            [callback](const boost::system::error_code &) { callback(); });
        // callback();
        return;
      }
    }
    ao_timer.expires_after(ao_interval);
    ao_timer.async_wait(std::bind(&Impl::AddBufferChannels, this,
                                  remaining_channels, callback, _1, async_id));
  }

  std::vector<std::pair<int, std::string>> parse_channels() {
    TRACE_EVENT("thalamus", "parse_channels");
    channel_ids.clear();
    auto &channels = channel_ids;
    name_to_id.clear();
    std::transform(ao_channels.begin(), ao_channels.end(),
                   std::inserter(name_to_id, name_to_id.begin()), [](auto &s) {
                     return std::pair<std::string, int>(s.channelName,
                                                        s.channelID);
                   });

    ObservableDictPtr all_channels = state->at("all_channels");
    for (auto i = all_channels->begin(); i != all_channels->end(); ++i) {
      ObservableDictPtr value = i->second;
      bool is_selected = value->at("Selected");
      long long int id = value->at("ID");
      std::string name = value->at("Name");
      if (is_selected) {
        channels.emplace_back(int(id), name);
      }
    }
    _num_channels = int(channels.size());

    bands.assign(3, std::vector<int>());
    std::vector<decltype(channel_ids)> new_channels(bands.size());
    for (auto channel : channel_ids) {
      auto channel_id = channel.first;
      if (10'000 <= channel_id && channel_id <= 10'127) {
        bands[0].push_back(channel_id);
        new_channels[0].push_back(channel);
      } else if ((10'128 <= channel_id && channel_id <= 10'271) ||
                 (10'384 <= channel_id && channel_id <= 10'399) ||
                 (11'348 <= channel_id && channel_id <= 11'351)) {
        bands[1].push_back(channel_id);
        new_channels[1].push_back(channel);
      } else if ((10'272 <= channel_id && channel_id <= 10'383) ||
                 (10'400 <= channel_id && channel_id <= 10'511)) {
        bands[2].push_back(channel_id);
        new_channels[2].push_back(channel);
      }
    }
    channel_ids.clear();
    for (auto &c : new_channels) {
      channel_ids.insert(channel_ids.end(), c.begin(), c.end());
    }
    channel_names.clear();
    for (auto &c : channel_ids) {
      channel_names.push_back(c.second);
    }
    for (auto &c : channel_ids) {
      channel_names.push_back(c.second + " Frequency");
    }

    return channels;
  }

  void on_timer(const boost::system::error_code &error,
                std::chrono::milliseconds polling_interval,
                size_t async_id = 0) {
    TRACE_EVENT("thalamus", "AlphaOmegaNode::on_timer");
    if (error.value() == boost::asio::error::operation_aborted ||
        is_running == false) {
      return;
    }
    BOOST_ASSERT(!error);

    if (!async_id) {
      async_id = next_async_id++;
      // TRACE_EVENT_ASYNC_BEGIN0("thalamus", "AlphaOmegaNode::on_timer",
      // async_id);
    }

    ULONG timestamp;

    short_buffer.resize(30'000);
    for (auto band_i = current_band; band_i < bands.size(); ++band_i) {
      auto &band = bands[band_i];
      if (band.empty()) {
        ++current_band;
        continue;
      }
      auto time_per_sample = channel_sample_interval(band[0]);

      int ao_error = alpha_omega->GetAlignedData(
          short_buffer.data(), int(short_buffer.size()), &captured, band.data(),
          int(band.size()), &timestamp);
      BOOST_ASSERT(ao_error == eAO_OK || ao_error == eAO_NOT_CONNECTED ||
                   ao_error == eAO_FAIL);
      if (ao_error == eAO_NOT_CONNECTED) {
        BOOST_ASSERT_MSG(eAO_DISCONNECTED != alpha_omega->isConnected(),
                         "AlphaOmega disconnected");
      }
      if (ao_error != eAO_OK) {
        timer.expires_after(ao_interval);
        timer.async_wait(
            std::bind(&Impl::on_timer, this, _1, polling_interval, async_id));
        return;
      }
      ++current_band;
      captured /= band.size();
      next_duration =
          std::max(duration + captured * time_per_sample, next_duration);
      if (captured) {
        auto initial_length = double_buffer.size();
        double_buffer.resize(double_buffer.size() +
                             size_t(captured) * band.size());
        for (auto i = 0u; i < size_t(captured) * band.size(); ++i) {
          double_buffer.at(initial_length + i) = short_buffer.at(i);
        }
        counts.insert(counts.end(), band.size(), size_t(captured));
      }
    }
    current_band = 0;

    spans.clear();
    frequencies.resize(counts.size(), 0);
    size_t position = 0;
    for (auto count : counts) {
      auto position_i = double_buffer.begin() + int64_t(position);
      frequencies.at(spans.size()) += double(count);
      spans.emplace_back(position_i, position_i + int64_t(count));
      position += count;
    }

    auto now = std::chrono::steady_clock::now();
    auto elapsed = now - last_frequency_update;
    auto frequencies_updated = elapsed >= 1s;
    if (frequencies_updated) {
      auto seconds = double(elapsed.count()) / decltype(elapsed)::period::den;
      for (auto i = frequencies.begin(); i != frequencies.end(); ++i) {
        *i /= seconds;
        spans.emplace_back(i, i + 1);
      }
      last_frequency_update = now;
    } else {
      for (auto i = frequencies.begin(); i != frequencies.end(); ++i) {
        spans.emplace_back();
      }
    }

    duration = next_duration;
    total_captured += captured;
    time = now.time_since_epoch();
    outer->ready(outer);
    // TRACE_EVENT_ASYNC_END0("thalamus", "AlphaOmegaNode::on_timer", async_id);
    double_buffer.clear();
    counts.clear();
    if (frequencies_updated) {
      frequencies.assign(frequencies.size(), 0);
    }

    timer.expires_after(polling_interval);
    timer.async_wait(std::bind(&Impl::on_timer, this, _1, polling_interval, 0));
  }

  int error_count;
  char error_message[2048];
  void show_error() {
#ifdef _WIN32
    aoapi->ErrorHandlingfunc(&error_count, error_message,
                             sizeof(error_message));
    THALAMUS_LOG(error) << error_message;
#else
    THALAMUS_LOG(error) << "ERROR";
#endif
  }
};

std::chrono::nanoseconds AlphaOmegaNode::Impl::duration;
std::chrono::nanoseconds AlphaOmegaNode::Impl::next_duration;
AlphaOmega *AlphaOmegaNode::Impl::alpha_omega;

AlphaOmegaNode::AlphaOmegaNode(ObservableDictPtr state,
                               boost::asio::io_context &io_context, NodeGraph *)
    : impl(new Impl(io_context, state, this)) {
  using namespace std::placeholders;
  impl->state_connection = state->changed.connect(
      std::bind(&AlphaOmegaNode::on_change, this, _1, _2, _3));
  memset(&impl->address, 0, sizeof(impl->address));
  impl->state->recap(std::bind(&AlphaOmegaNode::on_change, this, _1, _2, _3));
}

AlphaOmegaNode::~AlphaOmegaNode() { (*impl->state)["Running"].assign(false); }

// QWidget* AlphaOmegaNode::create_widget() {
//   return new Plot(impl->state, this, impl->io_context);
// }
std::span<const double> AlphaOmegaNode::data(int channel) const {
  auto uchannel = size_t(channel);
  if (uchannel >= impl->spans.size()) {
    return std::span<const double>();
  }
  return impl->spans.at(uchannel);
}
std::chrono::nanoseconds AlphaOmegaNode::sample_interval(int i) const {
  auto ui = size_t(i);
  if (!impl->sample_interval_overrides.empty()) {
    if (ui >= impl->sample_interval_overrides.size()) {
      return 0ns;
    }
    return impl->sample_interval_overrides.at(size_t(i));
  }
  if (ui >= 2 * impl->channel_ids.size()) {
    return 0ns;
  }
  if (ui >= impl->channel_ids.size()) {
    return 1s;
  }
  auto channel_id = impl->channel_ids[size_t(i)];
  return channel_sample_interval(channel_id.first);
}

std::string_view AlphaOmegaNode::name(int channel) const {
  auto uchannel = size_t(channel);
  if (uchannel < impl->channel_names.size()) {
    return impl->channel_names.at(uchannel);
  }
  return "";
}

std::span<const std::string> AlphaOmegaNode::get_recommended_channels() const {
  return std::span<const std::string>();
}

int AlphaOmegaNode::num_channels() const { return 2 * impl->_num_channels; }

void AlphaOmegaNode::inject(
    const thalamus::vector<std::span<double const>> &data,
    const thalamus::vector<std::chrono::nanoseconds> &sample_intervals,
    const thalamus::vector<std::string_view> &) {
  impl->spans.assign(data.begin(), data.end());
  impl->sample_interval_overrides = sample_intervals;
  impl->time = std::chrono::steady_clock::now().time_since_epoch();
  ready(this);
  impl->sample_interval_overrides.clear();
  impl->_num_channels = int(data.size());
}

void AlphaOmegaNode::on_change(ObservableCollection::Action,
                               const ObservableCollection::Key &k,
                               const ObservableCollection::Value &v) {
  auto key_str = std::get<std::string>(k);
  if (key_str == "MAC Address") {
    auto mac_str = std::get<std::string>(v);
    std::regex mac_regex(
        "(\\w{2})[:-](\\w{2})[:-](\\w{2})[:-](\\w{2})[:-](\\w{2})[:-](\\w{2})");
    std::smatch match_result;
    if (!std::regex_match(mac_str, match_result, mac_regex)) {
      // QMessageBox::warning(nullptr, "Parse Failed", "Failed to parse MAC
      // address");
      return;
    }
    impl->address[0] = int(std::stoul(match_result[1].str(), nullptr, 16));
    impl->address[1] = int(std::stoul(match_result[2].str(), nullptr, 16));
    impl->address[2] = int(std::stoul(match_result[3].str(), nullptr, 16));
    impl->address[3] = int(std::stoul(match_result[4].str(), nullptr, 16));
    impl->address[4] = int(std::stoul(match_result[5].str(), nullptr, 16));
    impl->address[5] = int(std::stoul(match_result[6].str(), nullptr, 16));
  } else if (key_str == "Running") {
    impl->is_running = std::get<bool>(v);
    if (impl->is_running) {
      impl->StartConnection([this] {
        impl->GetChannelsCount([this] {
          impl->GetAllChannels([this] {
            impl->AddBufferChannels(impl->channel_ids, [this] {
              size_t polling_interval = impl->state->at("Polling Interval");
              impl->current_band = 0;
              impl->last_frequency_update = std::chrono::steady_clock::now();
              impl->on_timer(boost::system::error_code(),
                             std::chrono::milliseconds(polling_interval));
            });
          });
        });
      });
    } else {
      Impl::alpha_omega->CloseConnection();
    }
  }
}

std::chrono::nanoseconds AlphaOmegaNode::time() const { return impl->time; }

bool AlphaOmegaNode::prepare() {
#ifdef _WIN32
  AlphaOmegaNode::Impl::alpha_omega = new RealAlphaOmega();
  static bool has_run = false;
  if (has_run) {
    return aoapi->loaded;
  }
  has_run = true;
  aoapi = new AlphaOmegaAPI();

  alphaomega_handle =
      LoadLibrary("C:\\Program Files (x86)\\AlphaOmega\\Neuro Omega System "
                  "SDK\\CPP_SDK\\win64\\NeuroOmega_x64.dll");
  THALAMUS_LOG(info) << "LoadLibrary " << alphaomega_handle;
  if (!alphaomega_handle) {
    THALAMUS_LOG(info)
        << "Couldn't find NeuroOmega_x64.dll.  Alpha Omega features disabled";
    return false;
  }
  THALAMUS_LOG(info) << "NeuroOmega_x64.dll found.  Loading Alpha Omega API";

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-function-type"
#pragma clang diagnostic ignored "-Wcast-function-type-strict"
#endif
  aoapi->DefaultStartConnection =
      reinterpret_cast<decltype(&DefaultStartConnection)>(
          ::GetProcAddress(alphaomega_handle, "DefaultStartConnection"));
  if (!aoapi->DefaultStartConnection) {
    THALAMUS_LOG(info) << "Failed to load DefaultStartConnection.  Alpha Omega "
                          "features disabled";
    return false;
  }

  aoapi->isConnected = reinterpret_cast<decltype(&isConnected)>(
      ::GetProcAddress(alphaomega_handle, "isConnected"));
  if (!aoapi->isConnected) {
    THALAMUS_LOG(info)
        << "Failed to load isConnected.  Alpha Omega features disabled";
    return false;
  }

  aoapi->AddBufferChannel = reinterpret_cast<decltype(&AddBufferChannel)>(
      ::GetProcAddress(alphaomega_handle, "AddBufferChannel"));
  if (!aoapi->AddBufferChannel) {
    THALAMUS_LOG(info)
        << "Failed to load AddBufferChannel.  Alpha Omega features disabled";
    return false;
  }

  aoapi->GetAlignedData = reinterpret_cast<decltype(&GetAlignedData)>(
      ::GetProcAddress(alphaomega_handle, "GetAlignedData"));
  if (!aoapi->GetAlignedData) {
    THALAMUS_LOG(info)
        << "Failed to load GetAlignedData.  Alpha Omega features disabled";
    return false;
  }

  aoapi->GetAllChannels = reinterpret_cast<decltype(&GetAllChannels)>(
      ::GetProcAddress(alphaomega_handle, "GetAllChannels"));
  if (!aoapi->GetAllChannels) {
    THALAMUS_LOG(info)
        << "Failed to load GetAllChannels.  Alpha Omega features disabled";
    return false;
  }

  aoapi->GetChannelsCount = reinterpret_cast<decltype(&GetChannelsCount)>(
      ::GetProcAddress(alphaomega_handle, "GetChannelsCount"));
  if (!aoapi->GetChannelsCount) {
    THALAMUS_LOG(info)
        << "Failed to load GetChannelsCount.  Alpha Omega features disabled";
    return false;
  }

  aoapi->ErrorHandlingfunc = reinterpret_cast<decltype(&ErrorHandlingfunc)>(
      ::GetProcAddress(alphaomega_handle, "ErrorHandlingfunc"));
  if (!aoapi->ErrorHandlingfunc) {
    THALAMUS_LOG(info)
        << "Failed to load ErrorHandlingfunc.  Alpha Omega features disabled";
    return false;
  }

  aoapi->CloseConnection = reinterpret_cast<decltype(&CloseConnection)>(
      ::GetProcAddress(alphaomega_handle, "CloseConnection"));
  if (!aoapi->CloseConnection) {
    THALAMUS_LOG(info)
        << "Failed to load CloseConnection.  Alpha Omega features disabled";
    return false;
  }
#ifdef __clang__
#pragma clang diagnostic pop
#endif

  aoapi->loaded = true;
  THALAMUS_LOG(info) << "Alpha Omega API loaded";
  return true;
#else
  AlphaOmegaNode::Impl::alpha_omega = new MockAlphaOmega();
  return true;
#endif
}

boost::json::value AlphaOmegaNode::process(const boost::json::value &request) {
  THALAMUS_LOG(info) << "request received";

  if (request.is_string() && request.get_string() == "load_channels") {
    impl->load_channels();
  }

  return boost::json::value();
}

size_t AlphaOmegaNode::modalities() const {
  return infer_modalities<AlphaOmegaNode>();
}
} // namespace thalamus
