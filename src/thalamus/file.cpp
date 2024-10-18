#include <thalamus/file.h>
#include <util.hpp>
#include <fstream>

namespace thalamus {

  std::filesystem::path get_home() {
#ifdef _WIN32
    char* buffer;
    size_t buffer_size;
    auto status = _dupenv_s(&buffer, &buffer_size, "USERPROFILE");
    THALAMUS_ASSERT(status == 0, "Failed to read USERPROFILE, status == %d", status);
    THALAMUS_ASSERT(buffer != nullptr, "USERPROFILE is undefined");
    THALAMUS_LOG(info) << buffer << " " << strlen(buffer) << " " << buffer_size << std::endl;
    std::filesystem::path result(buffer);
    free(buffer);
    return result;
#else
    return std::filesystem::path(std::string(getenv("HOME")));
#endif
  }

  bool can_write_file(const std::filesystem::path& path) {
    std::ofstream stream(path.string());
    stream << "WROTE" << std::endl;
    stream.close();
    return std::filesystem::exists(path);
  }
}
