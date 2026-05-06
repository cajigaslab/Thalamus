#pragma once

#include <memory>
#include <string>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace thalamus {
  class SharedLibrary {
    struct Impl;
    std::unique_ptr<Impl> impl;
#ifdef _WIN32
    FARPROC load_address(const std::string&);
#else
    void* load_address(const std::string&);
#endif
  public:
    SharedLibrary(const std::string&);
    SharedLibrary(SharedLibrary&&);
    ~SharedLibrary();
    template <typename T> T load(const std::string &name) {
      auto address = load_address(name);
#ifdef _WIN32
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-function-type"
#pragma clang diagnostic ignored "-Wcast-function-type-strict"
#endif
#endif
      return reinterpret_cast<T>(address);
#ifdef _WIN32
#ifdef __clang__
#pragma clang diagnostic pop
#endif
#endif
    }
  };
}
