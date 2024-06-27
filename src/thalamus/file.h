#include <filesystem>

namespace thalamus {

  std::filesystem::path get_home();
  bool can_write_file(const std::filesystem::path& path);
}
