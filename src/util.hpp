#pragma once
#include <functional>
#include <memory>
#include <chrono>
#include <map>
#include <variant>
#include <exception>

#ifdef __clang__
  #pragma clang diagnostic push
  #pragma clang diagnostic ignored "-Weverything"
#endif

#include <absl/strings/str_format.h>
#include <boost/stacktrace.hpp>
#include <boost/exception/info.hpp>
#include <boost/exception/get_error_info.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/utility/manipulators.hpp>

#ifdef __clang__
  #pragma clang diagnostic pop
#endif

#define THALAMUS_LOG(LEVEL) BOOST_LOG_TRIVIAL(LEVEL) \
  << boost::log::add_value("Line", __LINE__) \
  << boost::log::add_value("File", thalamus::filename(__FILE__)) \
  << boost::log::add_value("Function", __FUNCTION__)

//#ifdef NDEBUG
//#define THALAMUS_ASSERT(condition, ...) if(!(condition)) {throw boost::enable_error_info(std::runtime_error(absl::StrFormat(__VA_ARGS__))) << traced(boost::stacktrace::stacktrace(2, std::numeric_limits<size_t>::max()));}
//#else
#define THALAMUS_ASSERT(condition, ...) do { if(!(condition)) {THALAMUS_LOG(fatal) << absl::StrFormat("" __VA_ARGS__) << "\n" << boost::stacktrace::stacktrace(2, std::numeric_limits<size_t>::max()); std::abort(); } } while(0)
#define THALAMUS_ABORT(...) THALAMUS_LOG(fatal) << absl::StrFormat("" __VA_ARGS__) << "\n" << boost::stacktrace::stacktrace(2, std::numeric_limits<size_t>::max()); std::abort()
#define THALAMUS_ABORT_WITH_SKIP(skip, ...) THALAMUS_LOG(fatal) << absl::StrFormat("" __VA_ARGS__) << "\n" << boost::stacktrace::stacktrace(2+skip, std::numeric_limits<size_t>::max()); std::abort()

//#endif

#define THALAMUS_THROW(exc) throw boost::enable_error_info(exc) << thalamus::traced(boost::stacktrace::stacktrace(2, std::numeric_limits<size_t>::max()));
#define THALAMUS_THROW_WITH_SKIP(exc, skip) throw boost::enable_error_info(exc) << thalamus::traced(boost::stacktrace::stacktrace(2+skip, std::numeric_limits<size_t>::max()));

namespace thalamus {
  typedef boost::error_info<struct tag_stacktrace, boost::stacktrace::stacktrace> traced;
  consteval std::string_view filename(const std::string_view& path) {
    auto filename_start = path.find_last_of("/\\");
    return filename_start == std::string::npos ? path : path.substr(filename_start + 1);
  }

  template<typename T>
  T StacktraceAndThrowOnException(std::function<T()> func) {
    try {
      return func();
    }
    catch (std::exception const& e)
    {
      const boost::stacktrace::stacktrace* st = boost::get_error_info<thalamus::traced>(e);
      if (st) {
        THALAMUS_LOG(fatal) << e.what() << "\n" << *st;
      }
      throw e;
    }
  }

  template<typename T>
  T StacktraceAndContinueOnException(std::function<T()> func) {
    try {
      return func();
    }
    catch (std::exception const& e)
    {
      const boost::stacktrace::stacktrace* st = boost::get_error_info<thalamus::traced>(e);
      if (st) {
        THALAMUS_LOG(fatal) << e.what() << "\n" << *st;
      }
      return T();
    }
  }

  template<typename T>
  T StacktraceAndAbortOnException(std::function<T()> func) {
    try {
      return func();
    }
    catch (std::exception const& e)
    {
      const boost::stacktrace::stacktrace* st = boost::get_error_info<thalamus::traced>(e);
      if (st) {
        THALAMUS_LOG(fatal) << e.what() << "\n" << *st;
      }
      else {
        THALAMUS_LOG(fatal) << e.what() << "\nStack trace missing";
      }
      std::abort();
    }
  }

  template<typename KEY, typename... VALUES>
  KEY get(const std::variant<VALUES...>& arg) {
    try {
      return std::get<KEY>(arg);
    } catch (std::bad_variant_access& e) {
      THALAMUS_ABORT("%s", e.what());
    }
  }

  template<typename T>
  class vector : public std::vector<T> {
  public:
    vector() {}
    vector(size_t size, const T& initial = T()) {
      try {
        std::vector<T>::assign(size, initial);
      }
      catch (std::exception& e) {
        THALAMUS_ABORT("%s", e.what());
      }
    }
    template<typename INPUT>
    vector(INPUT first, INPUT last) {
      try {
        std::vector<T>::assign(first, last);
      }
      catch (std::exception& e) {
        THALAMUS_ABORT("%s", e.what());
      }
    }

    vector(std::initializer_list<T> values) : std::vector<T>(values) {}

    void assign(size_t size, const T& initial = T()) {
      try {
        std::vector<T>::assign(size, initial);
      }
      catch (std::exception& e) {
        THALAMUS_ABORT("%s", e.what());
      }
    }
    template<typename INPUT>
    void assign(INPUT first, INPUT last) {
      try {
        std::vector<T>::assign(first, last);
      }
      catch (std::exception& e) {
        THALAMUS_ABORT("%s", e.what());
      }
    }

    T& operator[](size_t i) {
      try {
        return std::vector<T>::at(i);
      } catch(std::exception& e) {
#ifdef _WIN32
        THALAMUS_ABORT_WITH_SKIP(5, "%s", e.what());
#else
        THALAMUS_ABORT("%s", e.what());
#endif
      }
    }
    const T& operator[](size_t i) const {
      try {
        return std::vector<T>::at(i);
      }
      catch (std::exception & e) {
#ifdef _WIN32
        THALAMUS_ABORT_WITH_SKIP(5, "%s", e.what());
#else
        THALAMUS_ABORT("%s", e.what());
#endif
      }
    }
    T& at(size_t i) {
      try {
        return std::vector<T>::at(i);
      }
      catch (std::exception & e) {
#ifdef _WIN32
        THALAMUS_ABORT_WITH_SKIP(5, "%s", e.what());
#else
        THALAMUS_ABORT("%s", e.what());
#endif
      }
    }
    const T& at(size_t i) const {
      try {
        return std::vector<T>::at(i);
      }
      catch (std::exception & e) {
#ifdef _WIN32
        THALAMUS_ABORT_WITH_SKIP(5, "%s", e.what());
#else
        THALAMUS_ABORT("%s", e.what());
#endif
      }
    }
  };

  template<typename K, typename V>
  class map : public std::map<K,V> {
  public:
    map() : std::map<K, V>() {}
    V& at(const K& k) {
      try {
        return std::map<K,V>::at(k);
      }
      catch (std::exception & e) {
#ifdef _WIN32
        THALAMUS_ABORT_WITH_SKIP(5, "%s", e.what());
#else
        THALAMUS_ABORT("%s", e.what());
#endif
      }
    }
    const V& at(const K& k) const {
      try {
        return std::map<K, V>::at(k);
      }
      catch (std::exception & e) {
#ifdef _WIN32
        THALAMUS_ABORT_WITH_SKIP(5, "%s", e.what());
#else
        THALAMUS_ABORT("%s", e.what());
#endif
      }
    }
  };

  template<typename T>
  class optional : public std::optional<T> {
  public:
    optional() {}
    optional(std::nullopt_t arg) : std::optional<T>(arg) {}
    optional(const T& arg) : std::optional<T>(arg) {}
    optional(T&& arg) : std::optional<T>(std::move(arg)) {}
    T& operator*() {
      try {
        return std::optional<T>::value();
      } catch (std::exception& e) {
//#ifdef _WIN32
//        THALAMUS_ABORT_WITH_SKIP(5, "%s", e.what());
//#else
        THALAMUS_ABORT("%s", e.what());
//#endif
      }
    }
    const T& operator*() const {
      try {
        return std::optional<T>::value();
      }
      catch (std::exception& e) {
        //#ifdef _WIN32
        //        THALAMUS_ABORT_WITH_SKIP(5, "%s", e.what());
        //#else
        THALAMUS_ABORT("%s", e.what());
        //#endif
      }
    }
    T* operator->() {
      try {
        return &std::optional<T>::value();
      }
      catch (std::exception& e) {
        //#ifdef _WIN32
        //        THALAMUS_ABORT_WITH_SKIP(5, "%s", e.what());
        //#else
        THALAMUS_ABORT("%s", e.what());
        //#endif
      }
    }
    const T* operator->() const {
      try {
        return &std::optional<T>::value();
      }
      catch (std::exception& e) {
        //#ifdef _WIN32
        //        THALAMUS_ABORT_WITH_SKIP(5, "%s", e.what());
        //#else
        THALAMUS_ABORT("%s", e.what());
        //#endif
      }
    }
    const T& value() const {
      try {
        return std::optional<T>::value();
      }
      catch (std::exception& e) {
        //#ifdef _WIN32
        //        THALAMUS_ABORT_WITH_SKIP(5, "%s", e.what());
        //#else
        THALAMUS_ABORT("%s", e.what());
        //#endif
      }
    }
    optional<T>& operator=(std::nullopt_t arg) {
      std::optional<T>::operator=(arg);
      return *this;
    }
    optional<T>& operator=(const T& arg) {
      std::optional<T>::operator=(arg);
      return *this;
    }
    optional<T>& operator=(T&& arg) {
      std::optional<T>::operator=(std::move(arg));
      return *this;
    }
  };

  const double PI = 3.14159265358979323846;

  struct ReceivedEvent {
    std::chrono::nanoseconds::rep time;
    size_t index;
  };
}
