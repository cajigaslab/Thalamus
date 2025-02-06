#include <thalamus/thread.hpp>
#ifdef _WIN32
#include <Windows.h>
#elif __APPLE__
#include <pthread.h>
#else
#include <sys/prctl.h>
#endif

namespace thalamus {
void set_current_thread_name(const std::string &name) {
#ifdef _WIN32
  std::wstring wname(name.begin(), name.end());
  SetThreadDescription(GetCurrentThread(), wname.c_str());
#elif __APPLE__
  pthread_setname_np(name.c_str());
#else
  prctl(PR_SET_NAME, name.c_str(), 0, 0, 0);
#endif
}
} // namespace thalamus
