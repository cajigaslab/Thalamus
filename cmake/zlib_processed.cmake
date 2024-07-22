file(GLOB ZLIB_HEADERS ${grpc_SOURCE_DIR}/third_party/zlib/*.h)
list(APPEND ZLIB_HEADERS ${grpc_BINARY_DIR}/third_party/zlib/zconf.h)

set(ZLIB_HEADER_FILENAMES)
foreach(HEADER ${ZLIB_HEADERS})
  get_filename_component(FILENAME ${HEADER} NAME)
  list(APPEND ZLIB_HEADER_FILENAMES "${FILENAME}")
endforeach()

set(ZLIB_PROCESSED_HEADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/opencv_zlib_headers")
set(ZLIB_PKG_CONFIG_DIR "${CMAKE_CURRENT_BINARY_DIR}/opencv_zlib_headers")
set(ZLIB_PROCESSED_HEADER_PATHS)
foreach(HEADER ${ZLIB_HEADER_FILENAMES})
  list(APPEND ZLIB_PROCESSED_HEADER_PATHS "${ZLIB_PROCESSED_HEADER_DIR}/${HEADER}")
endforeach()

add_custom_command(DEPENDS zlibstatic
                   OUTPUT ${ZLIB_PROCESSED_HEADER_PATHS} "${ZLIB_PROCESSED_HEADER_DIR}/zlib.pc"
                   COMMAND cmake -E make_directory "${ZLIB_PROCESSED_HEADER_DIR}"
                   && cmake -E copy ${ZLIB_HEADERS} "${ZLIB_PROCESSED_HEADER_DIR}"
		   && cmake "-DZLIB_LIBRARY=$<TARGET_FILE:zlibstatic>" "-DOUTPUT_DIR=${ZLIB_PROCESSED_HEADER_DIR}" -P ${CMAKE_SOURCE_DIR}/generate_zlib_pc.cmake)

add_library(zlib_processed INTERFACE ${ZLIB_PROCESSED_HEADER_PATHS})
target_link_options(zlib_processed INTERFACE -Wl,--whole-archive "$<TARGET_FILE:zlibstatic>" -Wl,--no-whole-archive)
target_include_directories(zlib_processed INTERFACE "${ZLIB_PROCESSED_HEADER_DIR}")

