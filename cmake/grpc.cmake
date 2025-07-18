FetchContent_Declare(
  gRPC
  GIT_REPOSITORY https://github.com/grpc/grpc
  GIT_TAG        v1.66.1
)
set(BUILD_SHARED_LIBS OFF)
set(BUILD_TESTING OFF)
set(FETCHCONTENT_QUIET OFF)
set(gRPC_MSVC_STATIC_RUNTIME ON)
set(protobuf_MSVC_STATIC_RUNTIME ON CACHE BOOL "Protobuf: Link static runtime libraries")
set(ABSL_ENABLE_INSTALL ON)
#add_definitions(-DBORINGSSL_NO_CXX)
set(gRPC_BUILD_TESTS ON)
FetchContent_MakeAvailable(gRPC)
#file(READ "${grpc_SOURCE_DIR}/third_party/zlib/CMakeLists.txt" FILE_CONTENTS)
#string(REPLACE "cmake_minimum_required(VERSION 2.4.4)" "cmake_minimum_required(VERSION 3.12)" FILE_CONTENTS "${FILE_CONTENTS}")
#file(WRITE "${grpc_SOURCE_DIR}/third_party/zlib/CMakeLists.txt" "${FILE_CONTENTS}")
#add_subdirectory("${grpc_SOURCE_DIR}" "${grpc_BINARY_DIR}")

#target_compile_options(crypto PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:-w>")

macro(apply_protoc_grpc OUTPUT_SOURCES)
  
  foreach(PROTO_FILE ${ARGN})
    get_filename_component(PROTO_ABSOLUTE "${PROTO_FILE}" ABSOLUTE)
    get_filename_component(PROTO_NAME "${PROTO_FILE}" NAME_WE)
    get_filename_component(PROTO_DIRECTORY "${PROTO_ABSOLUTE}" DIRECTORY)
    set(apply_protoc_grpc_GENERATED "${CMAKE_CURRENT_BINARY_DIR}/${PROTO_NAME}.pb.h"
                                    "${CMAKE_CURRENT_BINARY_DIR}/${PROTO_NAME}.pb.cc"
                                    "${CMAKE_CURRENT_BINARY_DIR}/${PROTO_NAME}.grpc.pb.h"
                                    "${CMAKE_CURRENT_BINARY_DIR}/${PROTO_NAME}.grpc.pb.cc")
    add_custom_command(
          OUTPUT ${apply_protoc_grpc_GENERATED}
          COMMAND $<TARGET_FILE:protobuf::protoc> --grpc_out "${CMAKE_CURRENT_BINARY_DIR}"
          --cpp_out "${CMAKE_CURRENT_BINARY_DIR}"
          --plugin=protoc-gen-grpc=$<TARGET_FILE:grpc_cpp_plugin>
          -I "${PROTO_DIRECTORY}"
          ${PROTO_ABSOLUTE}
          DEPENDS "${PROTO_ABSOLUTE}" $<TARGET_FILE:protobuf::protoc>)
    if("${CMAKE_CXX_COMPILER_ID}" MATCHES "MSVC")
      set_source_files_properties(
        ${apply_protoc_grpc_GENERATED}
        PROPERTIES
        COMPILE_FLAGS /wd4267)
    else()
      set_source_files_properties(
        ${apply_protoc_grpc_GENERATED}
        PROPERTIES
        COMPILE_FLAGS -Wno-everything)
    endif()
    list(APPEND ${OUTPUT_SOURCES} ${apply_protoc_grpc_GENERATED})
  endforeach()

endmacro()

macro(apply_protoc OUTPUT_SOURCES)
  
  foreach(PROTO_FILE ${ARGN})
    get_filename_component(PROTO_ABSOLUTE "${PROTO_FILE}" ABSOLUTE)
    get_filename_component(PROTO_NAME "${PROTO_FILE}" NAME_WE)
    get_filename_component(PROTO_DIRECTORY "${PROTO_ABSOLUTE}" DIRECTORY)
    set(apply_protoc_grpc_GENERATED "${CMAKE_CURRENT_BINARY_DIR}/${PROTO_NAME}.pb.h"
                                    "${CMAKE_CURRENT_BINARY_DIR}/${PROTO_NAME}.pb.cc")
    add_custom_command(
          OUTPUT ${apply_protoc_grpc_GENERATED}
          COMMAND $<TARGET_FILE:protobuf::protoc>
          --cpp_out "${CMAKE_CURRENT_BINARY_DIR}"
          -I "${PROTO_DIRECTORY}"
          ${PROTO_ABSOLUTE}
          DEPENDS "${PROTO_ABSOLUTE}" $<TARGET_FILE:protobuf::protoc>)
    if("${CMAKE_CXX_COMPILER_ID}" MATCHES "MSVC")
      set_source_files_properties(
        ${apply_protoc_grpc_GENERATED}
        PROPERTIES
        COMPILE_FLAGS /wd4267)
    else()
      set_source_files_properties(
        ${apply_protoc_grpc_GENERATED}
        PROPERTIES
        COMPILE_FLAGS -Wno-everything)
    endif()
    list(APPEND ${OUTPUT_SOURCES} ${apply_protoc_grpc_GENERATED})
  endforeach()

endmacro()
