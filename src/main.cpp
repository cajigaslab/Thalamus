#include <string>
#include <vector>
#include <map>
#include <exception>
#include <iostream>
#include <functional>
#include <filesystem>
#include <thalamus_config.h>

#ifdef _WIN32
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

const auto HELP = 
"Thalamus native program, version " GIT_COMMIT_HASH "\n"
"  thalamus         Signal tool\n"
"  hydrate          Thalamus capture parsing\n";

std::map<std::string, std::function<int(int, char**)>> COMMANDS;

#ifdef _WIN32
HMODULE library_handle;
template<typename T>
T load_function_impl(const std::string& name) {
  return reinterpret_cast<T>(::GetProcAddress(library_handle, name.c_str()));
}
#else
void* library_handle;
template<typename T>
T load_function_impl(const std::string& name) {
  return reinterpret_cast<T>(dlsym(library_handle, name.c_str()));
}
#endif

template<typename T>
T load_function(const std::string& name) {
  auto result = load_function_impl<T>(name);
  if(!result) {
    std::cout << "Failed to load function " << name << std::endl;
    std::abort();
  }
  return result;
}

typedef int (*MainFunc)(int argc, char** argv);

int main(int argc, char * argv[]) {
#ifdef _WIN32
  std::string path(256, '\0');
  auto count = 0;
  do {
    path.resize(2*path.size());
    count = GetModuleFileNameA(nullptr, path.data(), path.size());
    if(count <= 0) {
      std::cout << "Failed to query path of executable" << std::endl;
      std::abort();
    }
  } while(count == path.size());
  path.resize(count);
  path = (std::filesystem::path(path).parent_path() /  "native_lib.dll").string();
  std::cout << "Loading " << path << std::endl;
  library_handle = LoadLibrary(path.c_str());
#else
  std::string path(256, '\0');
  auto count = 0;
  do {
    path.resize(2*path.size());
    count = readlink("/proc/self/exe", path.data(), path.size());
    if(count <= 0) {
      std::cout << "Failed to query path of executable" << std::endl;
      std::abort();
    }
  } while(count == path.size());
  path.resize(count);
  path = (std::filesystem::path(path).parent_path() /  "libnative_lib.so").string();
  std::cout << "Loading " << path << std::endl;
	library_handle = dlopen(path.c_str(), RTLD_NOW);
#endif

  COMMANDS["thalamus"] = load_function<MainFunc>("thalamus_main");
  COMMANDS["hydrate"] = load_function<MainFunc>("hydrate_main");

  auto command = COMMANDS.find(argc < 2 ? "thalamus" : argv[1]);
  if(command == COMMANDS.end()) { 
    std::cout << HELP;
    return 1;
  }

  auto arguments = std::vector<char*>(argv, argv + argc);
  if (argc >= 2) {
    arguments.erase(arguments.begin() + 1);
  }

  return command->second(arguments.size(), arguments.data());
}
