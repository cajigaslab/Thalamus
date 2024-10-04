set(HDF5_EXTERNALLY_CONFIGURED 1)
set(HDF5_BUILD_TOOLS OFF)
set(HDF5_ENABLE_Z_LIB_SUPPORT ON)
set(H5_ZLIB_LIBRARIES "$<TARGET_FILE:zlibstatic>")
set(HDF5_BUILD_EXAMPLES OFF)
set(ZLIB_ROOT "${ZLIB_PROCESSED_HEADER_DIR}")
set(H5_ZLIB_HEADER zlib.h)
set(H5_HAVE_SYS_STAT_H ON)
set(H5_HAVE_SYS_TYPES_H ON)
set(H5_HAVE_VISUAL_STUDIO ON)
set(H5_ZLIB_INCLUDE_DIRS "${ZLIB_PROCESSED_HEADER_DIR}")

FetchContent_Declare(
  hdf5
  URL      https://support.hdfgroup.org/releases/hdf5/v1_14/v1_14_5/downloads/hdf5-1.14.5.tar.gz
  URL_HASH SHA256=ec2e13c52e60f9a01491bb3158cb3778c985697131fc6a342262d32a26e58e44
)
FetchContent_MakeAvailable(hdf5)
add_dependencies(hdf5-static zlib_processed)

#file(MAKE_DIRECTORY "${hdf5_BINARY_DIR}/Debug")
#file(MAKE_DIRECTORY "${hdf5_BINARY_DIR}/Release")
#
#if(WIN32)
#  set(HDF5_LIB_SUFFIX "$<$<CONFIG:Debug>:_D>.lib")
#else()
#  set(HDF5_LIB_SUFFIX ".a")
#endif()
#add_custom_command(OUTPUT "${hdf5_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/CMakeCache.txt"
#                   COMMAND cmake ${hdf5_SOURCE_DIR} -DCMAKE_POSITION_INDEPENDENT_CODE=ON
#                            -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
#                            -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
#                            -DCMAKE_LINKER=${CMAKE_LINKER}
#                            -DCMAKE_CXX_STANDARD=${CMAKE_CXX_STANDARD}
#                           "-DCMAKE_CXX_FLAGS=${ALL_COMPILE_OPTIONS_SPACED}" 
#                           "-DCMAKE_C_FLAGS=${ALL_COMPILE_OPTIONS_SPACED}"
#                           "-DCMAKE_EXE_LINKER_FLAGS=${ALL_LINK_OPTIONS_SPACED}"
#                           "-DCMAKE_BUILD_TYPE=$<IF:$<CONFIG:Debug>,Debug,Release>" -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTING=OFF
#                           "-DCMAKE_INSTALL_PREFIX=${hdf5_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install"
#                           -DBUILD_STATIC_CRT_LIBS=ON
#                           -DCMAKE_MSVC_RUNTIME_LIBRARY=${CMAKE_MSVC_RUNTIME_LIBRARY}
#                   && cmake -E touch_nocreate "${hdf5_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/CMakeCache.txt"
#                   WORKING_DIRECTORY "${hdf5_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>")
#add_custom_command(DEPENDS "${hdf5_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/CMakeCache.txt"
#                   OUTPUT "${hdf5_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install/lib/libhdf5${HDF5_LIB_SUFFIX}"
#                   COMMAND echo COMBO "$<IF:$<CONFIG:Debug>,Debug,Release>" && cmake --build . --config "$<IF:$<CONFIG:Debug>,Debug,Release>"
#                   && cmake --install . --config "$<IF:$<CONFIG:Debug>,Debug,Release>"
#                   && cmake -E touch_nocreate "${hdf5_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install/lib/libhdf5${HDF5_LIB_SUFFIX}"
#                   WORKING_DIRECTORY "${hdf5_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>")
#add_library(hdf5 INTERFACE "${hdf5_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install/lib/libhdf5${HDF5_LIB_SUFFIX}")
#target_include_directories(hdf5 INTERFACE "${hdf5_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install/include")
#target_link_libraries(hdf5 INTERFACE "${hdf5_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install/lib/libhdf5${HDF5_LIB_SUFFIX}")
