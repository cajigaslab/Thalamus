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

FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.12.0 
)
FetchContent_MakeAvailable(nlohmann_json)

find_package(Python)
message("Python_EXECUTABLE ${Python_EXECUTABLE}")

execute_process(
  COMMAND "${Python_EXECUTABLE}" -c "import thalamus;import pathlib;print(pathlib.Path(thalamus.__file__).parent)"
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE THALAMUS_PACKAGE_DIR
  OUTPUT_STRIP_TRAILING_WHITESPACE)

add_library(nidaqmx INTERFACE)
target_link_libraries(nidaqmx INTERFACE "C:/Program Files (x86)/National Instruments/Shared/ExternalCompilerSupport/C/lib64/msvc/NIDAQmx.lib")
target_include_directories(nidaqmx INTERFACE "C:/Program Files (x86)/National Instruments/Shared/ExternalCompilerSupport/C/include")

add_library(ceci SHARED ext.cpp)
target_link_libraries(ceci PUBLIC nlohmann_json::nlohmann_json nidaqmx)
target_include_directories(ceci PUBLIC "${THALAMUS_PACKAGE_DIR}")
