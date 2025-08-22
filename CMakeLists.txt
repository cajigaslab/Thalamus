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

option(CODE_COVERAGE "Enable Code Coverage" OFF)

message("CMAKE_SYSTEM_PROCESSOR ${CMAKE_SYSTEM_PROCESSOR}")

if(WIN32)
  #if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  #  execute_process(COMMAND "${CMAKE_CXX_COMPILER}" "${CMAKE_SOURCE_DIR}/stub_atomic.cpp" -c -o "${CMAKE_BINARY_DIR}/atomic.lib")
  #endif()
endif()

if(APPLE)
  set(OSX_TARGET_PARAMETER -mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET})
  list(APPEND ALL_COMPILE_OPTIONS ${OSX_TARGET_PARAMETER})
endif()

# Default to C99
if(NOT CMAKE_C_STANDARD) 
  set(CMAKE_C_STANDARD 99)
endif()

# Default to C++17
set(CMAKE_CXX_STANDARD 20)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(ABSL_PROPAGATE_CXX_STD ON)

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

if(WIN32)
  if("${CMAKE_CXX_COMPILER_ID}" MATCHES "MSVC")
    list(APPEND ALL_COMPILE_OPTIONS /MP)
    list(APPEND ALL_COMPILE_OPTIONS /Zi)
    list(APPEND ALL_LINK_OPTIONS /DEBUG)
  else()
    message("Release debug symbols enabled")
    list(APPEND ALL_COMPILE_OPTIONS -g)
  endif()
else()
  if(NOT DEFINED ENV{GITHUB_REPOSITORY})
    message("Release debug symbols enabled")
    list(APPEND ALL_COMPILE_OPTIONS -g)
  else()
    message("Release debug symbols disabled")
  endif()
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
include(cmake/cairo.cmake)
include(cmake/inja.cmake)
#if(NOT "${SANITIZER}" STREQUAL "")
  include(cmake/lua.cmake)
  #else()
  #  include(cmake/luajit.cmake)
  #endif()
 
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
include(cmake/perfetto.cmake)
include(cmake/zlib_processed.cmake)
include(cmake/opencv.cmake)
include(cmake/boost.cmake)
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
  proto/thalamus.proto
  #proto/delsys.proto
  )
apply_protoc_grpc(PROTO_GENERATED_SOURCES ${PROTO_SOURCES})

add_library(protoc_generated OBJECT ${PROTO_GENERATED_SOURCES} ${PROTO_SOURCES})
target_link_libraries(protoc_generated grpc++ grpc++_reflection)

message("PROTO_SOURCES ${PROTO_SOURCES}")
message("PROTO_GENERATED_SOURCES ${PROTO_GENERATED_SOURCES}")

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  set(WARNING_FLAGS 
    -Weverything -Wno-deprecated-declarations -Wno-c11-extensions -Wno-gcc-compat -Wno-c++98-compat-pedantic
    -Wno-c++20-compat -Wno-unsafe-buffer-usage -Wno-switch-default -Wno-c99-extensions
    -Wno-missing-template-arg-list-after-template-kw -Wno-padded -Wno-unknown-warning-option)
else()
  #set(WARNING_FLAGS /Wall)
  set(WARNING_FLAGS)
endif()

include_directories(${CMAKE_BINARY_DIR})

if("${CMAKE_CXX_COMPILER_ID}" MATCHES "MSVC")
  set_target_properties(boost PROPERTIES VS_GLOBAL_IntDir "boost_intermediate/x64/$(Configuration)/")
  set_target_properties(opencv PROPERTIES VS_GLOBAL_IntDir "opencv_intermediate/x64/$(Configuration)/")
  set_target_properties(zlib_processed PROPERTIES VS_GLOBAL_IntDir "zlib_processed_intermediate/x64/$(Configuration)/")
  set_target_properties(ffmpeg PROPERTIES VS_GLOBAL_IntDir "ffmpeg_intermediate/x64/$(Configuration)/")
endif()

add_library(hydrate "src/hydrate.cpp")
target_link_libraries(hydrate boost grpc++ hdf5-static ffmpeg zlib_processed)
add_dependencies(hydrate protoc_generated)
target_compile_definitions(hydrate PRIVATE _USE_MATH_DEFINES NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS _GNU_SOURCE)
target_include_directories(hydrate PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_compile_options(hydrate PRIVATE ${WARNING_FLAGS})
  
add_library(thalamus "${CMAKE_SOURCE_DIR}/src/thalamus.cpp"
                     "${CMAKE_SOURCE_DIR}/src/node_graph_impl.hpp"
                     "${CMAKE_SOURCE_DIR}/src/node_graph_impl.cpp"
                     "${CMAKE_SOURCE_DIR}/src/state.hpp"
                     "${CMAKE_SOURCE_DIR}/src/state.cpp"
                     "${CMAKE_SOURCE_DIR}/src/state_manager.hpp"
                     "${CMAKE_SOURCE_DIR}/src/state_manager.cpp"
                     "${CMAKE_SOURCE_DIR}/src/base_node.hpp"
                     "${CMAKE_SOURCE_DIR}/src/base_node.cpp"
                     "${CMAKE_SOURCE_DIR}/src/analog_node.hpp"
                     "${CMAKE_SOURCE_DIR}/src/analog_node.cpp"
                     "${CMAKE_SOURCE_DIR}/src/alpha_omega_node.hpp"
                     "${CMAKE_SOURCE_DIR}/src/alpha_omega_node.cpp"
                     "${CMAKE_SOURCE_DIR}/src/grpc_impl.hpp"
                     "${CMAKE_SOURCE_DIR}/src/grpc_impl.cpp"
                     "${CMAKE_SOURCE_DIR}/src/util.hpp"
                     "${CMAKE_SOURCE_DIR}/src/util.cpp"
                     "${CMAKE_SOURCE_DIR}/src/h5handle.hpp"
                     "${CMAKE_SOURCE_DIR}/src/h5handle.cpp"
                     "${CMAKE_SOURCE_DIR}/src/xsens_node.hpp"
                     "${CMAKE_SOURCE_DIR}/src/xsens_node.cpp"
                     "${CMAKE_SOURCE_DIR}/src/storage_node.hpp"
                     "${CMAKE_SOURCE_DIR}/src/storage_node.cpp"
                     "${CMAKE_SOURCE_DIR}/src/storage2_node.hpp"
                     "${CMAKE_SOURCE_DIR}/src/storage2_node.cpp"
                     "${CMAKE_SOURCE_DIR}/src/run_node.hpp"
                     "${CMAKE_SOURCE_DIR}/src/run_node.cpp"
                     "${CMAKE_SOURCE_DIR}/src/run2_node.hpp"
                     "${CMAKE_SOURCE_DIR}/src/run2_node.cpp"
                     "${CMAKE_SOURCE_DIR}/src/remote_node.hpp"
                     "${CMAKE_SOURCE_DIR}/src/remote_node.cpp"
                     "${CMAKE_SOURCE_DIR}/src/remotelog_node.hpp"
                     "${CMAKE_SOURCE_DIR}/src/remotelog_node.cpp"
                     "${CMAKE_SOURCE_DIR}/src/ophanim_node.hpp"
                     "${CMAKE_SOURCE_DIR}/src/ophanim_node.cpp"
                     "${CMAKE_SOURCE_DIR}/src/task_controller_node.hpp"
                     "${CMAKE_SOURCE_DIR}/src/task_controller_node.cpp"
                     "${CMAKE_SOURCE_DIR}/src/image_node.hpp"
                     "${CMAKE_SOURCE_DIR}/src/image_node.cpp"
                     "${CMAKE_SOURCE_DIR}/src/oculomatic_node.hpp"
                     "${CMAKE_SOURCE_DIR}/src/oculomatic_node.cpp"
                     "${CMAKE_SOURCE_DIR}/src/distortion_node.hpp"
                     "${CMAKE_SOURCE_DIR}/src/distortion_node.cpp"
                     "${CMAKE_SOURCE_DIR}/src/text_node.hpp"
                     "${CMAKE_SOURCE_DIR}/src/text_node.cpp"
                     "${CMAKE_SOURCE_DIR}/src/log_node.hpp"
                     "${CMAKE_SOURCE_DIR}/src/log_node.cpp"
                     "${CMAKE_SOURCE_DIR}/src/thread_pool.hpp"
                     "${CMAKE_SOURCE_DIR}/src/thread_pool.cpp"
                     "${CMAKE_SOURCE_DIR}/src/base64.hpp"
                     "${CMAKE_SOURCE_DIR}/src/base64.cpp"
                     "${CMAKE_SOURCE_DIR}/src/nidaq_node.hpp"
                     "${CMAKE_SOURCE_DIR}/src/gentl.h"
                     "${CMAKE_SOURCE_DIR}/src/genicam_node.hpp"
                     "${CMAKE_SOURCE_DIR}/src/genicam_node.cpp"
                     "${CMAKE_SOURCE_DIR}/src/channel_picker_node.hpp"
                     "${CMAKE_SOURCE_DIR}/src/channel_picker_node.cpp"
                     "${CMAKE_SOURCE_DIR}/src/algebra_node.hpp"
                     "${CMAKE_SOURCE_DIR}/src/algebra_node.cpp"
                     "${CMAKE_SOURCE_DIR}/src/normalize_node.hpp"
                     "${CMAKE_SOURCE_DIR}/src/normalize_node.cpp"
                     "${CMAKE_SOURCE_DIR}/src/lua_node.hpp"
                     "${CMAKE_SOURCE_DIR}/src/lua_node.cpp"
                     "${CMAKE_SOURCE_DIR}/src/intan_node.hpp"
                     "${CMAKE_SOURCE_DIR}/src/intan_node.cpp"
                     "${CMAKE_SOURCE_DIR}/src/aruco_node.hpp"
                     "${CMAKE_SOURCE_DIR}/src/aruco_node.cpp"
                     "${CMAKE_SOURCE_DIR}/src/spikeglx_node.hpp"
                     "${CMAKE_SOURCE_DIR}/src/spikeglx_node.cpp"
                     "${CMAKE_SOURCE_DIR}/src/sync_node.hpp"
                     "${CMAKE_SOURCE_DIR}/src/sync_node.cpp"
                     "${CMAKE_SOURCE_DIR}/src/video_node.hpp"
                     "${CMAKE_SOURCE_DIR}/src/video_node.cpp"
                     "${CMAKE_SOURCE_DIR}/src/touchscreen_node.hpp"
                     "${CMAKE_SOURCE_DIR}/src/touchscreen_node.cpp"
                     "${CMAKE_SOURCE_DIR}/src/stim_node.hpp"
                     "${CMAKE_SOURCE_DIR}/src/stim_node.cpp"
                     "${CMAKE_SOURCE_DIR}/src/stim_printer_node.hpp"
                     "${CMAKE_SOURCE_DIR}/src/stim_printer_node.cpp"
                     "${CMAKE_SOURCE_DIR}/src/wallclock_node.hpp"
                     "${CMAKE_SOURCE_DIR}/src/wallclock_node.cpp"
                     "${CMAKE_SOURCE_DIR}/src/delsys_node.hpp"
                     "${CMAKE_SOURCE_DIR}/src/delsys_node.cpp"
                     "${CMAKE_SOURCE_DIR}/src/test_pulse_node.hpp"
                     "${CMAKE_SOURCE_DIR}/src/test_pulse_node.cpp"
                     #"${CMAKE_SOURCE_DIR}/src/hexascope_node.hpp"
                     #"${CMAKE_SOURCE_DIR}/src/hexascope_node.cpp"
                     "${CMAKE_SOURCE_DIR}/src/thalamus/file.hpp"
                     "${CMAKE_SOURCE_DIR}/src/thalamus/file.cpp"
                     "${CMAKE_SOURCE_DIR}/src/thalamus/thread.hpp"
                     "${CMAKE_SOURCE_DIR}/src/thalamus/thread.cpp"
                     "${CMAKE_SOURCE_DIR}/src/thalamus/tracing.hpp"
                     "${CMAKE_SOURCE_DIR}/src/thalamus/tracing.cpp"
                     "${CMAKE_SOURCE_DIR}/proto/thalamus.proto")
target_sources(thalamus PRIVATE 
               "${CMAKE_SOURCE_DIR}/src/pupil_node.hpp"
               "${CMAKE_SOURCE_DIR}/src/pupil_node.cpp"
               "${CMAKE_SOURCE_DIR}/src/chessboard_node.hpp"
               "${CMAKE_SOURCE_DIR}/src/chessboard_node.cpp")

if(WIN32)
  target_compile_definitions(thalamus PRIVATE _WIN32_WINNT=0x0A00 _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN BOOST_STACKTRACE_USE_WINDBG)
  target_link_libraries(thalamus dbgeng ole32)
  if("${CMAKE_CXX_COMPILER_ID}" MATCHES "MSVC")
    target_compile_options(thalamus PRIVATE /bigobj)
  endif()
  target_sources(thalamus PRIVATE "${CMAKE_SOURCE_DIR}/src/nidaq_node_windows.cpp"
                                  "${CMAKE_SOURCE_DIR}/src/brainproducts_node.hpp"
                                  "${CMAKE_SOURCE_DIR}/src/brainproducts_node.cpp")
  target_include_directories(thalamus PRIVATE "${CMAKE_SOURCE_DIR}/src/brain_products")
elseif(APPLE)
  target_sources(thalamus PRIVATE "${CMAKE_SOURCE_DIR}/src/nidaq_node_mac.cpp")
  target_compile_definitions(thalamus PRIVATE _GNU_SOURCE)
else()
  target_sources(thalamus PRIVATE "${CMAKE_SOURCE_DIR}/src/nidaq_node_linux.cpp"
                                  "${CMAKE_SOURCE_DIR}/src/ros2_node.hpp"
                                  "${CMAKE_SOURCE_DIR}/src/ros2_node.cpp")
  target_link_libraries(thalamus comedi m)
endif()
target_compile_options(thalamus PRIVATE ${WARNING_FLAGS})
if(CODE_COVERAGE)
  target_compile_options(hydrate PRIVATE -fprofile-instr-generate -fcoverage-mapping)
  target_compile_options(thalamus PRIVATE -fprofile-instr-generate -fcoverage-mapping)
  target_link_options(hydrate PRIVATE -fprofile-instr-generate -fcoverage-mapping)
  target_link_options(thalamus PRIVATE -fprofile-instr-generate -fcoverage-mapping)
  target_compile_definitions(hydrate PRIVATE CODE_COVERAGE)
  target_compile_definitions(thalamus PRIVATE CODE_COVERAGE)
endif()

target_compile_definitions(thalamus PRIVATE _USE_MATH_DEFINES)
target_include_directories(thalamus PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(thalamus opencv hdf5-static lua
  boost grpc++ grpc++_reflection inja
  absl::str_format_internal absl::strings absl::str_format ffmpeg cairo perfetto)
if(NOT APPLE)
  target_link_libraries(thalamus zlib_processed)
endif()
add_dependencies(thalamus protoc_generated)

add_executable(native src/main.cpp)

if(WIN32)
  target_compile_definitions(native PRIVATE _WIN32_WINNT=0x0A00)
endif()
target_compile_definitions(native PRIVATE _GNU_SOURCE)

target_compile_options(native PRIVATE ${WARNING_FLAGS})
target_include_directories(native PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(native PRIVATE hydrate thalamus opencv thalamus_ffmpeg ffmpeg sdl protoc_generated lua cairo)

if(APPLE)
  target_link_options(native PRIVATE -pagezero_size 10000 -image_base 7fff04c4a000)
endif()

if(NOT "${CMAKE_CXX_COMPILER_ID}" MATCHES "MSVC")
  target_link_options(native PRIVATE -g)
endif()

add_custom_command(TARGET native POST_BUILD COMMAND cmake -E copy "$<TARGET_FILE:native>" "${CMAKE_SOURCE_DIR}/thalamus")

add_executable(test src/test.cpp)

if(WIN32)
  target_compile_definitions(test PRIVATE _WIN32_WINNT=0x0A00)
endif()
target_compile_definitions(test PRIVATE _GNU_SOURCE)

target_compile_options(test PRIVATE ${WARNING_FLAGS})
target_include_directories(test PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(test PRIVATE
  gtest hydrate thalamus opencv thalamus_ffmpeg ffmpeg sdl protoc_generated lua cairo)

target_compile_options(test PRIVATE -fprofile-instr-generate -fcoverage-mapping)
target_link_options(test PRIVATE -fprofile-instr-generate -fcoverage-mapping)
if(APPLE)
  target_link_options(test PRIVATE -pagezero_size 10000 -image_base 7fff04c4a000)
endif()

if(NOT "${CMAKE_CXX_COMPILER_ID}" MATCHES "MSVC")
  target_link_options(test PRIVATE -g)
endif()

if(WIN32)
  #Install nuget packages during configure
  execute_process(COMMAND msbuild -t:restore "/p:Platform=Any CPU"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/dotnet")

  add_custom_target(dotnet
    COMMAND msbuild dotnet.sln /p:Configuration=${CMAKE_BUILD_TYPE} "/p:Platform=Any CPU"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/dotnet")
  
  add_custom_command(TARGET dotnet POST_BUILD COMMAND 
    cmake -E copy_directory "${CMAKE_SOURCE_DIR}/dotnet/dotnet/bin/${CMAKE_BUILD_TYPE}/net8.0"
    "${CMAKE_SOURCE_DIR}/thalamus/dotnet")
  add_dependencies(native dotnet)
endif()
