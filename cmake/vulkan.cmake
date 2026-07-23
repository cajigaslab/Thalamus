find_package(Vulkan)
if(Vulkan_FOUND)
  message("Vulkan SDK found")
  return()
endif()
message("Vulkan SDK not found, will build from source")

if(CMAKE_VERSION VERSION_LESS "3.19")
  message(FATAL_ERROR "vulkan.cmake requires CMake 3.19+ for JSON support")
endif()

# --- Parse LunarG SDK config.json for component URLs and tags ---
set(LUNARG_CONFIG_URL "https://sdk.lunarg.com/sdk/download/1.4.350.0/windows/config.json")
set(LUNARG_CONFIG_FILE "${CMAKE_BINARY_DIR}/lunarg_config.json")

if(NOT EXISTS "${LUNARG_CONFIG_FILE}")
  message(STATUS "Downloading LunarG SDK config.json...")
  file(DOWNLOAD "${LUNARG_CONFIG_URL}" "${LUNARG_CONFIG_FILE}"
       STATUS LUNARG_DL_STATUS TLS_VERIFY ON)
  list(GET LUNARG_DL_STATUS 0 LUNARG_DL_RESULT)
  if(NOT LUNARG_DL_RESULT EQUAL 0)
    file(REMOVE "${LUNARG_CONFIG_FILE}")
    message(FATAL_ERROR "Failed to download LunarG config.json: ${LUNARG_DL_STATUS}")
  endif()
endif()

file(READ "${LUNARG_CONFIG_FILE}" LUNARG_CONFIG_JSON)

string(JSON VULKAN_HEADERS_URL   GET "${LUNARG_CONFIG_JSON}" repos Vulkan-Headers url)
string(JSON VULKAN_HEADERS_TAG   GET "${LUNARG_CONFIG_JSON}" repos Vulkan-Headers tag)
string(JSON VULKAN_LOADER_URL    GET "${LUNARG_CONFIG_JSON}" repos Vulkan-Loader url)
string(JSON VULKAN_LOADER_TAG    GET "${LUNARG_CONFIG_JSON}" repos Vulkan-Loader tag)
string(JSON VULKAN_VALLAYERS_URL GET "${LUNARG_CONFIG_JSON}" repos Vulkan-ValidationLayers url)
string(JSON VULKAN_VALLAYERS_TAG GET "${LUNARG_CONFIG_JSON}" repos Vulkan-ValidationLayers tag)

message(STATUS "Vulkan-Headers:          ${VULKAN_HEADERS_URL} @ ${VULKAN_HEADERS_TAG}")
message(STATUS "Vulkan-Loader:           ${VULKAN_LOADER_URL} @ ${VULKAN_LOADER_TAG}")
message(STATUS "Vulkan-ValidationLayers: ${VULKAN_VALLAYERS_URL} @ ${VULKAN_VALLAYERS_TAG}")

# --- Download glslang main-tot release during configuration ---
if(WIN32)
  set(GLSLANG_ASSET "glslang-master-windows-Release.zip")
elseif(APPLE)
  set(GLSLANG_ASSET "glslang-main-osx-Release.zip")
else()
  set(GLSLANG_ASSET "glslang-main-linux-Release.zip")
endif()

set(GLSLANG_DOWNLOAD_PATH "${CMAKE_BINARY_DIR}/${GLSLANG_ASSET}")
set(GLSLANG_URL "https://github.com/KhronosGroup/glslang/releases/download/main-tot/${GLSLANG_ASSET}")

set(GLSLANG_EXTRACT_DIR "${CMAKE_BINARY_DIR}/glslang")
if(WIN32)
  set(Vulkan_GLSLANG_VALIDATOR_EXECUTABLE "${GLSLANG_EXTRACT_DIR}/bin/glslangValidator.exe")
else()
  set(Vulkan_GLSLANG_VALIDATOR_EXECUTABLE "${GLSLANG_EXTRACT_DIR}/bin/glslangValidator")
endif()

if(NOT EXISTS "${GLSLANG_DOWNLOAD_PATH}")
  message(STATUS "Downloading glslang main-tot: ${GLSLANG_URL}")
  file(DOWNLOAD "${GLSLANG_URL}" "${GLSLANG_DOWNLOAD_PATH}"
       STATUS GLSLANG_DL_STATUS SHOW_PROGRESS TLS_VERIFY ON)
  list(GET GLSLANG_DL_STATUS 0 GLSLANG_DL_RESULT)
  if(NOT GLSLANG_DL_RESULT EQUAL 0)
    file(REMOVE "${GLSLANG_DOWNLOAD_PATH}")
    message(WARNING "Failed to download glslang: ${GLSLANG_DL_STATUS}")
  endif()
endif()

if(EXISTS "${GLSLANG_DOWNLOAD_PATH}" AND NOT EXISTS "${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE}")
  message(STATUS "Extracting glslang to ${GLSLANG_EXTRACT_DIR}")
  file(ARCHIVE_EXTRACT INPUT "${GLSLANG_DOWNLOAD_PATH}" DESTINATION "${GLSLANG_EXTRACT_DIR}")
endif()

if(EXISTS "${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE}")
  message(STATUS "Vulkan_GLSLANG_VALIDATOR_EXECUTABLE : ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE}")
else()
  message(WARNING "glslc compiler not found at ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE}")
endif()

# --- Vulkan-Headers ---
FetchContent_Declare(
  vulkan_headers
  GIT_REPOSITORY "${VULKAN_HEADERS_URL}"
  GIT_TAG        "${VULKAN_HEADERS_TAG}"
  SOURCE_SUBDIR  thalamus-nonexistant)
FetchContent_MakeAvailable(vulkan_headers)

file(MAKE_DIRECTORY "${vulkan_headers_BINARY_DIR}/Debug")
file(MAKE_DIRECTORY "${vulkan_headers_BINARY_DIR}/Release")

set(VULKAN_HEADERS_INSTALL_MARKER
    "${vulkan_headers_BINARY_DIR}/$<CONFIG>/install/include/vulkan/vulkan.h")

add_custom_command(
  OUTPUT "${vulkan_headers_BINARY_DIR}/$<CONFIG>/CMakeCache.txt"
  COMMAND
    cmake "${vulkan_headers_SOURCE_DIR}"
      -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
      -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
      "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
      -DCMAKE_LINKER=${CMAKE_LINKER}
      "-DCMAKE_BUILD_TYPE=$<CONFIG>"
      "-DCMAKE_INSTALL_PREFIX=${vulkan_headers_BINARY_DIR}/$<CONFIG>/install"
      -DCMAKE_MSVC_RUNTIME_LIBRARY=${CMAKE_MSVC_RUNTIME_LIBRARY}
      -G "${CMAKE_GENERATOR}"
    && cmake -E touch_nocreate "${vulkan_headers_BINARY_DIR}/$<CONFIG>/CMakeCache.txt"
  WORKING_DIRECTORY "${vulkan_headers_BINARY_DIR}/$<CONFIG>")

add_custom_command(
  OUTPUT "${VULKAN_HEADERS_INSTALL_MARKER}"
  DEPENDS "${vulkan_headers_BINARY_DIR}/$<CONFIG>/CMakeCache.txt"
  COMMAND
    cmake --build . --config "$<CONFIG>" --parallel ${CPU_COUNT}
    && cmake --install . --config "$<CONFIG>"
    && cmake -E touch_nocreate "${VULKAN_HEADERS_INSTALL_MARKER}"
  WORKING_DIRECTORY "${vulkan_headers_BINARY_DIR}/$<CONFIG>")

add_library(vulkan-headers INTERFACE "${VULKAN_HEADERS_INSTALL_MARKER}")
target_include_directories(vulkan-headers INTERFACE
  "${vulkan_headers_BINARY_DIR}/$<CONFIG>/install/include")

# --- Vulkan-Loader ---
FetchContent_Declare(
  vulkan_loader
  GIT_REPOSITORY "${VULKAN_LOADER_URL}"
  GIT_TAG        "${VULKAN_LOADER_TAG}"
  SOURCE_SUBDIR  thalamus-nonexistant)
FetchContent_MakeAvailable(vulkan_loader)

file(MAKE_DIRECTORY "${vulkan_loader_BINARY_DIR}/Debug")
file(MAKE_DIRECTORY "${vulkan_loader_BINARY_DIR}/Release")

if(WIN32)
  set(VULKAN_LOADER_LIB "${vulkan_loader_BINARY_DIR}/$<CONFIG>/install/lib/vulkan-1.lib")
elseif(APPLE)
  set(VULKAN_LOADER_LIB "${vulkan_loader_BINARY_DIR}/$<CONFIG>/install/lib/libvulkan.1.dylib")
else()
  set(VULKAN_LOADER_LIB "${vulkan_loader_BINARY_DIR}/$<CONFIG>/install/lib/libvulkan.so.1")
endif()

add_custom_command(
  OUTPUT "${vulkan_loader_BINARY_DIR}/$<CONFIG>/CMakeCache.txt"
  DEPENDS "${VULKAN_HEADERS_INSTALL_MARKER}"
  COMMAND
    cmake "${vulkan_loader_SOURCE_DIR}"
      -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
      -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
      "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
      -DCMAKE_LINKER=${CMAKE_LINKER}
      "-DCMAKE_BUILD_TYPE=$<CONFIG>"
      "-DCMAKE_INSTALL_PREFIX=${vulkan_loader_BINARY_DIR}/$<CONFIG>/install"
      "-DVULKAN_HEADERS_INSTALL_DIR=${vulkan_headers_BINARY_DIR}/$<CONFIG>/install"
      -DBUILD_TESTS=OFF
      -DCMAKE_MSVC_RUNTIME_LIBRARY=${CMAKE_MSVC_RUNTIME_LIBRARY}
      -DCMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}
      -G "${CMAKE_GENERATOR}"
    && cmake -E touch_nocreate "${vulkan_loader_BINARY_DIR}/$<CONFIG>/CMakeCache.txt"
  WORKING_DIRECTORY "${vulkan_loader_BINARY_DIR}/$<CONFIG>")

add_custom_command(
  OUTPUT "${VULKAN_LOADER_LIB}"
  DEPENDS "${vulkan_loader_BINARY_DIR}/$<CONFIG>/CMakeCache.txt"
  COMMAND
    cmake --build . --config "$<CONFIG>" --parallel ${CPU_COUNT}
    && cmake --install . --config "$<CONFIG>"
    && cmake -E touch_nocreate "${VULKAN_LOADER_LIB}"
  WORKING_DIRECTORY "${vulkan_loader_BINARY_DIR}/$<CONFIG>")

add_library(vulkan-loader INTERFACE "${VULKAN_LOADER_LIB}")
target_link_libraries(vulkan-loader INTERFACE "${VULKAN_LOADER_LIB}" vulkan-headers)

add_library(vulkan INTERFACE)
target_link_libraries(vulkan INTERFACE vulkan-headers vulkan-loader)
if(APPLE)
  target_link_libraries(vulkan INTERFACE "-framework QuartzCore")
endif()
add_library(Vulkan::Vulkan ALIAS vulkan)
## --- Vulkan-ValidationLayers ---
#FetchContent_Declare(
#  vulkan_validationlayers
#  GIT_REPOSITORY "${VULKAN_VALLAYERS_URL}"
#  GIT_TAG        "${VULKAN_VALLAYERS_TAG}"
#  SOURCE_SUBDIR  thalamus-nonexistant)
#FetchContent_MakeAvailable(vulkan_validationlayers)
#
#file(MAKE_DIRECTORY "${vulkan_validationlayers_BINARY_DIR}/Debug")
#file(MAKE_DIRECTORY "${vulkan_validationlayers_BINARY_DIR}/Release")
#
#if(WIN32)
#  set(VULKAN_VALIDATION_MARKER
#      "${vulkan_validationlayers_BINARY_DIR}/$<CONFIG>/install/bin/VkLayer_khronos_validation.dll")
#else()
#  set(VULKAN_VALIDATION_MARKER
#      "${vulkan_validationlayers_BINARY_DIR}/$<CONFIG>/install/lib/libVkLayer_khronos_validation.so")
#endif()
#
#add_custom_command(
#  OUTPUT "${vulkan_validationlayers_BINARY_DIR}/$<CONFIG>/CMakeCache.txt"
#  DEPENDS "${VULKAN_HEADERS_INSTALL_MARKER}"
#  COMMAND
#    cmake "${vulkan_validationlayers_SOURCE_DIR}"
#      -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
#      -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
#      "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
#      -DCMAKE_LINKER=${CMAKE_LINKER}
#      "-DCMAKE_BUILD_TYPE=$<CONFIG>"
#      "-DCMAKE_INSTALL_PREFIX=${vulkan_validationlayers_BINARY_DIR}/$<CONFIG>/install"
#      "-DVULKAN_HEADERS_INSTALL_DIR=${vulkan_headers_BINARY_DIR}/$<CONFIG>/install"
#      -DBUILD_TESTS=OFF
#      -DBUILD_WERROR=OFF
#      -DUPDATE_DEPS=ON
#      -DCMAKE_MSVC_RUNTIME_LIBRARY=${CMAKE_MSVC_RUNTIME_LIBRARY}
#      -G "${CMAKE_GENERATOR}"
#    && cmake -E touch_nocreate "${vulkan_validationlayers_BINARY_DIR}/$<CONFIG>/CMakeCache.txt"
#  WORKING_DIRECTORY "${vulkan_validationlayers_BINARY_DIR}/$<CONFIG>")
#
#add_custom_command(
#  OUTPUT "${VULKAN_VALIDATION_MARKER}"
#  DEPENDS "${vulkan_validationlayers_BINARY_DIR}/$<CONFIG>/CMakeCache.txt"
#  COMMAND
#    cmake --build . --config "$<CONFIG>" --parallel ${CPU_COUNT}
#    && cmake --install . --config "$<CONFIG>"
#    && cmake -E touch_nocreate "${VULKAN_VALIDATION_MARKER}"
#  WORKING_DIRECTORY "${vulkan_validationlayers_BINARY_DIR}/$<CONFIG>")
#
#add_library(vulkan-validationlayers INTERFACE "${VULKAN_VALIDATION_MARKER}")
#target_link_libraries(vulkan-validationlayers INTERFACE vulkan-headers)
