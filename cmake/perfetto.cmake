FetchContent_Declare(
  perfetto
  GIT_REPOSITORY https://android.googlesource.com/platform/external/perfetto
  GIT_TAG        v49.0
)
FetchContent_MakeAvailable(perfetto)

add_library(perfetto STATIC ${perfetto_SOURCE_DIR}/sdk/perfetto.cc)
target_include_directories(perfetto PUBLIC ${perfetto_SOURCE_DIR}/sdk)
if(WIN32)
  target_compile_definitions(perfetto PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(perfetto PRIVATE /bigobj)
endif()
