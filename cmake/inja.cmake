FetchContent_Declare(
  inja
  URL https://github.com/pantor/inja/archive/refs/tags/v3.4.0.tar.gz
  SOURCE_SUBDIR thalamus-nonexistant)
FetchContent_MakeAvailable(inja)

add_library(inja INTERFACE)
target_include_directories(inja INTERFACE "${inja_SOURCE_DIR}/include" "${inja_SOURCE_DIR}/third_party/include")
