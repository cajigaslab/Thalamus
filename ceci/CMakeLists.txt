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

project(ceci)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
include(FetchContent)

#JSON
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.12.0 
)
FetchContent_MakeAvailable(nlohmann_json)

find_package(Python)
message("Python_EXECUTABLE ${Python_EXECUTABLE}")

#HDF5
set(HDF5_EXTERNALLY_CONFIGURED 1)
set(HDF5_BUILD_TOOLS OFF)
set(HDF5_ENABLE_Z_LIB_SUPPORT OFF)
set(HDF5_BUILD_EXAMPLES OFF)
set(H5_HAVE_SYS_STAT_H ON)
set(H5_HAVE_SYS_TYPES_H ON)
set(H5_HAVE_VISUAL_STUDIO ON)

FetchContent_Declare(
  hdf5
  URL      https://github.com/HDFGroup/hdf5/releases/download/hdf5_1.14.5/hdf5-1.14.5.tar.gz
  URL_HASH SHA256=ec2e13c52e60f9a01491bb3158cb3778c985697131fc6a342262d32a26e58e44
)
FetchContent_MakeAvailable(hdf5)

#Thalamus
execute_process(
  COMMAND "${Python_EXECUTABLE}" -c "import thalamus;import pathlib;print(pathlib.Path(thalamus.__file__).parent)"
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE THALAMUS_PACKAGE_DIR
  OUTPUT_STRIP_TRAILING_WHITESPACE)

#NIDAQMX
add_library(nidaqmx INTERFACE)
target_link_libraries(nidaqmx INTERFACE "C:/Program Files (x86)/National Instruments/Shared/ExternalCompilerSupport/C/lib64/msvc/NIDAQmx.lib")
target_include_directories(nidaqmx INTERFACE "C:/Program Files (x86)/National Instruments/Shared/ExternalCompilerSupport/C/include")

#Implementation
add_library(ceci SHARED ext.cpp Rec_Stim.cpp)
target_link_libraries(ceci PUBLIC nlohmann_json::nlohmann_json nidaqmx hdf5-static)
target_include_directories(ceci PUBLIC "${THALAMUS_PACKAGE_DIR}" "${CMAKE_SOURCE_DIR}")
