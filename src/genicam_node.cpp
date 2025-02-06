#include <calculator.hpp>
#include <filesystem>
#include <fstream>
#include <genicam_node.hpp>
#include <gentl.h>
#include <modalities_util.hpp>
#include <numeric>
#include <regex>
#include <thalamus/tracing.hpp>
#include <zlib.h>
#ifdef _WIN32
#else
#include <dlfcn.h>
#endif
#include <thalamus/thread.hpp>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include <boost/endian/conversion.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/spirit/include/qi.hpp>

#ifdef __clang__
#pragma clang diagnostic pop
#endif

namespace thalamus {
using namespace std::chrono_literals;
template <class...> constexpr std::false_type always_false{};

template <typename T> struct Caster {
  template <typename M> T operator()(M arg) {
    if constexpr (std::is_convertible<M, T>()) {
      return static_cast<T>(arg);
    } else {
      std::string arg_text = "???";
      std::string m_name = "???";
      if constexpr (std::is_same<M, long long int>()) {
        m_name = "long long int";
        arg_text = std::to_string(arg);
      } else if constexpr (std::is_same<M, std::string>()) {
        m_name = "std::string";
        arg_text = arg;
      } else if constexpr (std::is_same<M, double>()) {
        m_name = "double";
        arg_text = std::to_string(arg);
      } else if constexpr (std::is_same<M, int>()) {
        m_name = "int";
        arg_text = std::to_string(arg);
      } else if constexpr (std::is_same<M, std::monostate>()) {
        m_name = "std::monostate";
        arg_text = "std::monostate";
      }

      std::string t_name = "???";
      if constexpr (std::is_same<T, long long int>()) {
        t_name = "long long int";
      } else if constexpr (std::is_same<T, std::string>()) {
        t_name = "std::string";
      } else if constexpr (std::is_same<T, double>()) {
        t_name = "double";
      } else if constexpr (std::is_same<T, int>()) {
        t_name = "int";
      }

      THALAMUS_ASSERT(false, "Not convertable: %s %s->%s", arg_text, m_name,
                      t_name);
    }
  }
};

template <typename T, typename... VARIANTS>
static T variant_cast(const std::variant<VARIANTS...> &arg) {
  return std::visit(Caster<T>{}, arg);
}

struct GenicamNode::Impl {
  boost::asio::io_context &io_context;
  ObservableDictPtr state;
  boost::signals2::scoped_connection state_connection;
  boost::signals2::scoped_connection options_connection;
  bool is_running = false;
  GenicamNode *outer;
  std::chrono::nanoseconds time;
  std::thread ffmpeg_thread;
  bool running = false;
  thalamus_grpc::Image image;
  std::atomic_bool frame_pending;
  std::vector<unsigned char> intermediate;
  thalamus::vector<Plane> data;
  std::chrono::nanoseconds frame_interval;
  Format format;
  size_t width;
  size_t height;
  AnalogNodeImpl analog_impl;
  bool has_analog = false;
  bool has_image = false;

  struct Cti {
    GenTL::PGCGetInfo GCGetInfo;
    GenTL::PGCGetLastError GCGetLastError;
    GenTL::PGCInitLib GCInitLib;
    GenTL::PGCCloseLib GCCloseLib;
    GenTL::PGCReadPort GCReadPort;
    GenTL::PGCWritePort GCWritePort;
    GenTL::PGCGetPortURL GCGetPortURL;
    GenTL::PGCGetPortInfo GCGetPortInfo;

    GenTL::PGCRegisterEvent GCRegisterEvent;
    GenTL::PGCUnregisterEvent GCUnregisterEvent;
    GenTL::PEventGetData EventGetData;
    GenTL::PEventGetDataInfo EventGetDataInfo;
    GenTL::PEventGetInfo EventGetInfo;
    GenTL::PEventFlush EventFlush;
    GenTL::PEventKill EventKill;
    GenTL::PTLOpen TLOpen;
    GenTL::PTLClose TLClose;
    GenTL::PTLGetInfo TLGetInfo;
    GenTL::PTLGetNumInterfaces TLGetNumInterfaces;
    GenTL::PTLGetInterfaceID TLGetInterfaceID;
    GenTL::PTLGetInterfaceInfo TLGetInterfaceInfo;
    GenTL::PTLOpenInterface TLOpenInterface;
    GenTL::PTLUpdateInterfaceList TLUpdateInterfaceList;
    GenTL::PIFClose IFClose;
    GenTL::PIFGetInfo IFGetInfo;
    GenTL::PIFGetNumDevices IFGetNumDevices;
    GenTL::PIFGetDeviceID IFGetDeviceID;
    GenTL::PIFUpdateDeviceList IFUpdateDeviceList;
    GenTL::PIFGetDeviceInfo IFGetDeviceInfo;
    GenTL::PIFOpenDevice IFOpenDevice;

    GenTL::PDevGetPort DevGetPort;
    GenTL::PDevGetNumDataStreams DevGetNumDataStreams;
    GenTL::PDevGetDataStreamID DevGetDataStreamID;
    GenTL::PDevOpenDataStream DevOpenDataStream;
    GenTL::PDevGetInfo DevGetInfo;
    GenTL::PDevClose DevClose;

    GenTL::PDSAnnounceBuffer DSAnnounceBuffer;
    GenTL::PDSAllocAndAnnounceBuffer DSAllocAndAnnounceBuffer;
    GenTL::PDSFlushQueue DSFlushQueue;
    GenTL::PDSStartAcquisition DSStartAcquisition;
    GenTL::PDSStopAcquisition DSStopAcquisition;
    GenTL::PDSGetInfo DSGetInfo;
    GenTL::PDSGetBufferID DSGetBufferID;
    GenTL::PDSClose DSClose;
    GenTL::PDSRevokeBuffer DSRevokeBuffer;
    GenTL::PDSQueueBuffer DSQueueBuffer;
    GenTL::PDSGetBufferInfo DSGetBufferInfo;

    GenTL::PGCGetNumPortURLs GCGetNumPortURLs;
    GenTL::PGCGetPortURLInfo GCGetPortURLInfo;
    GenTL::PGCReadPortStacked GCReadPortStacked;
    GenTL::PGCWritePortStacked GCWritePortStacked;

    GenTL::PDSGetBufferChunkData DSGetBufferChunkData;

    GenTL::PIFGetParentTL IFGetParentTL;
    GenTL::PDevGetParentIF DevGetParentIF;
    GenTL::PDSGetParentDev DSGetParentDev;

    GenTL::PDSGetNumBufferParts DSGetNumBufferParts;
    GenTL::PDSGetBufferPartInfo DSGetBufferPartInfo;
    std::string name;
    bool loaded = false;
    bool tl_opened = false;
    bool gc_inited = false;

#ifdef _WIN32
    HMODULE library_handle;
    template <typename T> T load_function(const std::string &func_name) {
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-function-type"
#pragma clang diagnostic ignored "-Wcast-function-type-strict"
#endif
      auto result = reinterpret_cast<T>(
          ::GetProcAddress(library_handle, func_name.c_str()));
#ifdef __clang__
#pragma clang diagnostic pop
#endif
      if (!result) {
        THALAMUS_LOG(info) << "Failed to load " << func_name << ".  "
                           << this->name << " disabled";
      }
      return result;
    }
#else
    void *library_handle;
    template <typename T> T load_function(const std::string &func_name) {
      auto result =
          reinterpret_cast<T>(dlsym(library_handle, func_name.c_str()));
      if (!result) {
        THALAMUS_LOG(info) << "Failed to load " << func_name << ".  "
                           << this->name << " disabled";
      }
      return result;
    }
#endif

    using RegValue = std::variant<long long int, std::string, double>;

    static calculator::number reg_to_number(const RegValue &from) {
      if (std::holds_alternative<long long int>(from)) {
        return std::get<long long int>(from);
      } else if (std::holds_alternative<double>(from)) {
        return std::get<double>(from);
      }
      THALAMUS_ASSERT(false, "Invalid Reg type");
    }
    static RegValue number_to_reg(const calculator::number &from) {
      if (std::holds_alternative<long long int>(from)) {
        return std::get<long long int>(from);
      } else if (std::holds_alternative<double>(from)) {
        return std::get<double>(from);
      }
      THALAMUS_ASSERT(false, "Invalid number type");
    }
    struct Device {
      virtual ~Device() {}
      virtual RegValue get(const std::string &reg) = 0;
      virtual void
      set(const std::string &reg,
          const std::variant<long long int, std::string, double> &value) = 0;
      virtual bool is_writable(const std::string &) = 0;
    };

    enum class AccessMode { RW, RO, WO };

    static AccessMode parse_access_mode(const std::string &text) {
      if (text == "RW") {
        return AccessMode::RW;
      } else if (text == "RO") {
        return AccessMode::RO;
      } else if (text == "WO") {
        return AccessMode::WO;
      }
      THALAMUS_ASSERT(false, "Invalid access mode string: %s", text);
    }

    struct IntConverter {
      Device *device;
      GenTL::PORT_HANDLE handle;
      Cti *cti;
      size_t address;
      std::string p_address;
      size_t length;

      std::string p_value;
      std::string to_code;
      std::string from_code;
      std::map<std::string, std::string> values;

      std::vector<unsigned char> buffer;

      std::optional<calculator::program> to_program;   // Our program (AST)
      std::optional<calculator::program> from_program; // Our program (AST)

      long long int read() {
        TRACE_EVENT("thalamus", "IntConverter::read");
        if (!from_program) {
          auto iter = from_code.cbegin();
          boost::spirit::ascii::space_type space;
          TRACE_EVENT("thalamus", "boost::spirit::qi::phrase_parse");
          auto success = phrase_parse(iter, from_code.cend(), *parser, space,
                                      from_program);
          THALAMUS_ASSERT(success, "Failed to parse expression: %s", from_code);
        }

        std::map<std::string, calculator::number> substitutions;
        for (auto &i : values) {
          auto val = device->get(i.second);
          substitutions[i.first] = reg_to_number(val);
        }
        auto to_val = device->get(p_value);
        substitutions["TO"] = reg_to_number(to_val);

        calculator::eval eval{substitutions};
        TRACE_EVENT("thalamus", "calculator::eval");
        auto result = eval(*from_program);
        return variant_cast<long long int>(result);
      }

      void write(long long int from) {
        TRACE_EVENT("thalamus", "IntConverter::write");
        if (!to_program) {
          auto iter = to_code.cbegin();
          boost::spirit::ascii::space_type space;
          TRACE_EVENT("thalamus", "boost::spirit::qi::phrase_parse");
          auto success =
              phrase_parse(iter, to_code.cend(), *parser, space, to_program);
          THALAMUS_ASSERT(success, "Failed to parse expression: %s", to_code);
        }

        std::map<std::string, calculator::number> substitutions;
        for (auto &i : values) {
          auto val = device->get(i.second);
          substitutions[i.first] = reg_to_number(val);
        }
        substitutions["FROM"] = from;

        calculator::eval eval{substitutions};
        TRACE_EVENT("thalamus", "calculator::eval");
        auto result = eval(*to_program);
        device->set(this->p_value, number_to_reg(result));
      }

      bool is_writable() { return device->is_writable(this->p_value); }
    };

    struct Converter {
      Device *device;
      GenTL::PORT_HANDLE handle;
      Cti *cti;
      size_t address;
      std::string p_address;
      size_t length;

      std::string p_value;
      std::string to_code;
      std::string from_code;
      std::map<std::string, std::string> values;

      std::vector<unsigned char> buffer;

      std::optional<calculator::program> to_program;   // Our program (AST)
      std::optional<calculator::program> from_program; // Our program (AST)

      double read() {
        TRACE_EVENT("thalamus", "Converter::read");
        if (!from_program) {
          auto iter = from_code.cbegin();
          boost::spirit::ascii::space_type space;
          TRACE_EVENT("thalamus", "boost::spirit::qi::phrase_parse");
          auto success = phrase_parse(iter, from_code.cend(), *parser, space,
                                      from_program);
          THALAMUS_ASSERT(success, "Failed to parse expression: %s", from_code);
        }

        std::map<std::string, calculator::number> substitutions;
        for (auto &i : values) {
          auto val = device->get(i.second);
          substitutions[i.first] = reg_to_number(val);
        }
        auto to_val = device->get(p_value);
        substitutions["TO"] = reg_to_number(to_val);

        calculator::eval eval{substitutions};
        TRACE_EVENT("thalamus", "calculator::eval");
        auto result = eval(*from_program);
        return variant_cast<double>(result);
      }

      void write(double from) {
        TRACE_EVENT("thalamus", "Converter::write");
        if (!to_program) {
          auto iter = to_code.cbegin();
          boost::spirit::ascii::space_type space;
          TRACE_EVENT("thalamus", "boost::spirit::qi::phrase_parse");
          auto success =
              phrase_parse(iter, to_code.cend(), *parser, space, to_program);
          THALAMUS_ASSERT(success, "Failed to parse expression: %s", to_code);
        }

        std::map<std::string, calculator::number> substitutions;
        for (auto &i : values) {
          auto val = device->get(i.second);
          substitutions[i.first] = reg_to_number(val);
        }
        substitutions["FROM"] = from;

        calculator::eval eval{substitutions};
        TRACE_EVENT("thalamus", "calculator::eval");
        auto result = eval(*to_program);
        device->set(this->p_value, number_to_reg(result));
      }

      bool is_writable() { return device->is_writable(this->p_value); }
    };

    struct IntSwissKnife {
      Device *device;
      GenTL::PORT_HANDLE handle;
      Cti *cti;
      size_t address;
      std::string p_address;
      size_t length;

      std::string code;
      std::map<std::string, std::string> values;

      std::vector<unsigned char> buffer;

      std::optional<calculator::program> program; // Our program (AST)

      long long int read() {
        TRACE_EVENT("thalamus", "IntSwissKnife::read");
        if (!program) {
          auto iter = code.cbegin();
          boost::spirit::ascii::space_type space;
          TRACE_EVENT("thalamus", "boost::spirit::qi::phrase_parse");
          auto success =
              phrase_parse(iter, code.cend(), *parser, space, program);
          THALAMUS_ASSERT(success, "Failed to parse expression: %s", code);
        }

        std::map<std::string, calculator::number> substitutions;
        for (auto &i : values) {
          auto val = device->get(i.second);
          substitutions[i.first] = reg_to_number(val);
        }

        calculator::eval eval{substitutions};
        TRACE_EVENT("thalamus", "calculator::eval");
        auto result = eval(*program);
        return variant_cast<long long int>(result);
      }
    };

    struct SwissKnife {
      Device *device;
      GenTL::PORT_HANDLE handle;
      Cti *cti;
      size_t address;
      std::string p_address;
      size_t length;

      std::string code;
      std::map<std::string, std::string> values;

      std::vector<unsigned char> buffer;

      std::optional<calculator::program> program; // Our program (AST)

      double read() {
        TRACE_EVENT("thalamus", "SwissKnife::read");
        if (!program) {
          auto iter = code.cbegin();
          boost::spirit::ascii::space_type space;
          TRACE_EVENT("thalamus", "boost::spirit::qi::phrase_parse");
          auto success =
              phrase_parse(iter, code.cend(), *parser, space, program);
          THALAMUS_ASSERT(success, "Failed to parse expression: %s", code);
        }

        std::map<std::string, calculator::number> substitutions;
        for (auto &i : values) {
          auto val = device->get(i.second);
          substitutions[i.first] = reg_to_number(val);
        }

        calculator::eval eval{substitutions};
        TRACE_EVENT("thalamus", "calculator::eval");
        auto result = eval(*program);
        return variant_cast<double>(result);
      }
    };

    struct StringReg {
      Device *device;
      GenTL::PORT_HANDLE handle;
      Cti *cti;
      size_t address;
      std::string p_address;
      std::string int_swiss_knife;
      size_t length;
      AccessMode access_mode;
      std::string buffer;
      std::string read() {
        TRACE_EVENT("thalamus", "StringRed::read");
        buffer.resize(length);

        auto total_address = int64_t(address);
        if (!p_address.empty()) {
          total_address += std::get<long long int>(device->get(p_address));
        }
        if (!int_swiss_knife.empty()) {
          total_address +=
              std::get<long long int>(device->get(int_swiss_knife));
        }

        auto error = cti->GCReadPort(handle, size_t(total_address),
                                     buffer.data(), &length);
        THALAMUS_ASSERT(error == GenTL::GC_ERR_SUCCESS, "GCReadPort failed: %d",
                        error);
        buffer = buffer.data();
        return buffer;
      }

      bool is_writable() { return access_mode != AccessMode::RO; }
    };

    struct IntReg {
      Device *device;
      GenTL::PORT_HANDLE handle;
      Cti *cti;
      size_t address;
      std::string p_address;
      std::string int_swiss_knife;
      size_t length;
      bool little_endian;
      bool _unsigned;
      AccessMode access_mode;
      std::optional<size_t> lsb;
      std::optional<size_t> msb;
      std::vector<unsigned char> buffer;
      long long int read() {
        TRACE_EVENT("thalamus", "IntReg::read");
        buffer.resize(length);

        auto total_address = int64_t(address);
        if (!p_address.empty()) {
          total_address += std::get<long long int>(device->get(p_address));
        }
        if (!int_swiss_knife.empty()) {
          total_address +=
              std::get<long long int>(device->get(int_swiss_knife));
        }

        auto error = cti->GCReadPort(handle, size_t(total_address),
                                     buffer.data(), &length);
        if (error == GenTL::GC_ERR_NO_DATA) {
          return 0;
        }
        THALAMUS_ASSERT(error == GenTL::GC_ERR_SUCCESS, "GCReadPort failed: %d",
                        error);

        auto visitor = [&](auto a, auto b) {
          a <<= 8;
          a |= b;
          return a;
        };

        auto first_byte = little_endian ? buffer.back() : buffer.front();
        auto result =
            little_endian
                ? std::accumulate(
                      buffer.rbegin(), buffer.rend(),
                      _unsigned || !(first_byte & 0x80) ? 0ll : -1ll, visitor)
                : std::accumulate(
                      buffer.begin(), buffer.end(),
                      _unsigned || !(first_byte & 0x80) ? 0ll : -1ll, visitor);

        if (msb) {
          long long int mask = (1ll << (*msb + 1)) - 1;
          result &= mask;
        }
        if (lsb) {
          result >>= *lsb;
        }

        return result;
      }

      void write(long long int value) {
        TRACE_EVENT("thalamus", "IntReg::write");
        std::vector<unsigned char> mask;
        for (auto i = 0ull; i < length; ++i) {
          unsigned char mask_byte = 255;
          if (lsb) {
            if (*lsb > i * 8) {
              mask_byte <<= *lsb - i * 8;
            }
          }
          if (msb) {
            if (*msb < (i + 1) * 8) {
              unsigned char temp = 255;
              temp >>= (i + 1) * 8 - (*msb + 1);
              mask_byte &= temp;
            }
          }
          mask.push_back(mask_byte);
        }

        if (lsb) {
          value <<= *lsb;
        }

        std::vector<unsigned char> value_buffer;
        value_buffer.resize(length);
        for (auto i = 0ull; i < length; ++i) {
          value_buffer.at(i) = value & 0x00FF;
          value >>= 8;
        }

        auto total_address = int64_t(address);
        if (!p_address.empty()) {
          total_address += std::get<long long int>(device->get(p_address));
        }

        buffer.resize(length);
        if (access_mode == AccessMode::WO) {
          buffer.assign(length, 0);
        } else {
          auto error = cti->GCReadPort(handle, size_t(total_address),
                                       buffer.data(), &length);
          if (error == GenTL::GC_ERR_NO_DATA) {
            buffer.assign(length, 0);
            return;
          }
          THALAMUS_ASSERT(error == GenTL::GC_ERR_SUCCESS,
                          "GCReadPort failed: %d", error);
        }

        if (!little_endian) {
          std::reverse(buffer.begin(), buffer.end());
        }

        for (auto i = 0ull; i < length; ++i) {
          auto current = buffer.at(i);
          auto mask_byte = mask.at(i);
          auto new_byte = value_buffer.at(i);
          buffer.at(i) = (current & ~mask_byte) | (new_byte & mask_byte);
        }

        if (!little_endian) {
          std::reverse(buffer.begin(), buffer.end());
        }

        auto error = cti->GCWritePort(handle, size_t(total_address),
                                      buffer.data(), &length);
        THALAMUS_ASSERT(error == GenTL::GC_ERR_SUCCESS,
                        "GCWritePort failed: %d", error);
      }

      bool is_writable() { return access_mode != AccessMode::RO; }
    };

    struct FloatReg {
      Device *device;
      GenTL::PORT_HANDLE handle;
      Cti *cti;
      size_t address;
      std::string p_address;
      std::string int_swiss_knife;
      size_t length;
      bool little_endian;
      bool _unsigned;
      AccessMode access_mode;
      std::vector<unsigned char> buffer;
      double read() {
        TRACE_EVENT("thalamus", "FloatReg::read");
        buffer.resize(length);

        auto total_address = int64_t(address);
        if (!p_address.empty()) {
          total_address += std::get<long long int>(device->get(p_address));
        }
        if (!int_swiss_knife.empty()) {
          total_address +=
              std::get<long long int>(device->get(int_swiss_knife));
        }

        auto error = cti->GCReadPort(handle, size_t(total_address),
                                     buffer.data(), &length);
        if (error == GenTL::GC_ERR_NO_DATA) {
          return 0;
        }
        THALAMUS_ASSERT(error == GenTL::GC_ERR_SUCCESS, "GCReadPort failed: %d",
                        error);

        double result;
        if (length == 4) {
          if (little_endian) {
            boost::endian::little_to_native_inplace(
                *reinterpret_cast<int *>(buffer.data()));
          } else {
            boost::endian::big_to_native_inplace(
                *reinterpret_cast<int *>(buffer.data()));
          }

          result = double(*reinterpret_cast<float *>(buffer.data()));
        } else if (length == 8) {
          if (little_endian) {
            boost::endian::little_to_native_inplace(
                *reinterpret_cast<long long int *>(buffer.data()));
          } else {
            boost::endian::big_to_native_inplace(
                *reinterpret_cast<long long int *>(buffer.data()));
          }

          result = *reinterpret_cast<double *>(buffer.data());
        } else {
          THALAMUS_ASSERT(false, "Unsupported FloatReg length: %d", length);
        }
        return result;
      }

      void write(double new_value) {
        TRACE_EVENT("thalamus", "FloatReg::write");
        auto total_address = int64_t(address);
        if (!p_address.empty()) {
          total_address += std::get<long long int>(device->get(p_address));
        }

        GenTL::GC_ERROR error;
        size_t length2 = length;
        buffer.resize(length);
        if (length == 4) {
          auto temp_float = float(new_value);
          auto temp_char = reinterpret_cast<unsigned char *>(&temp_float);
          auto temp_int = *reinterpret_cast<long long *>(temp_char);

          if (little_endian) {
            boost::endian::native_to_little_inplace(temp_int);
          } else {
            boost::endian::native_to_big_inplace(temp_int);
          }

          error = cti->GCWritePort(handle, size_t(total_address),
                                   reinterpret_cast<unsigned char *>(&temp_int),
                                   &length2);
        } else if (length == 8) {
          auto temp_char = reinterpret_cast<unsigned char *>(&new_value);
          auto temp_int = *reinterpret_cast<long long *>(temp_char);

          if (little_endian) {
            boost::endian::native_to_little_inplace(temp_int);
          } else {
            boost::endian::native_to_big_inplace(temp_int);
          }

          error = cti->GCWritePort(handle, size_t(total_address),
                                   reinterpret_cast<unsigned char *>(&temp_int),
                                   &length2);
        } else {
          THALAMUS_ASSERT(false, "Unsupported FloatReg length: %d", length);
        }
        THALAMUS_ASSERT(error == GenTL::GC_ERR_SUCCESS,
                        "GCWritePort failed: %d", error);
      }

      bool is_writable() { return access_mode != AccessMode::RO; }
    };

    struct Link {
      std::string name;
    };

    template <typename T = long long int>
    static T get_int(const boost::property_tree::ptree &tree,
                     const std::string &path, T default_value) {
      auto text = tree.get_optional<std::string>(path);
      if (!text) {
        return default_value;
      }
      T result;
      if (text->starts_with("0x")) {
        auto success = absl::SimpleHexAtoi(*text, &result);
        THALAMUS_ASSERT(success, "Failed to parse %s", *text);
      } else {
        auto success = absl::SimpleAtoi(*text, &result);
        THALAMUS_ASSERT(success, "Failed to parse %s", *text);
      }
      return result;
    }

    template <typename T>
    static std::optional<T>
    get_optional(const boost::property_tree::ptree &tree,
                 const std::string &path) {
      auto text = tree.get_optional<std::string>(path);
      if (!text) {
        return std::nullopt;
      }
      if constexpr (std::is_integral<T>()) {
        long long int result;
        if (text->starts_with("0x")) {
          auto success = absl::SimpleHexAtoi(*text, &result);
          THALAMUS_ASSERT(success, "Failed to parse %s", *text);
        } else {
          auto success = absl::SimpleAtoi(*text, &result);
          THALAMUS_ASSERT(success, "Failed to parse %s", *text);
        }
        return result;
      } else {
        double result;
        auto success = absl::SimpleAtod(*text, &result);
        THALAMUS_ASSERT(success, "Failed to parse %s", *text);
        return result;
      }
    }

    struct Float {
      Device *device;
      std::string value;
      std::optional<std::string> min;
      std::optional<std::string> max;
      std::optional<std::string> inc;

      double read() {
        TRACE_EVENT("thalamus", "Float::read");
        auto result = variant_cast<double>(device->get(this->value));
        return result;
      }

      void write(double new_value) {
        TRACE_EVENT("thalamus", "Float::write");
        std::optional<double> min_val =
            this->min ? std::optional<double>(
                            variant_cast<double>(device->get(*this->min)))
                      : std::nullopt;
        std::optional<double> max_val =
            this->max ? std::optional<double>(
                            variant_cast<double>(device->get(*this->max)))
                      : std::nullopt;
        std::optional<double> inc_val =
            this->inc ? std::optional<double>(
                            variant_cast<double>(device->get(*this->inc)))
                      : std::nullopt;

        if (min_val) {
          new_value = std::max(*min_val, new_value);
        }
        if (max_val) {
          new_value = std::min(*max_val, new_value);
        }

        if (inc_val) {
          auto clamped = new_value;
          if (min_val) {
            clamped -= *min_val;
          }
          clamped = (clamped / (*inc_val)) * (*inc_val);
          if (min_val) {
            clamped += *min_val;
          }
          new_value = clamped;
        }

        device->set(this->value, new_value);
      }

      bool is_writable() { return device->is_writable(this->value); }
    };

    struct Command {
      Device *device;
      std::string value;
      std::variant<std::string, long long int> command_value;

      void execute() {
        TRACE_EVENT("thalamus", "Command::execute");
        long long int output;
        if (std::holds_alternative<std::string>(command_value)) {
          auto command_value_str = std::get<std::string>(command_value);
          output = variant_cast<long long int>(device->get(command_value_str));
        } else {
          output = std::get<long long int>(command_value);
        }

        device->set(value, output);
      }
    };

    struct Enumeration {
      Device *device;
      std::string value;
      std::map<std::string, long long int> enums;
      std::map<long long int, std::string> reverse_enums;

      std::string read() {
        TRACE_EVENT("thalamus", "Enumeration::read");
        if (reverse_enums.empty()) {
          for (auto &i : enums) {
            reverse_enums[i.second] = i.first;
          }
        }
        auto key = variant_cast<long long int>(device->get(this->value));
        auto result = reverse_enums.at(key);
        return result;
      }

      void write(std::string key) {
        TRACE_EVENT("thalamus", "Enumeration::write");
        auto translated = enums.at(key);
        device->set(this->value, translated);
      }

      bool is_writable() { return device->is_writable(this->value); }
    };

    struct Integer {
      Device *device;
      std::string value;
      std::optional<std::string> min;
      std::optional<std::string> max;
      std::optional<std::string> inc;

      long long int read() {
        TRACE_EVENT("thalamus", "Integer::read");
        auto result = variant_cast<long long int>(device->get(this->value));
        return result;
      }

      void write(long long int new_value) {
        TRACE_EVENT("thalamus", "Integer::write");
        std::optional<long long int> min_val =
            this->min
                ? std::optional<long long int>(
                      variant_cast<long long int>(device->get(*this->min)))
                : std::nullopt;
        std::optional<long long int> max_val =
            this->max
                ? std::optional<long long int>(
                      variant_cast<long long int>(device->get(*this->max)))
                : std::nullopt;
        std::optional<long long int> inc_val =
            this->inc
                ? std::optional<long long int>(
                      variant_cast<long long int>(device->get(*this->inc)))
                : std::nullopt;

        if (min_val) {
          new_value = std::max(*min_val, new_value);
        }
        if (max_val) {
          new_value = std::min(*max_val, new_value);
        }

        if (inc_val) {
          auto clamped = new_value;
          if (min_val) {
            clamped -= *min_val;
          }
          clamped = (clamped / (*inc_val)) * (*inc_val);
          if (min_val) {
            clamped += *min_val;
          }
          new_value = clamped;
        }

        device->set(this->value, new_value);
      }

      bool is_writable() { return device->is_writable(this->value); }
    };

    struct DeviceImpl : public Device {
      std::string id;
      Cti *cti;
      GenTL::DEV_HANDLE dev_handle;
      GenTL::PORT_HANDLE port_handle = nullptr;
      GenTL::DS_HANDLE ds_handle = nullptr;
      GenTL::EVENT_HANDLE event_handle = nullptr;

      bool ready = false;
      boost::property_tree::ptree tree;

      using Value = std::variant<long long int, double, std::string, Link,
                                 StringReg, IntConverter, IntReg, IntSwissKnife,
                                 FloatReg, SwissKnife, Converter, Float,
                                 Integer, Enumeration, Command>;
      std::map<std::string, Value> nodes;

      ~DeviceImpl() override {
        stop_stream();
        if (dev_handle) {
          cti->DevClose(dev_handle);
        }
        if (ds_handle) {
          cti->DSClose(ds_handle);
        }
        if (event_handle) {
          cti->GCUnregisterEvent(ds_handle, GenTL::EVENT_NEW_BUFFER);
        }
      }

      bool load_xml() {
        TRACE_EVENT("thalamus", "DeviceImpl::load_xml");
        uint32_t num_urls;
        auto error = cti->GCGetNumPortURLs(port_handle, &num_urls);
        if (error != GenTL::GC_ERR_SUCCESS) {
          THALAMUS_LOG(info) << "GCGetNumPortURLs failed.";
          return false;
        }

        for (auto l = 0u; l < num_urls; ++l) {
          size_t id_size;
          GenTL::INFO_DATATYPE type;
          error = cti->GCGetPortURLInfo(port_handle, l, GenTL::URL_INFO_URL,
                                        &type, nullptr, &id_size);
          if (error != GenTL::GC_ERR_SUCCESS) {
            THALAMUS_LOG(info) << "GCGetPortURLInfo failed.";
            return false;
          }
          std::string port_url;
          port_url.resize(id_size);
          error = cti->GCGetPortURLInfo(port_handle, l, GenTL::URL_INFO_URL,
                                        &type, port_url.data(), &id_size);
          if (error != GenTL::GC_ERR_SUCCESS) {
            THALAMUS_LOG(info) << "GCGetPortURLInfo failed.";
            return false;
          }
          port_url.resize(port_url.size() - 1);

          std::vector<std::string> tokens =
              absl::StrSplit(port_url, absl::ByAnyChar(":;?"));
          if (tokens.empty() || tokens[0] != "local") {
            THALAMUS_LOG(info) << "Unsupported port URL " << port_url << ".";
            return false;
          }

          size_t address, length;
          auto success = absl::SimpleHexAtoi(tokens[2], &address);
          if (!success) {
            THALAMUS_LOG(info) << "Failed to parse URL address.";
            return false;
          }
          success = absl::SimpleHexAtoi(tokens[3], &length);
          if (!success) {
            THALAMUS_LOG(info) << "Failed to parse URL length.";
            return false;
          }

          std::vector<unsigned char> zip_data(length, 0);
          error =
              cti->GCReadPort(port_handle, address, zip_data.data(), &length);
          if (error != GenTL::GC_ERR_SUCCESS) {
            THALAMUS_LOG(info) << "GCReadPort failed.";
            return false;
          }

          std::string xml;
          size_t offset = 0;
          auto buffer = zip_data.data();
          auto central_header_found = false;
          while (offset < length) {
            auto signature = boost::endian::little_to_native(
                *reinterpret_cast<unsigned int *>(buffer + offset));
            if (signature == 0x04034b50) {
              THALAMUS_LOG(info) << "local";
              offset += 4;
              // auto min_version =
              // boost::endian::little_to_native(*reinterpret_cast<short*>(buffer
              // + offset));
              offset += 2;
              // auto bit_flag =
              // boost::endian::little_to_native(*reinterpret_cast<short*>(buffer
              // + offset));
              offset += 2;
              auto compression = boost::endian::little_to_native(
                  *reinterpret_cast<short *>(buffer + offset));
              offset += 2;
              // auto modification_time =
              // boost::endian::little_to_native(*reinterpret_cast<short*>(buffer
              // + offset));
              offset += 2;
              // auto modification_date =
              // boost::endian::little_to_native(*reinterpret_cast<short*>(buffer
              // + offset));
              offset += 2;
              // auto crc =
              // boost::endian::little_to_native(*reinterpret_cast<unsigned
              // int*>(buffer + offset));
              offset += 4;
              auto compressed_size = boost::endian::little_to_native(
                  *reinterpret_cast<unsigned int *>(buffer + offset));
              offset += 4;
              auto uncompressed_size = boost::endian::little_to_native(
                  *reinterpret_cast<unsigned int *>(buffer + offset));
              offset += 4;
              auto filename_length = boost::endian::little_to_native(
                  *reinterpret_cast<unsigned short *>(buffer + offset));
              offset += 2;
              auto extra_length = boost::endian::little_to_native(
                  *reinterpret_cast<unsigned short *>(buffer + offset));
              offset += 2;
              auto filename = std::string(
                  reinterpret_cast<char *>(buffer + offset), filename_length);
              offset += filename_length;
              auto extra = std::vector<unsigned char>(
                  buffer + offset, buffer + offset + extra_length);
              offset += extra_length;
              if (!central_header_found) {
                offset += compressed_size;
                continue;
              }

              std::cout << "ZIP file = " << filename
                        << " Compression = " << compression << std::endl;
              if (compression != 8) {
                THALAMUS_LOG(info)
                    << "Unsupported compression: " << compression;
                return false;
              }

              xml.assign(uncompressed_size, ' ');
              z_stream strm;
              strm.zalloc = nullptr;
              strm.zfree = nullptr;
              strm.opaque = nullptr;
              strm.avail_in = 0;
              strm.next_in = nullptr;
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
#endif
              auto ret = inflateInit2(&strm, -MAX_WBITS);
#ifdef __clang__
#pragma clang diagnostic pop
#endif
              if (ret != Z_OK) {
                THALAMUS_LOG(info) << "Failed to initialize inflate.";
                return false;
              }
              strm.avail_in = compressed_size;
              strm.avail_out = uncompressed_size;
              strm.next_in = buffer + offset;
              strm.next_out = reinterpret_cast<unsigned char *>(xml.data());
              ret = inflate(&strm, Z_NO_FLUSH);
              if (ret == Z_STREAM_ERROR) {
                THALAMUS_LOG(info) << "Z_STREAM_ERROR.";
                return false;
              } else if (ret == Z_NEED_DICT) {
                THALAMUS_LOG(info) << "Z_NEED_DICT.";
                return false;
              } else if (ret == Z_DATA_ERROR) {
                THALAMUS_LOG(info) << "Z_DATA_ERROR.";
                return false;
              } else if (ret == Z_MEM_ERROR) {
                THALAMUS_LOG(info) << "Z_MEM_ERROR.";
                return false;
              }
              offset += compressed_size;
              {
                std::ofstream output(cti->name + id + "-stream.xml");
                output.write(xml.data(), int64_t(xml.size()));
              }
              try {
                std::stringstream xml_stream(xml);
                boost::property_tree::xml_parser::read_xml(xml_stream, tree);
              } catch (std::exception &e) {
                THALAMUS_ASSERT(false, "Failed to read xml: %s", e.what());
              }
              return true;
            } else if (signature == 0x02014b50) {
              THALAMUS_LOG(info) << "central";
              offset += 4;
              // auto made_version =
              // boost::endian::little_to_native(*reinterpret_cast<short*>(buffer
              // + offset));
              offset += 2;
              // auto min_version =
              // boost::endian::little_to_native(*reinterpret_cast<short*>(buffer
              // + offset));
              offset += 2;
              // auto bit_flag =
              // boost::endian::little_to_native(*reinterpret_cast<short*>(buffer
              // + offset));
              offset += 2;
              // auto compression =
              // boost::endian::little_to_native(*reinterpret_cast<short*>(buffer
              // + offset));
              offset += 2;
              // auto modification_time =
              // boost::endian::little_to_native(*reinterpret_cast<short*>(buffer
              // + offset));
              offset += 2;
              // auto modification_date =
              // boost::endian::little_to_native(*reinterpret_cast<short*>(buffer
              // + offset));
              offset += 2;
              // auto crc =
              // boost::endian::little_to_native(*reinterpret_cast<unsigned
              // int*>(buffer + offset));
              offset += 4;
              // auto compressed_size =
              // boost::endian::little_to_native(*reinterpret_cast<unsigned
              // int*>(buffer + offset));
              offset += 4;
              // auto uncompressed_size =
              // boost::endian::little_to_native(*reinterpret_cast<unsigned
              // int*>(buffer + offset));
              offset += 4;
              auto filename_length = boost::endian::little_to_native(
                  *reinterpret_cast<unsigned short *>(buffer + offset));
              offset += 2;
              auto extra_length = boost::endian::little_to_native(
                  *reinterpret_cast<unsigned short *>(buffer + offset));
              offset += 2;
              auto comment_length = boost::endian::little_to_native(
                  *reinterpret_cast<unsigned short *>(buffer + offset));
              offset += 2;
              // auto start_disk =
              // boost::endian::little_to_native(*reinterpret_cast<unsigned
              // short*>(buffer + offset));
              offset += 2;
              // auto internal_attr =
              // boost::endian::little_to_native(*reinterpret_cast<unsigned
              // short*>(buffer + offset));
              offset += 2;
              // auto external_attr =
              // boost::endian::little_to_native(*reinterpret_cast<unsigned
              // int*>(buffer + offset));
              offset += 4;
              auto file_offset = boost::endian::little_to_native(
                  *reinterpret_cast<unsigned int *>(buffer + offset));
              offset += 4;
              auto filename = std::string(
                  reinterpret_cast<char *>(buffer + offset), filename_length);
              offset += filename_length;
              auto extra = std::vector<unsigned char>(
                  buffer + offset, buffer + offset + extra_length);
              offset += extra_length;
              auto comment = std::string(
                  reinterpret_cast<char *>(buffer + offset), comment_length);
              offset += comment_length;
              central_header_found = true;
              offset = file_offset;
            } else if (signature == 0x06054b50) {
              THALAMUS_LOG(info) << "end";
            }
          }
        }
        return false;
      }

      bool setup_registry() {
        TRACE_EVENT("thalamus", "DeviceImpl::setup_registry");
        std::vector<std::pair<std::string, const boost::property_tree::ptree *>>
            open;
        open.emplace_back("", &tree);
        while (!open.empty()) {
          auto [current_name, current] = open.back();
          open.pop_back();
          try {
            auto node_name = current->get<std::string>("<xmlattr>.Name", "");

            if (current_name == "Integer") {
              auto p_value = current->get_optional<std::string>("pValue");
              auto value = get_optional<long long int>(*current, "Value");
              auto min = current->get_optional<std::string>("pMin");
              auto max = current->get_optional<std::string>("pMax");
              auto inc = current->get_optional<std::string>("pInc");

              if (value) {
                nodes[node_name] = *value;
              } else if (p_value) {
                nodes[node_name] = Integer{
                    this, *p_value,
                    min ? std::optional<std::string>(*min) : std::nullopt,
                    max ? std::optional<std::string>(*max) : std::nullopt,
                    inc ? std::optional<std::string>(*inc) : std::nullopt};
              }
            } else if (current_name == "Command") {
              auto p_value = current->get_optional<std::string>("pValue");
              auto command_value = get_int(*current, "CommandValue", 0ll);
              auto p_command_value =
                  current->get_optional<std::string>("pCommandValue");

              if (p_command_value) {
                nodes[node_name] = Command{this, *p_value, *p_command_value};
              } else {
                nodes[node_name] = Command{this, *p_value, command_value};
              }
            } else if (current_name == "StringReg") {
              size_t address = get_int<size_t>(*current, "Address", 0);
              auto p_address = current->get<std::string>("pAddress", "");
              auto access_mode = parse_access_mode(
                  current->get<std::string>("AccessMode", "RW"));

              auto int_swiss_knife_node =
                  current->get_child_optional("IntSwissKnife");
              std::string int_swiss_knife = "";
              if (int_swiss_knife_node.has_value()) {
                open.emplace_back("IntSwissKnife",
                                  &int_swiss_knife_node.value());
                int_swiss_knife =
                    int_swiss_knife_node->get<std::string>("<xmlattr>.Name");
              }

              auto length = get_int<size_t>(*current, "Length", 0);
              nodes[node_name] = StringReg{
                  this,   port_handle, cti, address, p_address, int_swiss_knife,
                  length, access_mode, ""};
            } else if (current_name == "IntReg") {
              size_t address = get_int<size_t>(*current, "Address", 0);
              auto p_address = current->get<std::string>("pAddress", "");
              auto access_mode = parse_access_mode(
                  current->get<std::string>("AccessMode", "RW"));

              auto int_swiss_knife_node =
                  current->get_child_optional("IntSwissKnife");
              std::string int_swiss_knife = "";
              if (int_swiss_knife_node.has_value()) {
                open.emplace_back("IntSwissKnife",
                                  &int_swiss_knife_node.value());
                int_swiss_knife =
                    int_swiss_knife_node->get<std::string>("<xmlattr>.Name");
              }

              auto length = get_int<size_t>(*current, "Length", 0);
              auto little_endian =
                  current->get<std::string>("Endianess", "LittleEndian") ==
                  "LittleEndian";
              auto _unsigned =
                  current->get<std::string>("Sign", "Unsigned") == "Unsigned";
              nodes[node_name] = IntReg{
                  this,      port_handle,     cti,          address,
                  p_address, int_swiss_knife, length,       little_endian,
                  _unsigned, access_mode,     std::nullopt, std::nullopt,
                  {}};
            } else if (current_name == "FloatReg") {
              size_t address = get_int<size_t>(*current, "Address", 0);
              auto p_address = current->get<std::string>("pAddress", "");
              auto access_mode = parse_access_mode(
                  current->get<std::string>("AccessMode", "RW"));

              auto int_swiss_knife_node =
                  current->get_child_optional("IntSwissKnife");
              std::string int_swiss_knife = "";
              if (int_swiss_knife_node.has_value()) {
                open.emplace_back("IntSwissKnife",
                                  &int_swiss_knife_node.value());
                int_swiss_knife =
                    int_swiss_knife_node->get<std::string>("<xmlattr>.Name");
              }

              auto length = get_int<size_t>(*current, "Length", 0);
              auto little_endian =
                  current->get<std::string>("Endianess", "LittleEndian") ==
                  "LittleEndian";
              auto _unsigned =
                  current->get<std::string>("Sign", "Unsigned") == "Unsigned";
              nodes[node_name] =
                  FloatReg{this,      port_handle,     cti,    address,
                           p_address, int_swiss_knife, length, little_endian,
                           _unsigned, access_mode,     {}};
            } else if (current_name == "Float") {
              auto p_value = current->get_optional<std::string>("pValue");
              auto value = get_optional<double>(*current, "Value");
              auto min = current->get_optional<std::string>("pMin");
              auto max = current->get_optional<std::string>("pMax");
              auto inc = current->get_optional<std::string>("pInc");

              if (value) {
                nodes[node_name] = *value;
              } else if (p_value) {
                nodes[node_name] = Float{
                    this, *p_value,
                    min ? std::optional<std::string>(*min) : std::nullopt,
                    max ? std::optional<std::string>(*max) : std::nullopt,
                    inc ? std::optional<std::string>(*inc) : std::nullopt};
              }
            } else if (current_name == "IntConverter") {
              auto formula_from = current->get<std::string>("FormulaFrom");
              auto formula_to = current->get<std::string>("FormulaTo");
              auto p_value = current->get<std::string>("pValue");

              std::map<std::string, std::string> values;
              for (auto &pair : *current) {
                if (pair.first == "pVariable") {
                  auto var_name =
                      pair.second.get_optional<std::string>("<xmlattr>.Name");
                  THALAMUS_ASSERT(var_name, "Name missing");
                  values[*var_name] = pair.second.data();
                } else if (pair.first == "Expression") {
                  auto var_name =
                      pair.second.get_optional<std::string>("<xmlattr>.Name");
                  std::regex label("\\b" + *var_name + "\\b",
                                   std::regex_constants::icase);
                  formula_from = std::regex_replace(
                      formula_from, label, "(" + pair.second.data() + ")");
                  formula_to = std::regex_replace(
                      formula_to, label, "(" + pair.second.data() + ")");
                }
              }

              nodes[node_name] =
                  IntConverter{this,         port_handle, cti,     0,
                               "",           0,           p_value, formula_to,
                               formula_from, values,      {},      std::nullopt,
                               std::nullopt};
            } else if (current_name == "Converter") {
              auto formula_from = current->get<std::string>("FormulaFrom");
              auto formula_to = current->get<std::string>("FormulaTo");
              auto p_value = current->get<std::string>("pValue");

              std::map<std::string, std::string> values;
              for (auto &pair : *current) {
                if (pair.first == "pVariable") {
                  auto var_name =
                      pair.second.get_optional<std::string>("<xmlattr>.Name");
                  THALAMUS_ASSERT(var_name, "Name missing");
                  values[*var_name] = pair.second.data();
                } else if (pair.first == "Expression") {
                  auto var_name =
                      pair.second.get_optional<std::string>("<xmlattr>.Name");
                  std::regex label("\\b" + *var_name + "\\b",
                                   std::regex_constants::icase);
                  formula_from = std::regex_replace(
                      formula_from, label, "(" + pair.second.data() + ")");
                  formula_to = std::regex_replace(
                      formula_to, label, "(" + pair.second.data() + ")");
                }
              }

              nodes[node_name] =
                  Converter{this,         port_handle, cti,     0,
                            "",           0,           p_value, formula_to,
                            formula_from, values,      {},      std::nullopt,
                            std::nullopt};
            } else if (current_name == "IntSwissKnife") {
              auto formula = current->get<std::string>("Formula");

              std::map<std::string, std::string> values;
              for (auto &pair : *current) {
                if (pair.first == "pVariable") {
                  auto var_name =
                      pair.second.get_optional<std::string>("<xmlattr>.Name");
                  THALAMUS_ASSERT(var_name, "Name missing");
                  values[*var_name] = pair.second.data();
                } else if (pair.first == "Expression") {
                  auto var_name =
                      pair.second.get_optional<std::string>("<xmlattr>.Name");
                  std::regex label("\\b" + *var_name + "\\b",
                                   std::regex_constants::icase);
                  formula = std::regex_replace(formula, label,
                                               "(" + pair.second.data() + ")");
                }
              }

              nodes[node_name] =
                  IntSwissKnife{this, port_handle, cti,    0,  "",
                                0,    formula,     values, {}, std::nullopt};
            } else if (current_name == "SwissKnife") {
              auto formula = current->get<std::string>("Formula");

              std::map<std::string, std::string> values;
              for (auto &pair : *current) {
                if (pair.first == "pVariable") {
                  auto var_name =
                      pair.second.get_optional<std::string>("<xmlattr>.Name");
                  THALAMUS_ASSERT(var_name, "Name missing");
                  values[*var_name] = pair.second.data();
                } else if (pair.first == "Expression") {
                  auto var_name =
                      pair.second.get_optional<std::string>("<xmlattr>.Name");
                  std::regex label("\\b" + *var_name + "\\b",
                                   std::regex_constants::icase);
                  formula = std::regex_replace(formula, label,
                                               "(" + pair.second.data() + ")");
                }
              }

              nodes[node_name] =
                  SwissKnife{this, port_handle, cti,    0,  "",
                             0,    formula,     values, {}, std::nullopt};
            } else if (current_name == "MaskedIntReg") {
              size_t address = get_int<size_t>(*current, "Address", 0);
              auto p_address = current->get<std::string>("pAddress", "");
              auto access_mode = parse_access_mode(
                  current->get<std::string>("AccessMode", "RW"));

              auto int_swiss_knife_node =
                  current->get_child_optional("IntSwissKnife");
              std::string int_swiss_knife = "";
              if (int_swiss_knife_node.has_value()) {
                open.emplace_back("IntSwissKnife",
                                  &int_swiss_knife_node.value());
                int_swiss_knife =
                    int_swiss_knife_node->get<std::string>("<xmlattr>.Name");
              }

              auto lsb = current->get_optional<long long int>("LSB");
              auto msb = current->get_optional<long long int>("MSB");
              auto bit = current->get_optional<long long int>("Bit");
              if (bit) {
                lsb = msb = bit;
              }

              auto length = get_int<size_t>(*current, "Length", 0);
              auto little_endian =
                  current->get<std::string>("Endianess", "LittleEndian") ==
                  "LittleEndian";
              auto _unsigned =
                  current->get<std::string>("Sign", "Unsigned") == "Unsigned";
              nodes[node_name] =
                  IntReg{this,
                         port_handle,
                         cti,
                         address,
                         p_address,
                         int_swiss_knife,
                         length,
                         little_endian,
                         _unsigned,
                         access_mode,
                         lsb ? std::optional<long long int>(lsb.value())
                             : std::nullopt,
                         msb ? std::optional<long long int>(msb.value())
                             : std::nullopt,
                         {}};
            } else if (current_name == "StructReg") {
              size_t default_address = get_int<size_t>(*current, "Address", 0);
              auto default_p_address =
                  current->get<std::string>("pAddress", "");
              auto access_mode = parse_access_mode(
                  current->get<std::string>("AccessMode", "RW"));

              auto int_swiss_knife_node =
                  current->get_child_optional("IntSwissKnife");
              std::string default_int_swiss_knife = "";
              if (int_swiss_knife_node.has_value()) {
                open.emplace_back("IntSwissKnife",
                                  &int_swiss_knife_node.value());
                default_int_swiss_knife =
                    int_swiss_knife_node->get<std::string>("<xmlattr>.Name");
              }

              auto default_lsb = current->get_optional<long long int>("LSB");
              auto default_msb = current->get_optional<long long int>("MSB");
              auto default_bit = current->get_optional<long long int>("Bit");
              if (default_bit) {
                default_lsb = default_msb = default_bit;
              }

              auto default_length = get_int<size_t>(*current, "Length", 0);
              auto default_little_endian =
                  current->get<std::string>("Endianess", "LittleEndian");
              auto default_unsigned =
                  current->get<std::string>("Sign", "Unsigned");

              for (auto &pair : *current) {
                if (pair.first != "StructEntry") {
                  continue;
                }
                auto struct_name =
                    pair.second.get_optional<std::string>("<xmlattr>.Name");

                size_t address =
                    get_int(pair.second, "Address", default_address);
                auto p_address =
                    pair.second.get<std::string>("pAddress", default_p_address);

                auto lsb = pair.second.get_optional<long long int>("LSB");
                auto msb = pair.second.get_optional<long long int>("MSB");
                auto bit = pair.second.get_optional<long long int>("Bit");
                lsb = lsb ? lsb : default_lsb;
                msb = msb ? msb : default_msb;
                bit = bit ? bit : default_bit;
                if (bit) {
                  lsb = msb = bit;
                }

                auto length =
                    get_int<size_t>(pair.second, "Length", default_length);
                auto little_endian =
                    pair.second.get<std::string>(
                        "Endianess", default_little_endian) == "LittleEndian";
                auto _unsigned = pair.second.get<std::string>(
                                     "Sign", default_unsigned) == "Unsigned";

                nodes[*struct_name] =
                    IntReg{this,
                           port_handle,
                           cti,
                           address,
                           p_address,
                           default_int_swiss_knife,
                           length,
                           little_endian,
                           _unsigned,
                           access_mode,
                           lsb ? std::optional<long long int>(lsb.value())
                               : std::nullopt,
                           msb ? std::optional<long long int>(msb.value())
                               : std::nullopt,
                           {}};
              }
            } else if (current_name == "Enumeration") {
              auto p_value = current->get_optional<std::string>("pValue");
              auto value = get_optional<long long int>(*current, "Value");
              std::map<std::string, long long int> enums;
              std::map<long long int, std::string> reverse_enums;

              for (auto &pair : *current) {
                if (pair.first != "EnumEntry") {
                  continue;
                }
                auto enum_name =
                    pair.second.get_optional<std::string>("<xmlattr>.Name");
                auto enum_value =
                    get_optional<long long int>(pair.second, "Value");
                enums[*enum_name] = *enum_value;
                reverse_enums[*enum_value] = *enum_name;
              }
              if (p_value) {
                nodes[node_name] = Enumeration{this, *p_value, enums, {}};
              } else {
                auto text = reverse_enums.at(*value);
                nodes[node_name] = text;
              }
            }

            for (auto &pair : *current) {
              open.emplace_back(pair.first, &pair.second);
            }
          } catch (std::exception &e) {
            std::cout << e.what() << std::endl;
            return false;
          }
        }
        return true;
      }

      DeviceImpl(GenTL::DEV_HANDLE _dev_handle, Cti *_cti)
          : cti(_cti), dev_handle(_dev_handle) {
        TRACE_EVENT("thalamus", "DeviceImpl::DeviceImpl");

        auto error = cti->DevGetPort(dev_handle, &port_handle);
        if (error != GenTL::GC_ERR_SUCCESS) {
          THALAMUS_LOG(info) << "DevGetPort failed.";
          return;
        }

        auto success = load_xml();
        if (!success) {
          THALAMUS_LOG(info) << "Failed to load GenApi XML";
          return;
        }

        success = setup_registry();
        if (!success) {
          THALAMUS_LOG(info) << "Failed to setup GenApi registry";
          return;
        }

        unsigned int num_streams;
        error = cti->DevGetNumDataStreams(dev_handle, &num_streams);
        if (error != GenTL::GC_ERR_SUCCESS) {
          THALAMUS_LOG(info) << "DevGetNumDataStreams failed.";
          return;
        }

        if (num_streams == 0) {
          THALAMUS_LOG(info) << "No data streams.";
          return;
        }
        THALAMUS_LOG(info) << "Found " << num_streams << " data streams.";

        if (num_streams) {
          size_t id_size;
          error = cti->DevGetDataStreamID(dev_handle, 0, nullptr, &id_size);
          if (error != GenTL::GC_ERR_SUCCESS) {
            THALAMUS_LOG(info) << "DevGetDataStreamID failed.";
            return;
          }

          std::string stream_id(id_size, ' ');
          error = cti->DevGetDataStreamID(dev_handle, 0, stream_id.data(),
                                          &id_size);
          if (error != GenTL::GC_ERR_SUCCESS) {
            THALAMUS_LOG(info) << "DevGetDataStreamID failed.";
            return;
          }
          stream_id.resize(stream_id.size() - 1);

          error =
              cti->DevOpenDataStream(dev_handle, stream_id.c_str(), &ds_handle);
          if (error != GenTL::GC_ERR_SUCCESS) {
            THALAMUS_LOG(info) << "DevOpenDataStream failed.";
            return;
          }

          error = cti->GCRegisterEvent(ds_handle, GenTL::EVENT_NEW_BUFFER,
                                       &event_handle);
          if (error != GenTL::GC_ERR_SUCCESS) {
            THALAMUS_LOG(info) << "GCRegisterEvent failed.";
            return;
          }

          GenTL::INFO_DATATYPE info_type;
          size_t info_size;
          error = cti->EventGetInfo(event_handle, GenTL::EVENT_SIZE_MAX,
                                    &info_type, &buffer_size, &info_size);
          if (error != GenTL::GC_ERR_SUCCESS) {
            THALAMUS_LOG(info) << "EventGetInfo failed.";
            return;
          }
        }

        ready = true;
      }

      size_t buffer_size;
      std::vector<GenTL::BUFFER_HANDLE> buffer_handles;
      std::vector<std::vector<unsigned char>> buffer_data;
      std::thread stream_thread;
      std::atomic_bool streaming = false;
      boost::signals2::signal<void(const unsigned char *, int, int,
                                   std::chrono::steady_clock::time_point)>
          frame_ready;
      boost::asio::io_context *io_context;

      void start_stream(boost::asio::io_context &_io_context) {
        TRACE_EVENT("thalamus", "DeviceImpl::start_stream");
        if (streaming) {
          return;
        }
        this->io_context = &_io_context;

        GenTL::INFO_DATATYPE defines_type;
        bool8_t does_define;
        size_t define_size = sizeof(does_define);
        auto error =
            cti->DSGetInfo(ds_handle, GenTL::STREAM_INFO_DEFINES_PAYLOADSIZE,
                           &defines_type, &does_define, &define_size);
        if (error != GenTL::GC_ERR_SUCCESS) {
          THALAMUS_LOG(info) << "DSGetInfo failed.";
          return;
        }
        THALAMUS_ASSERT(does_define, "Payload size undefined");

        GenTL::INFO_DATATYPE payload_size_type;
        size_t payload_size;
        size_t payload_size_size = sizeof(payload_size);
        error = cti->DSGetInfo(ds_handle, GenTL::STREAM_INFO_PAYLOAD_SIZE,
                               &payload_size_type, &payload_size,
                               &payload_size_size);
        if (error != GenTL::GC_ERR_SUCCESS) {
          THALAMUS_LOG(info) << "DSGetInfo failed.";
          return;
        }

        GenTL::INFO_DATATYPE announce_min_type;
        size_t announce_min;
        size_t announce_min_size = sizeof(announce_min);
        error = cti->DSGetInfo(ds_handle, GenTL::STREAM_INFO_BUF_ANNOUNCE_MIN,
                               &announce_min_type, &announce_min,
                               &announce_min_size);
        if (error != GenTL::GC_ERR_SUCCESS) {
          THALAMUS_LOG(info) << "DSGetInfo failed.";
          return;
        }

        buffer_handles.resize(announce_min);
        buffer_data.resize(announce_min);
        auto frame_width = variant_cast<long long int>(get("Width"));
        auto frame_height = variant_cast<long long int>(get("Height"));
        for (auto i = 0ull; i < buffer_data.size(); ++i) {
          buffer_data.at(i) = std::vector<unsigned char>(payload_size, 0);
          auto &buffer = buffer_data.at(i);
          error = cti->DSAnnounceBuffer(
              ds_handle, buffer_data.at(i).data(), buffer.size(),
              reinterpret_cast<void *>(i), &buffer_handles.at(i));
          THALAMUS_ASSERT(error == GenTL::GC_ERR_SUCCESS,
                          "DSAnnounceBuffer failed: %d", error);

          cti->DSQueueBuffer(ds_handle, buffer_handles.at(i));
          THALAMUS_ASSERT(error == GenTL::GC_ERR_SUCCESS,
                          "DSQueueBuffer failed: %d", error);
        }

        error = cti->DSStartAcquisition(
            ds_handle, GenTL::ACQ_START_FLAGS_DEFAULT, GENTL_INFINITE);
        THALAMUS_ASSERT(error == GenTL::GC_ERR_SUCCESS,
                        "DSStartAcquisition failed: %d", error);
        execute("AcquisitionStart");

        streaming = true;
        stream_thread = std::thread(std::bind(&DeviceImpl::stream_target, this,
                                              frame_width, frame_height));
      }

      static std::atomic_uint global_frame;

      void stream_target(long long int frame_width,
                         long long int frame_height) {
        set_current_thread_name("GENTL");
        while (streaming) {
          GenTL::EVENT_NEW_BUFFER_DATA frame_data;
          size_t size = sizeof(GenTL::EVENT_NEW_BUFFER_DATA);
          auto error = cti->EventGetData(event_handle, &frame_data, &size,
                                         GENTL_INFINITE);
          if (error == GenTL::GC_ERR_ABORT) {
            break;
          }
          THALAMUS_ASSERT(error == GenTL::GC_ERR_SUCCESS,
                          "EventGetData failed: %d", error);
          auto frame_id = get_unique_id();
          TRACE_EVENT_BEGIN("thalamus", "Cti::GotFrame",
                            perfetto::Flow::ProcessScoped(frame_id));

          auto now = std::chrono::steady_clock::now();
          auto index = reinterpret_cast<size_t>(frame_data.pUserPointer);
          TRACE_EVENT_END("thalamus");
          io_context->post([this, &buffer = buffer_data.at(index), frame_id,
                            handle = frame_data.BufferHandle, frame_width,
                            frame_height, now] {
            TRACE_EVENT("thalamus", "GenicamNode Post Main",
                        perfetto::TerminatingFlow::ProcessScoped(frame_id));
            if (!streaming) {
              return;
            }
            frame_ready(buffer.data(), int(frame_width), int(frame_height),
                        now);
            TRACE_EVENT("thalamus", "Cti::DSQueueBuffer");
            auto queue_error = cti->DSQueueBuffer(ds_handle, handle);
            THALAMUS_ASSERT(queue_error == GenTL::GC_ERR_SUCCESS,
                            "DSQueueBuffer failed: %d", queue_error);
          });
        }
      }

      void stop_stream() {
        TRACE_EVENT("thalamus", "DeviceImpl::stop_stream");
        if (!streaming) {
          return;
        }
        THALAMUS_LOG(info) << "Stopping Stream";
        std::this_thread::sleep_for(1s);
        streaming = false;
        auto error = cti->EventKill(event_handle);
        THALAMUS_ASSERT(error == GenTL::GC_ERR_SUCCESS, "EventKill failed: %d",
                        error);
        stream_thread.join();
        execute("AcquisitionStop");
        error =
            cti->DSStopAcquisition(ds_handle, GenTL::ACQ_STOP_FLAGS_DEFAULT);
        THALAMUS_ASSERT(error == GenTL::GC_ERR_SUCCESS,
                        "DSStopAcquisition failed: %d", error);
        error = cti->DSFlushQueue(ds_handle, GenTL::ACQ_QUEUE_ALL_DISCARD);
        THALAMUS_ASSERT(error == GenTL::GC_ERR_SUCCESS,
                        "DSFlushQueue failed: %d", error);

        for (auto h : buffer_handles) {
          error = cti->DSRevokeBuffer(ds_handle, h, nullptr, nullptr);
          THALAMUS_ASSERT(error == GenTL::GC_ERR_SUCCESS,
                          "DSRevokeBuffer failed: %d", error);
        }
        buffer_data.clear();
        buffer_handles.clear();
      }

      void execute(const std::string &reg) {
        TRACE_EVENT("thalamus", "DeviceImpl::execute");
        auto i = nodes.find(reg);
        THALAMUS_ASSERT(i != nodes.end(), "Register not found: %s", reg);
        if (std::holds_alternative<Command>(i->second)) {
          return std::get<Command>(i->second).execute();
        }
        THALAMUS_ASSERT(false, "Register is not a command");
      }

      std::variant<long long int, std::string, double>
      get(const std::string &reg) override {
        TRACE_EVENT("thalamus", "DeviceImpl::get");
        auto i = nodes.find(reg);
        THALAMUS_ASSERT(i != nodes.end(), "Register not found: %s", reg);
        if (std::holds_alternative<long long int>(i->second)) {
          return std::get<long long int>(i->second);
        } else if (std::holds_alternative<double>(i->second)) {
          return std::get<double>(i->second);
        } else if (std::holds_alternative<std::string>(i->second)) {
          return std::get<std::string>(i->second);
        } else if (std::holds_alternative<StringReg>(i->second)) {
          return std::get<StringReg>(i->second).read();
        } else if (std::holds_alternative<IntReg>(i->second)) {
          return std::get<IntReg>(i->second).read();
        } else if (std::holds_alternative<IntConverter>(i->second)) {
          return std::get<IntConverter>(i->second).read();
        } else if (std::holds_alternative<IntSwissKnife>(i->second)) {
          return std::get<IntSwissKnife>(i->second).read();
        } else if (std::holds_alternative<FloatReg>(i->second)) {
          return std::get<FloatReg>(i->second).read();
        } else if (std::holds_alternative<Converter>(i->second)) {
          return std::get<Converter>(i->second).read();
        } else if (std::holds_alternative<SwissKnife>(i->second)) {
          return std::get<SwissKnife>(i->second).read();
        } else if (std::holds_alternative<Integer>(i->second)) {
          return std::get<Integer>(i->second).read();
        } else if (std::holds_alternative<Enumeration>(i->second)) {
          return std::get<Enumeration>(i->second).read();
        } else if (std::holds_alternative<Float>(i->second)) {
          return std::get<Float>(i->second).read();
        } else if (std::holds_alternative<Link>(i->second)) {
          return get(std::get<Link>(i->second).name);
        }
        THALAMUS_ASSERT(false, "Unexpected register type");
      }

      void set(const std::string &reg,
               const std::variant<long long int, std::string, double> &value)
          override {
        TRACE_EVENT("thalamus", "DeviceImpl::set");
        if (std::holds_alternative<long long int>(value)) {
          THALAMUS_LOG(debug)
              << reg << " int=" << std::get<long long int>(value);
        } else if (std::holds_alternative<std::string>(value)) {
          THALAMUS_LOG(debug)
              << reg << " string=" << std::get<std::string>(value);
        } else if (std::holds_alternative<double>(value)) {
          THALAMUS_LOG(debug) << reg << " double=" << std::get<double>(value);
        }
        auto i = nodes.find(reg);
        THALAMUS_ASSERT(i != nodes.end(), "Register not found: %s", reg);
        if (std::holds_alternative<Integer>(i->second)) {
          std::get<Integer>(i->second).write(
              variant_cast<long long int>(value));
        } else if (std::holds_alternative<IntReg>(i->second)) {
          std::get<IntReg>(i->second).write(variant_cast<long long int>(value));
        } else if (std::holds_alternative<IntConverter>(i->second)) {
          std::get<IntConverter>(i->second).write(
              variant_cast<long long int>(value));
        } else if (std::holds_alternative<Float>(i->second)) {
          std::get<Float>(i->second).write(variant_cast<double>(value));
        } else if (std::holds_alternative<FloatReg>(i->second)) {
          std::get<FloatReg>(i->second).write(variant_cast<double>(value));
        } else if (std::holds_alternative<Converter>(i->second)) {
          std::get<Converter>(i->second).write(variant_cast<double>(value));
        } else if (std::holds_alternative<Enumeration>(i->second)) {
          std::get<Enumeration>(i->second).write(
              variant_cast<std::string>(value));
        } else {
          THALAMUS_ASSERT(false, "Unexpected register type");
        }
      }

      bool exists(const std::string &reg) {
        return nodes.find(reg) != nodes.end();
      }

      bool is_writable(const std::string &reg) override {
        auto i = nodes.find(reg);
        if (i == nodes.end()) {
          return false;
        } else if (std::holds_alternative<long long int>(i->second)) {
          return false;
        } else if (std::holds_alternative<double>(i->second)) {
          return false;
        } else if (std::holds_alternative<std::string>(i->second)) {
          return false;
        } else if (std::holds_alternative<StringReg>(i->second)) {
          return std::get<StringReg>(i->second).access_mode != AccessMode::RO;
        } else if (std::holds_alternative<IntReg>(i->second)) {
          return std::get<IntReg>(i->second).access_mode != AccessMode::RO;
        } else if (std::holds_alternative<IntConverter>(i->second)) {
          return std::get<IntConverter>(i->second).is_writable();
        } else if (std::holds_alternative<IntSwissKnife>(i->second)) {
          return false;
        } else if (std::holds_alternative<FloatReg>(i->second)) {
          return std::get<FloatReg>(i->second).access_mode != AccessMode::RO;
        } else if (std::holds_alternative<Converter>(i->second)) {
          return std::get<Converter>(i->second).is_writable();
        } else if (std::holds_alternative<SwissKnife>(i->second)) {
          return false;
        } else if (std::holds_alternative<Integer>(i->second)) {
          return std::get<Integer>(i->second).is_writable();
        } else if (std::holds_alternative<Enumeration>(i->second)) {
          return std::get<Enumeration>(i->second).is_writable();
        } else if (std::holds_alternative<Float>(i->second)) {
          return std::get<Float>(i->second).is_writable();
        } else if (std::holds_alternative<Link>(i->second)) {
          return is_writable(std::get<Link>(i->second).name);
        }
        THALAMUS_ASSERT(false, "Unexpected Register type");
      }
    };
    std::map<std::string, std::shared_ptr<DeviceImpl>> devices;

    GenTL::TL_HANDLE tl_handle = nullptr;
    GenTL::IF_HANDLE if_handle = nullptr;

    Cti(const std::string &_name, const std::string &path) : name(_name) {
      TRACE_EVENT("thalamus", "Cti::Cti");
#ifdef _WIN32
      library_handle = LoadLibrary(path.c_str());
#else
      library_handle = dlopen(path.c_str(), RTLD_NOW);
#endif
      if (!library_handle) {
        THALAMUS_LOG(info) << "Couldn't find " << path << ".  " << name
                           << " disabled";
        return;
      }

#define LOAD_FUNC(name)                                                        \
  do {                                                                         \
    name = load_function<GenTL::P##name>(#name);                               \
    if (!name) {                                                               \
      return;                                                                  \
    }                                                                          \
  } while (0)
      LOAD_FUNC(GCGetInfo);
      LOAD_FUNC(GCGetLastError);
      LOAD_FUNC(GCInitLib);
      LOAD_FUNC(GCCloseLib);
      LOAD_FUNC(GCReadPort);
      LOAD_FUNC(GCWritePort);
      LOAD_FUNC(GCGetPortURL);
      LOAD_FUNC(GCGetPortInfo);

      LOAD_FUNC(GCRegisterEvent);
      LOAD_FUNC(GCUnregisterEvent);
      LOAD_FUNC(EventGetData);
      LOAD_FUNC(EventGetDataInfo);
      LOAD_FUNC(EventGetInfo);
      LOAD_FUNC(EventFlush);
      LOAD_FUNC(EventKill);
      LOAD_FUNC(TLOpen);
      LOAD_FUNC(TLClose);
      LOAD_FUNC(TLGetInfo);
      LOAD_FUNC(TLGetNumInterfaces);
      LOAD_FUNC(TLGetInterfaceID);
      LOAD_FUNC(TLGetInterfaceInfo);
      LOAD_FUNC(TLOpenInterface);
      LOAD_FUNC(TLUpdateInterfaceList);
      LOAD_FUNC(IFClose);
      LOAD_FUNC(IFGetInfo);
      LOAD_FUNC(IFGetNumDevices);
      LOAD_FUNC(IFGetDeviceID);
      LOAD_FUNC(IFUpdateDeviceList);
      LOAD_FUNC(IFGetDeviceInfo);
      LOAD_FUNC(IFOpenDevice);

      LOAD_FUNC(DevGetPort);
      LOAD_FUNC(DevGetNumDataStreams);
      LOAD_FUNC(DevGetDataStreamID);
      LOAD_FUNC(DevOpenDataStream);
      LOAD_FUNC(DevGetInfo);
      LOAD_FUNC(DevClose);

      LOAD_FUNC(DSAnnounceBuffer);
      LOAD_FUNC(DSAllocAndAnnounceBuffer);
      LOAD_FUNC(DSFlushQueue);
      LOAD_FUNC(DSStartAcquisition);
      LOAD_FUNC(DSStopAcquisition);
      LOAD_FUNC(DSGetInfo);
      LOAD_FUNC(DSGetBufferID);
      LOAD_FUNC(DSClose);
      LOAD_FUNC(DSRevokeBuffer);
      LOAD_FUNC(DSQueueBuffer);
      LOAD_FUNC(DSGetBufferInfo);

      LOAD_FUNC(GCGetNumPortURLs);
      LOAD_FUNC(GCGetPortURLInfo);

      auto error = GCInitLib();
      if (error != GenTL::GC_ERR_SUCCESS) {
        THALAMUS_LOG(info) << "GCInitLib failed.  " << name << " disabled";
        return;
      }
      gc_inited = true;

      error = TLOpen(&tl_handle);
      if (!tl_handle) {
        THALAMUS_LOG(info) << "TLOpen failed.  " << name << " disabled";
        return;
      }
      tl_opened = true;

      bool8_t changed;
      error = TLUpdateInterfaceList(tl_handle, &changed, 1000);
      if (error != GenTL::GC_ERR_SUCCESS) {
        THALAMUS_LOG(info) << "TLUpdateInterfaceList failed.  " << name
                           << " disabled";
        return;
      }

      unsigned int num_interfaces;
      error = TLGetNumInterfaces(tl_handle, &num_interfaces);
      if (error != GenTL::GC_ERR_SUCCESS) {
        THALAMUS_LOG(info) << "TLGetNumInterfaces failed.  " << name
                           << " disabled";
        return;
      }
      THALAMUS_LOG(info) << "Num interfaces = " << num_interfaces;

      std::string interface_id;
      for (auto i = 0u; i < num_interfaces; ++i) {
        size_t id_size;
        error = TLGetInterfaceID(tl_handle, i, nullptr, &id_size);
        if (error != GenTL::GC_ERR_SUCCESS) {
          THALAMUS_LOG(info)
              << "TLGetInterfaceID failed.  " << name << " disabled";
          return;
        }
        interface_id.resize(id_size);
        error = TLGetInterfaceID(tl_handle, i, interface_id.data(), &id_size);
        if (error != GenTL::GC_ERR_SUCCESS) {
          THALAMUS_LOG(info)
              << "TLGetInterfaceID failed.  " << name << " disabled";
          return;
        }
        interface_id.resize(interface_id.size() - 1);
        THALAMUS_LOG(info) << "Interface ID = " << interface_id;

        GenTL::INFO_DATATYPE type;
        std::string tltype;
        error = TLGetInterfaceInfo(tl_handle, interface_id.c_str(),
                                   GenTL::INTERFACE_INFO_TLTYPE, &type, nullptr,
                                   &id_size);
        if (error != GenTL::GC_ERR_SUCCESS) {
          THALAMUS_LOG(info)
              << "TLGetInterfaceInfo failed.  " << name << " disabled";
          return;
        }
        tltype.resize(id_size);

        error = TLGetInterfaceInfo(tl_handle, interface_id.c_str(),
                                   GenTL::INTERFACE_INFO_TLTYPE, &type,
                                   tltype.data(), &id_size);
        if (error != GenTL::GC_ERR_SUCCESS) {
          THALAMUS_LOG(info)
              << "TLGetInterfaceInfo failed.  " << name << " disabled";
          return;
        }
        tltype.resize(tltype.size() - 1);
        THALAMUS_LOG(info) << "  TLTYPE = " << tltype;

        if (tltype == TLTypeU3VName) {
          error = TLOpenInterface(tl_handle, interface_id.data(), &if_handle);
          if (error != GenTL::GC_ERR_SUCCESS) {
            THALAMUS_LOG(info)
                << "TLOpenInterface failed.  " << name << " disabled";
            return;
          }

          error = IFUpdateDeviceList(if_handle, &changed, 1000);
          if (error != GenTL::GC_ERR_SUCCESS) {
            THALAMUS_LOG(info)
                << "IFUpdateDeviceList failed.  " << name << " disabled";
            return;
          }

          unsigned int num_devices;
          error = IFGetNumDevices(if_handle, &num_devices);
          if (error != GenTL::GC_ERR_SUCCESS) {
            THALAMUS_LOG(info)
                << "IFGetNumDevices failed.  " << name << " disabled";
            return;
          }
          THALAMUS_LOG(info) << "Num devices = " << num_devices;

          for (auto j = 0u; j < num_devices; ++j) {
            error = IFGetDeviceID(if_handle, j, nullptr, &id_size);
            if (error != GenTL::GC_ERR_SUCCESS) {
              THALAMUS_LOG(info)
                  << "IFGetDeviceID failed.  " << name << " disabled";
              return;
            }
            std::string device_id;
            device_id.resize(id_size);
            error = IFGetDeviceID(if_handle, j, device_id.data(), &id_size);
            if (error != GenTL::GC_ERR_SUCCESS) {
              THALAMUS_LOG(info)
                  << "IFGetDeviceID failed.  " << name << " disabled";
              return;
            }
            device_id.resize(device_id.size() - 1);
            THALAMUS_LOG(info) << "Device ID = " << device_id;

            GenTL::INFO_DATATYPE vendor_type;
            size_t vendor_size;
            error = IFGetDeviceInfo(if_handle, device_id.c_str(),
                                    GenTL::DEVICE_INFO_VENDOR, &vendor_type,
                                    nullptr, &vendor_size);
            if (error != GenTL::GC_ERR_SUCCESS) {
              THALAMUS_LOG(info)
                  << "IFGetDeviceInfo failed.  " << name << " disabled";
              return;
            }
            std::string vendor;
            vendor.resize(vendor_size);
            error = IFGetDeviceInfo(if_handle, device_id.c_str(),
                                    GenTL::DEVICE_INFO_VENDOR, &vendor_type,
                                    vendor.data(), &vendor_size);
            if (error != GenTL::GC_ERR_SUCCESS) {
              THALAMUS_LOG(info)
                  << "IFGetDeviceInfo failed.  " << name << " disabled";
              return;
            }
            vendor.resize(vendor.size() - 1);
            if (interface_id.find("IDS") != std::string::npos &&
                vendor.find("IDS") == std::string::npos) {
              THALAMUS_LOG(info)
                  << "IDS GenTL detected another vendor's camera, ignoring.  "
                  << vendor;
              return;
            }

            GenTL::DEV_HANDLE dev_handle;
            error = IFOpenDevice(if_handle, device_id.c_str(),
                                 GenTL::DEVICE_ACCESS_EXCLUSIVE, &dev_handle);
            if (error != GenTL::GC_ERR_SUCCESS) {
              THALAMUS_LOG(info)
                  << "IFOpenDevice failed.  " << name << " disabled";
              return;
            }

            auto new_device = std::make_shared<DeviceImpl>(dev_handle, this);
            devices[device_id] = std::move(new_device);
          }
        }
      }
      if (!if_handle) {
        THALAMUS_LOG(info) << "No suitable interface found.  " << name
                           << " disabled";
        return;
      }

      THALAMUS_LOG(info) << "Successfully loaded " << path << ".  " << name
                         << " enabled";
      loaded = true;
    }
    ~Cti() {
      devices.clear();
      if (if_handle) {
        IFClose(if_handle);
      }
      if (tl_handle) {
        TLClose(tl_handle);
      }
      if (gc_inited) {
        GCCloseLib();
      }
    }
  };

  static std::vector<std::unique_ptr<Cti>> *ctis;
  static calculator::parser<std::string::const_iterator> *parser;

  static void load_ctis() {
    if (!ctis) {
      TRACE_EVENT("thalamus", "Cti::load_ctis");
      ctis = new std::vector<std::unique_ptr<Cti>>();
      parser = new calculator::parser<std::string::const_iterator>();

      auto envval = std::getenv("GENICAM_GENTL64_PATH");
      if (envval == nullptr) {
        return;
      }
#ifdef _WIN32
      auto paths = absl::StrSplit(envval, ';');
#else
      auto paths = absl::StrSplit(envval, ':');
#endif
      for (auto &path : paths) {
        if (!std::filesystem::exists(std::filesystem::path(path))) {
          continue;
        }
        for (auto &file : std::filesystem::directory_iterator(path)) {
          THALAMUS_LOG(info) << "Loading " << file;
          if (file.path().extension() == ".cti") {
            ctis->emplace_back(
                new Cti(file.path().stem().string(), file.path().string()));
          }
        }
      }
      auto end = std::remove_if(ctis->begin(), ctis->end(),
                                [](auto &cti) { return !cti->loaded; });
      ctis->erase(end, ctis->end());
      for (auto &i : *ctis) {
        THALAMUS_LOG(info) << i->name;
      }
    }
  }

  std::shared_ptr<Cti::DeviceImpl> device;
  std::string default_camera;
  boost::signals2::scoped_connection frame_connection;
  std::once_flag load_ctis_flag;

  Impl(ObservableDictPtr _state, boost::asio::io_context &_io_context,
       GenicamNode *_outer)
      : io_context(_io_context), state(_state), outer(_outer) {
    TRACE_EVENT("thalamus", "GenicamNode::Impl::Impl");
    using namespace std::placeholders;

    load_ctis();

    analog_impl.inject({{std::span<double const>()}}, {0ns}, {""});

    analog_impl.ready.connect([_outer](Node *) { _outer->ready(_outer); });

    for (auto &cti : *ctis) {
      if (!cti->devices.empty()) {
        default_camera = cti->name + ":" + cti->devices.begin()->first;
      }
    }
    if (default_camera.empty()) {
      return;
    }
    if (!state->contains("Camera")) {
      (*state)["Camera"].assign(default_camera);
    } else {
      initialize_camera(true);
    }

    state_connection =
        state->changed.connect(std::bind(&Impl::on_change, this, _1, _2, _3));
  }

  ~Impl() {
    (*state)["Running"].assign(false, [&] {});
  }

  std::vector<std::chrono::steady_clock::time_point> frame_times;
  double framerate = 0;
  double target_framerate = 0;

  void on_frame_ready(const unsigned char *frame_data, int frame_width,
                      int frame_height,
                      std::chrono::steady_clock::time_point now) {
    TRACE_EVENT("thalamus", "GenicamNode::on_frame_ready");
    while (!frame_times.empty() && now - frame_times.front() >= 1s) {
      std::pop_heap(frame_times.begin(), frame_times.end(),
                    [](auto &l, auto &r) { return l > r; });
      frame_times.pop_back();
    }
    if (!frame_times.empty()) {
      auto duration = now - frame_times.front();
      auto duration_seconds =
          double(duration.count()) / decltype(duration)::period::den;
      framerate = double(frame_times.size()) / duration_seconds;
    } else {
      framerate = 0;
    }
    frame_times.push_back(now);
    std::push_heap(frame_times.begin(), frame_times.end(),
                   [](auto &l, auto &r) { return l > r; });

    this->time = now.time_since_epoch();
    this->data.clear();
    this->data.emplace_back(frame_data, frame_data + width * height);
    this->width = size_t(frame_width);
    this->height = size_t(frame_height);
    this->has_image = true;
    this->has_analog = true;
    TRACE_EVENT("thalamus", "GenicamNode::on_frame_ready");
    analog_impl.inject(
        {std::span<const double>(&framerate, &framerate + 1)},
        {std::chrono::nanoseconds(size_t(1e9 / target_framerate))}, {""});
  }

  void initialize_camera(bool apply_state = false) {
    TRACE_EVENT("thalamus", "GenicamNode::initialize_camera");
    std::string camera = state->at("Camera");
    std::vector<std::string> tokens = absl::StrSplit(camera, ':');
    if (tokens.size() < 2) {
      (*state)["Camera"].assign(default_camera);
      return;
    }

    for (auto &cti : *ctis) {
      if (tokens[0] != cti->name) {
        continue;
      }
      auto i = cti->devices.find(tokens[1]);
      if (i != cti->devices.end()) {
        this->device = i->second;
        frame_connection = this->device->frame_ready.connect(
            std::bind(&Impl::on_frame_ready, this, _1, _2, _3, _4));
        break;
      }
      (*state)["Camera"].assign(default_camera);
      return;
    }

    if (!device) {
      return;
    }

    if (!device->streaming) {
      if (device->is_writable("GainAuto")) {
        device->set("GainAuto", "Off");
      }
      if (device->is_writable("ExposureAuto")) {
        device->set("ExposureAuto", "Off");
      }
      if (device->is_writable("ExposureMode")) {
        device->set("ExposureMode", "Timed");
      }
      if (device->is_writable("AcquisitionFrameRateAuto")) {
        device->set("AcquisitionFrameRateAuto", "Off");
      }
      if (device->is_writable("AcquisitionFrameRateMode")) {
        device->set("AcquisitionFrameRateMode", "Basic");
      }
      device->set("AcquisitionMode", "Continuous");
    }

    {
      auto value = variant_cast<long long int>(this->device->get("WidthMax"));
      (*state)["WidthMax"].assign(value);
      value = variant_cast<long long int>(this->device->get("HeightMax"));
      (*state)["HeightMax"].assign(value);
    }

    if (apply_state && state->contains("Width")) {
      long long int state_width = state->at("Width");
      this->device->set("Width", state_width);
    } else {
      long long int value =
          variant_cast<long long int>(this->device->get("Width"));
      (*state)["Width"].assign(value);
    }
    if (apply_state && state->contains("Height")) {
      long long int value = state->at("Height");
      this->device->set("Height", value);
    } else {
      long long int value =
          variant_cast<long long int>(this->device->get("Height"));
      (*state)["Height"].assign(value);
    }
    if (apply_state && state->contains("OffsetX")) {
      long long int value = state->at("OffsetX");
      this->device->set("OffsetX", value);
    } else {
      long long int value =
          variant_cast<long long int>(this->device->get("OffsetX"));
      (*state)["OffsetX"].assign(value);
    }
    if (apply_state && state->contains("OffsetY")) {
      long long int value = state->at("OffsetY");
      this->device->set("OffsetY", value);
    } else {
      long long int value =
          variant_cast<long long int>(this->device->get("OffsetY"));
      (*state)["OffsetY"].assign(value);
    }
    if (apply_state && state->contains("ExposureTime")) {
      double value = state->at("ExposureTime");
      this->device->set("ExposureTime", value);
    } else {
      auto value = variant_cast<double>(this->device->get("ExposureTime"));
      (*state)["ExposureTime"].assign(value);
    }
    if (apply_state && state->contains("AcquisitionFrameRate")) {
      double value = state->at("AcquisitionFrameRate");
      target_framerate = value;
      this->device->set("AcquisitionFrameRate", value);
    } else {
      auto value =
          variant_cast<double>(this->device->get("AcquisitionFrameRate"));
      target_framerate = value;
      (*state)["AcquisitionFrameRate"].assign(value);
    }
    if (apply_state && state->contains("Gain")) {
      double value = state->at("Gain");
      this->device->set("Gain", value);
    } else {
      auto value = variant_cast<double>(this->device->get("Gain"));
      (*state)["Gain"].assign(value);
    }
  }

  void on_change(ObservableCollection::Action,
                 const ObservableCollection::Key &k,
                 const ObservableCollection::Value &v) {
    TRACE_EVENT("thalamus", "GenicamNode::on_change");
    auto key_str = std::get<std::string>(k);
    if (key_str == "Camera") {
      initialize_camera();
      return;
    } else if (key_str == "Running") {
      running = variant_cast<bool>(v);
      if (device) {
        if (running) {
          device->start_stream(io_context);
        } else {
          device->stop_stream();
        }
      }
      return;
    }

    if (!device) {
      return;
    }

    if (key_str == "Width") {
      auto value = variant_cast<long long int>(v);
      this->device->set("Width", value);
      auto new_value = variant_cast<long long int>(this->device->get("Width"));
      if (value != new_value) {
        (*state)["Width"].assign(new_value);
      }
    } else if (key_str == "Height") {
      auto value = variant_cast<long long int>(v);
      this->device->set("Height", value);
      auto new_value = variant_cast<long long int>(this->device->get("Height"));
      if (value != new_value) {
        (*state)["Height"].assign(new_value);
      }
    } else if (key_str == "OffsetX") {
      auto value = variant_cast<long long int>(v);
      this->device->set("OffsetX", value);
      auto new_value =
          variant_cast<long long int>(this->device->get("OffsetX"));
      if (value != new_value) {
        (*state)["OffsetX"].assign(new_value);
      }
    } else if (key_str == "OffsetY") {
      auto value = variant_cast<long long int>(v);
      this->device->set("OffsetY", value);
      auto new_value =
          variant_cast<long long int>(this->device->get("OffsetY"));
      if (value != new_value) {
        (*state)["OffsetY"].assign(new_value);
      }
    } else if (key_str == "ExposureTime") {
      auto value = variant_cast<double>(v);
      this->device->set("ExposureTime", value);
      auto new_value = variant_cast<double>(this->device->get("ExposureTime"));
      if (std::abs(value - new_value) > 1) {
        (*state)["ExposureTime"].assign(new_value);
      }
    } else if (key_str == "AcquisitionFrameRate") {
      auto value = variant_cast<double>(v);
      this->device->set("AcquisitionFrameRate", value);
      auto new_value =
          variant_cast<double>(this->device->get("AcquisitionFrameRate"));
      target_framerate = new_value;
      if (std::abs(value - new_value) > 1) {
        (*state)["AcquisitionFrameRate"].assign(new_value);
      }
    } else if (key_str == "Gain") {
      auto value = variant_cast<double>(v);
      this->device->set("Gain", value);
      auto new_value = variant_cast<double>(this->device->get("Gain"));
      if (std::abs(value - new_value) > 1) {
        (*state)["Gain"].assign(new_value);
      }
    }
  }
};

GenicamNode::GenicamNode(ObservableDictPtr state,
                         boost::asio::io_context &io_context, NodeGraph *)
    : impl(new Impl(state, io_context, this)) {}

GenicamNode::~GenicamNode() {}

std::string GenicamNode::type_name() { return "GENICAM"; }

ImageNode::Plane GenicamNode::plane(int i) const {
  return impl->data.at(size_t(i));
}

size_t GenicamNode::num_planes() const { return impl->data.size(); }

ImageNode::Format GenicamNode::format() const {
  return ImageNode::Format::Gray;
}

size_t GenicamNode::width() const { return impl->width; }

size_t GenicamNode::height() const { return impl->height; }

void GenicamNode::inject(const thalamus_grpc::Image &) {
  THALAMUS_ASSERT(false, "Unimplemented");
}

std::chrono::nanoseconds GenicamNode::time() const { return impl->time; }

std::chrono::nanoseconds GenicamNode::frame_interval() const {
  return std::chrono::nanoseconds(size_t(1e9 / impl->target_framerate));
}

bool GenicamNode::prepare() { return true; }

void GenicamNode::cleanup() {
  if (Impl::ctis) {
    // delete Impl::ctis;
    // delete Impl::parser;
    Impl::ctis = nullptr;
    Impl::parser = nullptr;
  }
}

std::span<const double> GenicamNode::data(int index) const {
  return impl->analog_impl.data(index);
}

int GenicamNode::num_channels() const {
  return impl->analog_impl.num_channels();
}

std::chrono::nanoseconds GenicamNode::sample_interval(int channel) const {
  return impl->analog_impl.sample_interval(channel);
}

std::string_view GenicamNode::name(int channel) const {
  if (channel == 0) {
    return "Framerate";
  } else {
    return "";
  }
}

void GenicamNode::inject(
    const thalamus::vector<std::span<double const>> &data,
    const thalamus::vector<std::chrono::nanoseconds> &interval,
    const thalamus::vector<std::string_view> &_names) {
  impl->has_analog = true;
  impl->has_image = false;
  impl->analog_impl.inject(data, interval, _names);
}

bool GenicamNode::has_analog_data() const { return impl->has_analog; }

bool GenicamNode::has_image_data() const { return impl->has_image; }

boost::json::value GenicamNode::process(const boost::json::value &request) {
  TRACE_EVENT("thalamus", "GenicamNode::process");
  if (request.kind() != boost::json::kind::string) {
    return boost::json::value();
  }

  auto request_str = request.as_string();
  if (request_str == "get_cameras") {
    boost::json::array result;
    for (auto &cti : *Impl::ctis) {
      for (auto &pair : cti->devices) {
        result.push_back(boost::json::string(cti->name + ":" + pair.first));
      }
    }
    return result;
  }
  return boost::json::value();
}
size_t GenicamNode::modalities() const {
  return infer_modalities<GenicamNode>();
}

std::atomic_uint GenicamNode::Impl::Cti::DeviceImpl::global_frame = 0;
std::vector<std::unique_ptr<GenicamNode::Impl::Cti>> *GenicamNode::Impl::ctis =
    nullptr;
calculator::parser<std::string::const_iterator> *GenicamNode::Impl::parser =
    nullptr;
} // namespace thalamus
