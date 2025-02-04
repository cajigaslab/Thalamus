if("${SANITIZER}" STREQUAL thread)
  set(LLVM_SANITIZER -DLLVM_USE_SANITIZER=Thread)
elseif("${SANITIZER}" STREQUAL address)
  set(LLVM_SANITIZER -DLLVM_USE_SANITIZER=Address)
elseif("${SANITIZER}" STREQUAL memory)
  set(LLVM_SANITIZER -DLLVM_USE_SANITIZER=MemoryWithOrigins)
endif()

FetchContent_Declare(
  llvm
  GIT_REPOSITORY https://github.com/llvm/llvm-project
  GIT_TAG        llvmorg-14.0.3
  SOURCE_SUBDIR  thalamus-nonexistant
)  
message("Populate libc++")
FetchContent_MakeAvailable(llvm)
message("Populated libc++")
if(NOT EXISTS "${llvm_BINARY_DIR}/CMakeCache.txt")
  message("BUILD libc++")
  execute_process(
    COMMAND cmake ${llvm_SOURCE_DIR}/runtimes -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -DLLVM_ENABLE_RUNTIMES=libcxx\\;libcxxabi
    -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
    -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
    -DCMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}
    -DCMAKE_CXX_STANDARD=${CMAKE_CXX_STANDARD}
    ${LLVM_SANITIZER}
    -DLIBCXX_HERMETIC_STATIC_LIBRARY=ON
    -DLIBCXXABI_HERMETIC_STATIC_LIBRARY=ON
    WORKING_DIRECTORY ${llvm_BINARY_DIR})
  message("BUILT libc++")
endif()
if(NOT EXISTS "${llvm_BINARY_DIR}/lib/libc++.a")
  execute_process(COMMAND cmake --build . ${CMAKE_PARALLEL} -- cxx_static cxxabi_static VERBOSE=1
                    WORKING_DIRECTORY ${llvm_BINARY_DIR})
  if("${SANITIZER}" STREQUAL thread)
    execute_process(COMMAND ar dv libc++abi.a cxa_guard.cpp.o WORKING_DIRECTORY ${llvm_BINARY_DIR}/lib)
  endif()
endif()
if(NOT EXISTS "${llvm_BINARY_DIR}/lib/combined/libc++.a")
  execute_process(COMMAND bash ${CMAKE_SOURCE_DIR}/make_combined.sh ${OSX_TARGET_PARAMETER}
                    WORKING_DIRECTORY ${llvm_BINARY_DIR}/lib)
endif()

set(LIBCXX_COMPILE_OPTIONS -nostdinc++ "-isystem${llvm_BINARY_DIR}/include" "-isystem${llvm_BINARY_DIR}/include/c++/v1")
set(LIBCXX_LINK_OPTIONS -stdlib=libc++ "-L${llvm_BINARY_DIR}/lib/combined" -lpthread)
