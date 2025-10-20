#pragma once

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include <boost/log/trivial.hpp>
#include <boost/log/utility/manipulators.hpp>

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#define THALAMUS_LOG(LEVEL)                                                    \
  BOOST_LOG_TRIVIAL(LEVEL) << boost::log::add_value("Line", __LINE__)          \
                           << boost::log::add_value(                           \
                                  "File", thalamus::filename(__FILE__))        \
                           << boost::log::add_value("Function", __FUNCTION__)
                           
namespace thalamus {
consteval std::string_view filename(const std::string_view &path) {
  auto filename_start = path.find_last_of("/\\");
  return filename_start == std::string::npos ? path
                                             : path.substr(filename_start + 1);
}
}
