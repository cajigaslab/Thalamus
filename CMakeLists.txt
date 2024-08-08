cmake_minimum_required(VERSION 3.16)
cmake_policy(SET CMP0042 NEW)
cmake_policy(SET CMP0071 NEW)
cmake_policy(SET CMP0077 NEW)
cmake_policy(SET CMP0091 NEW)
if(${CMAKE_VERSION} VERSION_GREATER_EQUAL "3.24")
  cmake_policy(SET CMP0135 NEW)
endif()
set(CMAKE_POLICY_DEFAULT_CMP0042 NEW)
set(CMAKE_POLICY_DEFAULT_CMP0063 NEW)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

project(thalamus)

message("CMAKE_SYSTEM_PROCESSOR ${CMAKE_SYSTEM_PROCESSOR}")

if(WIN32)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    execute_process(COMMAND "${CMAKE_CXX_COMPILER}" "${CMAKE_SOURCE_DIR}/stub_atomic.cpp" -c -o "${CMAKE_BINARY_DIR}/atomic.lib")
  endif()
endif()

if(APPLE)
  set(OSX_TARGET_PARAMETER -mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET})
endif()

# Default to C99
if(NOT CMAKE_C_STANDARD) 
  set(CMAKE_C_STANDARD 99)
endif()

# Default to C++17
set(CMAKE_CXX_STANDARD 20)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(ABSL_PROPAGATE_CXX_STD ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(FetchContent)

include(ProcessorCount)
ProcessorCount(CPU_COUNT)
if(CPU_COUNT EQUAL 0)
  set(MAKE_J)
  set(CMAKE_PARALLEL)
else()
  if(WIN32)
    set(MAKE_J /J ${CPU_COUNT})
  else()
    set(MAKE_J -j ${CPU_COUNT})
  endif()
  set(CMAKE_PARALLEL --parallel ${CPU_COUNT})
endif()

if("${CMAKE_CXX_COMPILER_ID}" MATCHES "MSVC")
  list(APPEND ALL_COMPILE_OPTIONS /Zi /MP)
  list(APPEND ALL_LINK_OPTIONS /DEBUG)
else()
  list(APPEND ALL_COMPILE_OPTIONS -g -fPIC)
endif()
  
if("${SANITIZER}" STREQUAL thread)
  list(APPEND ALL_COMPILE_OPTIONS -fsanitize=thread -O1)
  list(APPEND ALL_LINK_OPTIONS -fsanitize=thread -O1)
elseif("${SANITIZER}" STREQUAL address)
  if("${CMAKE_CXX_COMPILER_ID}" MATCHES "MSVC")
    list(APPEND ALL_COMPILE_OPTIONS -fsanitize=address)
  else()
    list(APPEND ALL_COMPILE_OPTIONS -fsanitize=address -O1 -fno-omit-frame-pointer -fno-optimize-sibling-calls)
    list(APPEND ALL_LINK_OPTIONS -fsanitize=address -O1 -fno-omit-frame-pointer -fno-optimize-sibling-calls)
  endif()
elseif("${SANITIZER}" STREQUAL memory)
  list(APPEND ALL_COMPILE_OPTIONS -fsanitize=memory -fsanitize-memory-track-origins=2 -O1 -fno-omit-frame-pointer -fno-optimize-sibling-calls)
  list(APPEND ALL_LINK_OPTIONS -fsanitize=memory -fsanitize-memory-track-origins=2 -O1 -fno-omit-frame-pointer -fno-optimize-sibling-calls)
endif()

string(REPLACE ";" " " ALL_C_COMPILE_OPTIONS_SPACED "${ALL_COMPILE_OPTIONS}")
string(REPLACE ";" " " ALL_C_LINK_OPTIONS_SPACED "${ALL_LINK_OPTIONS}")

if(NOT WIN32 AND NOT APPLE)
  include(cmake/comedi.cmake)
endif()
include(cmake/sdl.cmake)
include(cmake/ffmpeg.cmake)
include(cmake/glib.cmake)
if(WITH_CAIRO)
  include(cmake/cairo.cmake)
endif()
include(cmake/luajit.cmake)
 
if(NOT WIN32 AND NOT APPLE)
  include(cmake/llvm.cmake)
  list(APPEND ALL_COMPILE_OPTIONS ${LIBCXX_COMPILE_OPTIONS})
  list(APPEND ALL_LINK_OPTIONS ${LIBCXX_LINK_OPTIONS})
endif()

add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:${ALL_COMPILE_OPTIONS}>")
add_link_options("$<$<COMPILE_LANGUAGE:CXX>:${ALL_LINK_OPTIONS}>")
string(REPLACE ";" " " ALL_COMPILE_OPTIONS_SPACED "${ALL_COMPILE_OPTIONS}")
string(REPLACE ";" " " ALL_LINK_OPTIONS_SPACED "${ALL_LINK_OPTIONS}")

add_definitions(-DPRINTSYNC)

include(cmake/grpc.cmake)
include(cmake/zlib_processed.cmake)
include(cmake/opencv.cmake)
include(cmake/boost.cmake)
include(cmake/kfr.cmake)
include(cmake/hdf5.cmake)

execute_process(
  COMMAND git log -1 --format=%h
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE GIT_COMMIT_HASH
  OUTPUT_STRIP_TRAILING_WHITESPACE)

configure_file(thalamus_config.h.in thalamus_config.h)
 
set(PROTO_SOURCES
  proto/util.proto
  proto/task_controller.proto
  proto/ophanim.proto
  proto/thalamus.proto)
apply_protoc_grpc(PROTO_GENERATED_SOURCES ${PROTO_SOURCES})

add_library(protoc_generated OBJECT ${PROTO_GENERATED_SOURCES} ${PROTO_SOURCES})
target_link_libraries(protoc_generated grpc++ grpc++_reflection)

message("PROTO_SOURCES ${PROTO_SOURCES}")
message("PROTO_GENERATED_SOURCES ${PROTO_GENERATED_SOURCES}")

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  set(WARNING_FLAGS -Wall -Wextra -Wpedantic -Wno-deprecated-declarations -Wno-c11-extensions -Wno-gcc-compat)
else()
  #set(WARNING_FLAGS /Wall)
  set(WARNING_FLAGS)
endif()

include_directories(${CMAKE_BINARY_DIR})

if("${CMAKE_CXX_COMPILER_ID}" MATCHES "MSVC")
  set_target_properties(boost PROPERTIES VS_GLOBAL_IntDir "boost_intermediate/x64/$(Configuration)/")
  #set_target_properties(yaml PROPERTIES VS_GLOBAL_IntDir "yaml_intermediate/x64/$(Configuration)/")
  #set_target_properties(qt PROPERTIES VS_GLOBAL_IntDir "qt_intermediate/x64/$(Configuration)/")
  set_target_properties(opencv PROPERTIES VS_GLOBAL_IntDir "opencv_intermediate/x64/$(Configuration)/")
  set_target_properties(zlib_processed PROPERTIES VS_GLOBAL_IntDir "zlib_processed_intermediate/x64/$(Configuration)/")
  #set_target_properties(prometheus PROPERTIES VS_GLOBAL_IntDir "prometheus_intermediate/x64/$(Configuration)/")
  set_target_properties(ffmpeg PROPERTIES VS_GLOBAL_IntDir "ffmpeg_intermediate/x64/$(Configuration)/")
endif()

FetchContent_Declare(
  webrtc_content
  GIT_REPOSITORY https://webrtc.googlesource.com/src
  GIT_TAG 7b80d4480e85cec2ca6010a4912333bf3aa30a50)
FetchContent_Populate(webrtc_content)
add_library(webrtc INTERFACE)
target_link_libraries(webrtc INTERFACE absl::strings absl::str_format)
target_include_directories(webrtc INTERFACE "${webrtc_content_SOURCE_DIR}")
if(WIN32)
  target_compile_definitions(webrtc INTERFACE WEBRTC_WIN NOMINMAX WIN32_LEAN_AND_MEAN)
elseif(APPLE)
  target_compile_definitions(webrtc INTERFACE WEBRTC_POSIX WEBRTC_MAC)
else()
  target_compile_definitions(webrtc INTERFACE WEBRTC_POSIX WEBRTC_LINUX)
endif()

set(WEBRTC_FILES 
  "${webrtc_content_SOURCE_DIR}/rtc_base/event.cc"
  "${webrtc_content_SOURCE_DIR}/rtc_base/checks.cc"
  "${webrtc_content_SOURCE_DIR}/rtc_base/logging.cc"
  "${webrtc_content_SOURCE_DIR}/rtc_base/time_utils.cc"
  "${webrtc_content_SOURCE_DIR}/rtc_base/task_queue.cc"
  "${webrtc_content_SOURCE_DIR}/rtc_base/system_time.cc"
  "${webrtc_content_SOURCE_DIR}/rtc_base/string_utils.cc"
  "${webrtc_content_SOURCE_DIR}/rtc_base/event_tracer.cc"
  "${webrtc_content_SOURCE_DIR}/rtc_base/string_encode.cc"
  "${webrtc_content_SOURCE_DIR}/rtc_base/platform_thread.cc"
  "${webrtc_content_SOURCE_DIR}/rtc_base/platform_thread_types.cc"
  "${webrtc_content_SOURCE_DIR}/rtc_base/strings/string_builder.cc"
  "${webrtc_content_SOURCE_DIR}/rtc_base/synchronization/yield_policy.cc"
  "${webrtc_content_SOURCE_DIR}/rtc_base/synchronization/sequence_checker_internal.cc"
  "${webrtc_content_SOURCE_DIR}/api/task_queue/task_queue_base.cc")

add_library(tracing OBJECT
  "${CMAKE_SOURCE_DIR}/src/tracing/tracing.h"
  "${CMAKE_SOURCE_DIR}/src/tracing/tracing.cpp"
  "${CMAKE_SOURCE_DIR}/src/tracing/iclock.h"
  "${CMAKE_SOURCE_DIR}/src/tracing/systemclock.h"
  "${CMAKE_SOURCE_DIR}/src/tracing/systemclock.cpp"
  ${WEBRTC_FILES})
target_link_libraries(tracing PUBLIC webrtc)
if(WIN32)
  target_link_libraries(tracing PUBLIC winmm.lib)
elseif(APPLE)
else()
  target_link_libraries(tracing PUBLIC stdc++fs)
  #target_compile_options(tracing PRIVATE -fPIC)
endif()
target_include_directories(tracing PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_compile_options(tracing PRIVATE ${WARNING_FLAGS})

add_library(hydrate "src/hydrate.cpp")
target_link_libraries(hydrate boost grpc++ hdf5-static)
target_compile_definitions(hydrate PRIVATE _USE_MATH_DEFINES NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS _GNU_SOURCE)
target_include_directories(hydrate PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_compile_options(hydrate PRIVATE ${WARNING_FLAGS})
  
add_library(thalamus "${CMAKE_SOURCE_DIR}/src/thalamus.cpp"
                     "${CMAKE_SOURCE_DIR}/src/node_graph_impl.h"
                     "${CMAKE_SOURCE_DIR}/src/node_graph_impl.cpp"
                     "${CMAKE_SOURCE_DIR}/src/state.h"
                     "${CMAKE_SOURCE_DIR}/src/state.cpp"
                     "${CMAKE_SOURCE_DIR}/src/base_node.h"
                     "${CMAKE_SOURCE_DIR}/src/base_node.cpp"
                     "${CMAKE_SOURCE_DIR}/src/alpha_omega_node.h"
                     "${CMAKE_SOURCE_DIR}/src/alpha_omega_node.cpp"
                     "${CMAKE_SOURCE_DIR}/src/grpc_impl.h"
                     "${CMAKE_SOURCE_DIR}/src/grpc_impl.cpp"
                     "${CMAKE_SOURCE_DIR}/src/util.h"
                     "${CMAKE_SOURCE_DIR}/src/util.cpp"
                     "${CMAKE_SOURCE_DIR}/src/h5handle.h"
                     "${CMAKE_SOURCE_DIR}/src/h5handle.cpp"
                     "${CMAKE_SOURCE_DIR}/src/xsens_node.h"
                     "${CMAKE_SOURCE_DIR}/src/xsens_node.cpp"
                     "${CMAKE_SOURCE_DIR}/src/storage_node.h"
                     "${CMAKE_SOURCE_DIR}/src/storage_node.cpp"
                     "${CMAKE_SOURCE_DIR}/src/run_node.h"
                     "${CMAKE_SOURCE_DIR}/src/run_node.cpp"
                     "${CMAKE_SOURCE_DIR}/src/remote_node.h"
                     "${CMAKE_SOURCE_DIR}/src/remote_node.cpp"
                     "${CMAKE_SOURCE_DIR}/src/ophanim_node.h"
                     "${CMAKE_SOURCE_DIR}/src/ophanim_node.cpp"
                     "${CMAKE_SOURCE_DIR}/src/task_controller_node.h"
                     "${CMAKE_SOURCE_DIR}/src/task_controller_node.cpp"
                     "${CMAKE_SOURCE_DIR}/src/image_node.h"
                     "${CMAKE_SOURCE_DIR}/src/image_node.cpp"
                     "${CMAKE_SOURCE_DIR}/src/oculomatic_node.h"
                     "${CMAKE_SOURCE_DIR}/src/oculomatic_node.cpp"
                     "${CMAKE_SOURCE_DIR}/src/distortion_node.h"
                     "${CMAKE_SOURCE_DIR}/src/distortion_node.cpp"
                     "${CMAKE_SOURCE_DIR}/src/text_node.h"
                     "${CMAKE_SOURCE_DIR}/src/log_node.h"
                     "${CMAKE_SOURCE_DIR}/src/log_node.cpp"
                     "${CMAKE_SOURCE_DIR}/src/thread_pool.h"
                     "${CMAKE_SOURCE_DIR}/src/thread_pool.cpp"
                     "${CMAKE_SOURCE_DIR}/src/nidaq_node.h"
                     "${CMAKE_SOURCE_DIR}/src/gentl.h"
                     "${CMAKE_SOURCE_DIR}/src/genicam_node.h"
                     "${CMAKE_SOURCE_DIR}/src/genicam_node.cpp"
                     "${CMAKE_SOURCE_DIR}/src/channel_picker_node.h"
                     "${CMAKE_SOURCE_DIR}/src/channel_picker_node.cpp"
                     "${CMAKE_SOURCE_DIR}/src/algebra_node.h"
                     "${CMAKE_SOURCE_DIR}/src/algebra_node.cpp"
                     "${CMAKE_SOURCE_DIR}/src/normalize_node.h"
                     "${CMAKE_SOURCE_DIR}/src/normalize_node.cpp"
                     "${CMAKE_SOURCE_DIR}/src/lua_node.h"
                     "${CMAKE_SOURCE_DIR}/src/lua_node.cpp"
                     "${CMAKE_SOURCE_DIR}/src/intan_node.h"
                     "${CMAKE_SOURCE_DIR}/src/intan_node.cpp"
                     "${CMAKE_SOURCE_DIR}/src/aruco_node.h"
                     "${CMAKE_SOURCE_DIR}/src/aruco_node.cpp"
                     "${CMAKE_SOURCE_DIR}/src/spikeglx_node.h"
                     "${CMAKE_SOURCE_DIR}/src/spikeglx_node.cpp"
                     "${CMAKE_SOURCE_DIR}/src/thalamus/file.h"
                     "${CMAKE_SOURCE_DIR}/src/thalamus/file.cpp"
                     "${CMAKE_SOURCE_DIR}/proto/thalamus.proto")
if(WITH_CAIRO)
  target_sources(thalamus PRIVATE 
                 "${CMAKE_SOURCE_DIR}/src/pupil_node.h"
                 "${CMAKE_SOURCE_DIR}/src/pupil_node.cpp"
                 "${CMAKE_SOURCE_DIR}/src/chessboard_node.h"
                 "${CMAKE_SOURCE_DIR}/src/chessboard_node.cpp")
endif()

if(WIN32)
  target_compile_definitions(thalamus PRIVATE _WIN32_WINNT=0x0A00 _CRT_SECURE_NO_WARNINGS)
  if("${CMAKE_CXX_COMPILER_ID}" MATCHES "MSVC")
    target_compile_options(thalamus PRIVATE /bigobj)
  endif()
  target_sources(thalamus PRIVATE "${CMAKE_SOURCE_DIR}/src/nidaq_node_windows.cpp")
elseif(APPLE)
  target_sources(thalamus PRIVATE "${CMAKE_SOURCE_DIR}/src/nidaq_node_mac.cpp")
  target_compile_definitions(thalamus PRIVATE _GNU_SOURCE)
else()
  target_sources(thalamus PRIVATE "${CMAKE_SOURCE_DIR}/src/nidaq_node_linux.cpp"
                                  "${CMAKE_SOURCE_DIR}/src/ros2_node.h"
                                  "${CMAKE_SOURCE_DIR}/src/ros2_node.cpp")
  target_link_libraries(thalamus comedi m)
endif()
target_compile_options(thalamus PRIVATE ${WARNING_FLAGS})

target_compile_definitions(thalamus PRIVATE _USE_MATH_DEFINES)
target_include_directories(thalamus PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(thalamus opencv hdf5-static lua kfr
  tracing webrtc boost grpc++ grpc++_reflection 
  absl::str_format_internal absl::strings absl::str_format ffmpeg)
if(NOT APPLE)
  target_link_libraries(thalamus zlib_processed)
endif()

add_library(native_lib SHARED src/plugin.cpp)
target_compile_options(native_lib PRIVATE ${WARNING_FLAGS})
target_include_directories(native_lib PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(native_lib 
  thalamus 
  hydrate
  thalamus_ffmpeg
  tracing webrtc ffmpeg sdl protoc_generated kfr lua)
if(NOT WIN32 AND NOT APPLE)
  target_link_options(native_lib PRIVATE -Wl,-Bsymbolic)
endif()
if(WITH_CAIRO)
  target_link_libraries(thalamus cairo)
  target_link_libraries(native_lib cairo)
endif()

if(WIN32)
  target_compile_definitions(native_lib PRIVATE _WIN32_WINNT=0x0A00)
endif()
target_compile_definitions(native_lib PRIVATE _GNU_SOURCE)

add_executable(native src/main.cpp)
target_compile_options(native PRIVATE ${WARNING_FLAGS})
if(WIN32)
else()
  target_link_libraries(native PRIVATE dl)
endif()

if(APPLE)
  target_link_options(native PRIVATE -pagezero_size 10000)
  target_link_options(native_lib PRIVATE -image_base 7fff04c4a000)
endif()

if(ENABLE_SWIG)
  add_custom_command(TARGET native POST_BUILD COMMAND cmake -E copy "$<TARGET_FILE:native>" "${FINAL_DIRECTORY}" 
                                                        && cmake -E copy "$<TARGET_FILE:native>" "${CMAKE_SOURCE_DIR}/thalamus")
else()
  add_custom_command(TARGET native POST_BUILD COMMAND cmake -E copy "$<TARGET_FILE:native>" "${CMAKE_SOURCE_DIR}/thalamus")
  add_custom_command(TARGET native_lib POST_BUILD COMMAND cmake -E copy "$<TARGET_FILE:native_lib>" "${CMAKE_SOURCE_DIR}/thalamus")
endif()

