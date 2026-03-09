#include <thalamus/shared_library.hpp>
#include <thalamus/log.hpp>

#ifdef _WIN32
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

using namespace thalamus;

struct SharedLibrary::Impl {
  std::string name;
#ifdef _WIN32
      HMODULE library_handle;
#else
      void* library_handle;
#endif
  Impl(const std::string& _name) : name(_name) {
#ifdef _WIN32
      library_handle = LoadLibrary(name.c_str());
#else
      library_handle = dlopen(name.c_str(), RTLD_NOW);
#endif
  }
  ~Impl() {
#ifdef _WIN32
      FreeLibrary(library_handle);
#else
      dlclose(library_handle);
#endif
  }
};

SharedLibrary::SharedLibrary(const std::string& name) : impl(new Impl(name)) {}
SharedLibrary::SharedLibrary(SharedLibrary&& that) : impl(std::move(that.impl)) {}
SharedLibrary::~SharedLibrary() {}

#ifdef _WIN32
FARPROC SharedLibrary::load_address(const std::string& name) {
  auto result = ::GetProcAddress(impl->library_handle, name.c_str());
  if (!result) {
    THALAMUS_LOG(info) << "Failed to load " << name << ".  "
                       << impl->name << " disabled";
  }
  return result;
}
#else
void SharedLibrary::load_address(const std::string& name) {
  auto result = dlsym(library_handle, name.c_str());
  if (!result) {
    THALAMUS_LOG(info) << "Failed to load " << name << ".  "
                       << impl->name << " disabled";
  }
  return result;
}
#endif
