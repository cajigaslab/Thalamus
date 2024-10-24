#include <string>
#include <vector>
#include <map>
#include <iostream>
#include <functional>
#include <thalamus_config.h>

const auto HELP = 
"Thalamus native program, version " GIT_COMMIT_HASH "\n"
"  thalamus         Signal tool\n"
"  hydrate          Thalamus capture parsing\n"
"  ffmpeg           ffmpeg\n"
"  ffprobe          ffprobe\n"
"  ffplay           ffplay\n"
;


namespace thalamus { extern int main(int argc, char ** argv); }
namespace hydrate { extern int main(int argc, char ** argv); }
extern "C" {
  int ffmpeg_main_impl(int argc, char** argv);
  int ffplay_main_impl(int argc, char** argv);
  int ffprobe_main_impl(int argc, char** argv);
}

std::map<std::string, std::function<int(int, char**)>> COMMANDS = {
  {"thalamus", thalamus::main},
  {"hydrate", hydrate::main},
  {"ffmpeg", ffmpeg_main_impl},
  {"ffprobe", ffprobe_main_impl},
  {"ffplay", ffplay_main_impl}
};

int main(int argc, char * argv[]) {
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
