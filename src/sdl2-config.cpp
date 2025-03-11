#include <absl/strings/str_replace.h>
#include <algorithm>
#include <array>
#include <iostream>
#include <map>
#include <string>
#include <thalamus_config.h>
#include <vector>

int main(int argc, char **argv) {
  std::vector<std::string> args(argv, argv + argc);

  auto prefix = "";
  if (std::find(args.begin(), args.end(), "--cflags") != args.end()) {
    std::cout << prefix << "-I" << SDL_INCLUDE;
    prefix = " ";
  }
  if (std::find(args.begin(), args.end(), "--libs") != args.end()) {
    std::cout << prefix << SDL_LIB_FILES
              << " User32.lib Gdi32.lib Setupapi.lib Advapi32.lib Imm32.lib "
                 "Winmm.lib Shell32.lib Ole32.lib Oleaut32.lib Version.lib"
              << std::endl;
    prefix = " ";
  }
  std::cout << std::endl;

  return 0;
}
