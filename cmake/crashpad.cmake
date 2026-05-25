set(GCLIENT_CHECKOUT "${CMAKE_BINARY_DIR}/_deps-gclient")
set(CRASHPAD_SOURCE "${GCLIENT_CHECKOUT}/crashpad")

set(CRASHPAD_REVISION "e2c5e38691caccd792b1edb8d70680139e6a0b56")

file(WRITE "${GCLIENT_CHECKOUT}/.gclient"
"solutions = [
  {
    \"name\": \"crashpad\",
    \"url\": \"https://chromium.googlesource.com/crashpad/crashpad@${CRASHPAD_REVISION}\",
    \"managed\": True,
    \"custom_deps\": {},
  },
]
")

get_filename_component(CRASHPAD_CLANG_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
get_filename_component(CRASHPAD_CLANG_BASE_PATH "${CRASHPAD_CLANG_BIN_DIR}" DIRECTORY)
file(TO_CMAKE_PATH "${CRASHPAD_CLANG_BASE_PATH}" CRASHPAD_CLANG_BASE_PATH)

if(WIN32)
  execute_process(
    COMMAND powershell -Command "(New-Object -ComObject Scripting.FileSystemObject).GetFolder('${CRASHPAD_CLANG_BASE_PATH}').ShortPath"
    OUTPUT_VARIABLE CRASHPAD_CLANG_BASE_PATH_SHORT
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  message("CRASHPAD_CLANG_BASE_PATH_SHORT ${CRASHPAD_CLANG_BASE_PATH} ${CRASHPAD_CLANG_BASE_PATH_SHORT}")
endif()

set(CRASHPAD_OUT_DIR "${GCLIENT_CHECKOUT}/crashpad/out/${CMAKE_BUILD_TYPE}")
set(CRASHPAD_MIG_OUTPUT_LIB "${CRASHPAD_OUT_DIR}/obj/util/${CMAKE_STATIC_LIBRARY_PREFIX}mig_output${CMAKE_STATIC_LIBRARY_SUFFIX}")
set(CRASHPAD_CLIENT_LIB "${CRASHPAD_OUT_DIR}/obj/client/${CMAKE_STATIC_LIBRARY_PREFIX}client${CMAKE_STATIC_LIBRARY_SUFFIX}")
set(CRASHPAD_COMMON_LIB "${CRASHPAD_OUT_DIR}/obj/client/${CMAKE_STATIC_LIBRARY_PREFIX}common${CMAKE_STATIC_LIBRARY_SUFFIX}")
set(CRASHPAD_UTIL_LIB   "${CRASHPAD_OUT_DIR}/obj/util/${CMAKE_STATIC_LIBRARY_PREFIX}util${CMAKE_STATIC_LIBRARY_SUFFIX}")
set(CRASHPAD_BASE_LIB   "${CRASHPAD_OUT_DIR}/obj/third_party/mini_chromium/mini_chromium/base/${CMAKE_STATIC_LIBRARY_PREFIX}base${CMAKE_STATIC_LIBRARY_SUFFIX}")
set(CRASHPAD_HANDLER_EXE "${CRASHPAD_OUT_DIR}/crashpad_handler${CMAKE_EXECUTABLE_SUFFIX}")

set(CRASHPAD_LIBS "${CRASHPAD_CLIENT_LIB}" "${CRASHPAD_COMMON_LIB}" "${CRASHPAD_UTIL_LIB}" "${CRASHPAD_BASE_LIB}")
if(APPLE)
  list(APPEND CRASHPAD_LIBS "${CRASHPAD_MIG_OUTPUT_LIB}")
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(CRASHPAD_IS_DEBUG "true")
else()
  set(CRASHPAD_IS_DEBUG "false")
endif()

if(WIN32 AND CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(CRASHPAD_EXTRA_CFLAGS "/MTd /D_DEBUG /D_ITERATOR_DEBUG_LEVEL=2")
endif()

string(REPLACE ";" " " CRASHPAD_LIBCXX_CFLAGS  "${LIBCXX_COMPILE_OPTIONS}")
string(REPLACE ";" " " CRASHPAD_LIBCXX_LDFLAGS "${LIBCXX_LINK_OPTIONS}")

file(MAKE_DIRECTORY "${CRASHPAD_OUT_DIR}")
file(WRITE "${CRASHPAD_OUT_DIR}/args.gn"
"is_debug = ${CRASHPAD_IS_DEBUG}
clang_path = \"${CRASHPAD_CLANG_BASE_PATH_SHORT}\"
extra_cflags = \"${CRASHPAD_EXTRA_CFLAGS}\"
extra_cflags_cc = \"${CRASHPAD_LIBCXX_CFLAGS}\"
extra_ldflags = \"${CRASHPAD_LIBCXX_LDFLAGS}\"
")
if(APPLE)
 file(APPEND "${CRASHPAD_OUT_DIR}/args.gn"
"mac_deployment_target=\"${CMAKE_OSX_DEPLOYMENT_TARGET}\"
")
endif()

set(CRASHPAD_GCLIENT_STAMP "${GCLIENT_CHECKOUT}/gclient.stamp")
add_custom_command(
  OUTPUT "${CRASHPAD_GCLIENT_STAMP}"
  COMMAND gclient sync --no-history --nohooks
  COMMAND "${CMAKE_COMMAND}" -E touch "${CRASHPAD_GCLIENT_STAMP}"
  WORKING_DIRECTORY "${GCLIENT_CHECKOUT}")

add_custom_command(
  OUTPUT "${CRASHPAD_OUT_DIR}/build.ninja"
  DEPENDS "${CRASHPAD_GCLIENT_STAMP}" "${CRASHPAD_OUT_DIR}/args.gn"
  COMMAND gn gen "out/${CMAKE_BUILD_TYPE}"
  WORKING_DIRECTORY "${CRASHPAD_SOURCE}")

add_custom_command(
  OUTPUT ${CRASHPAD_LIBS} "${CRASHPAD_HANDLER_EXE}"
  DEPENDS "${CRASHPAD_OUT_DIR}/build.ninja"
  COMMAND ninja -C "${CRASHPAD_OUT_DIR}"
    "obj/client/${CMAKE_STATIC_LIBRARY_PREFIX}client${CMAKE_STATIC_LIBRARY_SUFFIX}"
    "obj/client/${CMAKE_STATIC_LIBRARY_PREFIX}common${CMAKE_STATIC_LIBRARY_SUFFIX}"
    "obj/util/${CMAKE_STATIC_LIBRARY_PREFIX}util${CMAKE_STATIC_LIBRARY_SUFFIX}"
    "obj/third_party/mini_chromium/mini_chromium/base/${CMAKE_STATIC_LIBRARY_PREFIX}base${CMAKE_STATIC_LIBRARY_SUFFIX}"
    "crashpad_handler${CMAKE_EXECUTABLE_SUFFIX}"
  WORKING_DIRECTORY "${CRASHPAD_SOURCE}")

add_library(crashpad INTERFACE
  "${CRASHPAD_LIBS}"
  "${CRASHPAD_HANDLER_EXE}")
target_link_libraries(crashpad INTERFACE
  "${CRASHPAD_LIBS}")
if(APPLE)
  target_link_libraries(crashpad INTERFACE bsm)
endif()

target_include_directories(crashpad INTERFACE
  "${CRASHPAD_SOURCE}"
  "${CRASHPAD_SOURCE}/third_party/mini_chromium/mini_chromium")
add_custom_target(build-crashpad DEPENDS crashpad)
