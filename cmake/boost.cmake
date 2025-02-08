set(BOOST_VERSION 86)

if(DEFINED BOOST_BINARY_DIR)
  FetchContent_Declare(
    boost_content
    URL https://archives.boost.io/release/1.86.0/source/boost_1_86_0.tar.gz
    URL_HASH SHA256=2575e74ffc3ef1cd0babac2c1ee8bdb5782a0ee672b1912da40e5b4b591ca01f
    BINARY_DIR "${BOOST_BINARY_DIR}")
else()
  FetchContent_Declare(
    boost_content
    URL https://archives.boost.io/release/1.86.0/source/boost_1_86_0.tar.gz
    URL_HASH SHA256=2575e74ffc3ef1cd0babac2c1ee8bdb5782a0ee672b1912da40e5b4b591ca01f)
endif()
FetchContent_MakeAvailable(boost_content)

if(WIN32)
  add_custom_command(OUTPUT "${boost_content_SOURCE_DIR}/b2.exe"
    COMMAND cmd /c call bootstrap.bat
    WORKING_DIRECTORY ${boost_content_SOURCE_DIR})
else()
  add_custom_command(OUTPUT "${boost_content_SOURCE_DIR}/b2"
    COMMAND sh bootstrap.sh
    WORKING_DIRECTORY ${boost_content_SOURCE_DIR})
endif()

if(WIN32)
  set(BOOST_ALL_COMPILE_OPTIONS_SPACED "${ALL_COMPILE_OPTIONS_SPACED}")
  string(REPLACE "/MP" "" BOOST_ALL_COMPILE_OPTIONS_SPACED "${BOOST_ALL_COMPILE_OPTIONS_SPACED}")
  string(REPLACE "/Zi" "" BOOST_ALL_COMPILE_OPTIONS_SPACED "${BOOST_ALL_COMPILE_OPTIONS_SPACED}")
  string(STRIP "${BOOST_ALL_COMPILE_OPTIONS_SPACED}" BOOST_ALL_COMPILE_OPTIONS_SPACED)
  if("${CMAKE_CXX_COMPILER_ID}" MATCHES "MSVC")
    string(APPEND BOOST_ALL_COMPILE_OPTIONS_SPACED " /FS")
  endif()
  set(BOOST_ABI_TAG "$<IF:$<CONFIG:Debug>,-sgd-x64,-s-x64>")
  if("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
    message("CMAKE_CXX_COMPILER_VERSION ${CMAKE_CXX_COMPILER_VERSION}")
    string(REPLACE "." ";" BOOST_COMPILER_VERSION "${CMAKE_CXX_COMPILER_VERSION}")
    message("BOOST_COMPILER_VERSION ${BOOST_COMPILER_VERSION}")
    list(GET BOOST_COMPILER_VERSION 0 BOOST_MAJOR_COMPILER_VERSION)
    set(BOOST_VC_TOOLSET clangw${BOOST_MAJOR_COMPILER_VERSION})
    set(BOOST_TOOLSET toolset=clang-win)
  else()
    execute_process(COMMAND cmd /c call bootstrap.bat WORKING_DIRECTORY "${boost_content_SOURCE_DIR}" OUTPUT_VARIABLE BOOTSTRAP_STDOUT)
    string(REGEX MATCH "### Using 'vc[0-9]+' toolset" VC_TOOLSET_LINE "${BOOTSTRAP_STDOUT}")
    string(REGEX MATCH "vc[0-9]+" VC_TOOLSET_MATCH "${VC_TOOLSET_LINE}")
    set(BOOST_VC_TOOLSET ${VC_TOOLSET_MATCH})
    set(BOOST_CFLAGS "cflags=/FS")
  endif()
  message("USING TOOLSET ${BOOST_VC_TOOLSET}")

  set(BOOST_LIBS 
    "${boost_content_SOURCE_DIR}/stage/lib/libboost_date_time-${BOOST_VC_TOOLSET}-mt${BOOST_ABI_TAG}-1_${BOOST_VERSION}.lib"
    "${boost_content_SOURCE_DIR}/stage/lib/libboost_filesystem-${BOOST_VC_TOOLSET}-mt${BOOST_ABI_TAG}-1_${BOOST_VERSION}.lib"
    "${boost_content_SOURCE_DIR}/stage/lib/libboost_system-${BOOST_VC_TOOLSET}-mt${BOOST_ABI_TAG}-1_${BOOST_VERSION}.lib"
    "${boost_content_SOURCE_DIR}/stage/lib/libboost_program_options-${BOOST_VC_TOOLSET}-mt${BOOST_ABI_TAG}-1_${BOOST_VERSION}.lib"
    "${boost_content_SOURCE_DIR}/stage/lib/libboost_log-${BOOST_VC_TOOLSET}-mt${BOOST_ABI_TAG}-1_${BOOST_VERSION}.lib"
    "${boost_content_SOURCE_DIR}/stage/lib/libboost_log_setup-${BOOST_VC_TOOLSET}-mt${BOOST_ABI_TAG}-1_${BOOST_VERSION}.lib"
    "${boost_content_SOURCE_DIR}/stage/lib/libboost_container-${BOOST_VC_TOOLSET}-mt${BOOST_ABI_TAG}-1_${BOOST_VERSION}.lib"
    "${boost_content_SOURCE_DIR}/stage/lib/libboost_thread-${BOOST_VC_TOOLSET}-mt${BOOST_ABI_TAG}-1_${BOOST_VERSION}.lib"
    "${boost_content_SOURCE_DIR}/stage/lib/libboost_chrono-${BOOST_VC_TOOLSET}-mt${BOOST_ABI_TAG}-1_${BOOST_VERSION}.lib"
    "${boost_content_SOURCE_DIR}/stage/lib/libboost_json-${BOOST_VC_TOOLSET}-mt${BOOST_ABI_TAG}-1_${BOOST_VERSION}.lib"
    "${boost_content_SOURCE_DIR}/stage/lib/libboost_atomic-${BOOST_VC_TOOLSET}-mt${BOOST_ABI_TAG}-1_${BOOST_VERSION}.lib")
 
  add_custom_command(OUTPUT ${BOOST_LIBS}
    DEPENDS "${boost_content_SOURCE_DIR}/b2.exe"
    COMMAND b2 
    ${BOOST_TOOLSET}
    "${BOOST_CFLAGS}" 
    "cxxflags=${BOOST_ALL_COMPILE_OPTIONS_SPACED} -DBOOST_ASIO_HAS_STD_INVOKE_RESULT -D_WIN32_WINNT=0x0A00"
    "linkflags=${ALL_LINK_OPTIONS_SPACED}"
    "--build-dir=${boost_content_BINARY_DIR}"
    --abbreviate-paths 
    --with-atomic --with-chrono --with-thread --with-filesystem --with-date_time --with-system --with-program_options
    --with-log --with-json --with-container address-model=64 debug-symbols=on debug-store=database runtime-link=static
    link=static cxxstd=20
    "$<IF:$<CONFIG:Debug>,debug,release>"
    WORKING_DIRECTORY ${boost_content_SOURCE_DIR})

  add_library(boost INTERFACE ${BOOST_LIBS})
  target_link_libraries(boost INTERFACE ${BOOST_LIBS})
else()
  add_custom_command(
    DEPENDS "${boost_content_SOURCE_DIR}/b2"
    OUTPUT "${boost_content_SOURCE_DIR}/stage-debug/lib/libboost_date_time.a"
                            "${boost_content_SOURCE_DIR}/stage-debug/lib/libboost_filesystem.a"
                            "${boost_content_SOURCE_DIR}/stage-debug/lib/libboost_system.a"
                            "${boost_content_SOURCE_DIR}/stage-debug/lib/libboost_program_options.a"
                            "${boost_content_SOURCE_DIR}/stage-debug/lib/libboost_log.a"
                            "${boost_content_SOURCE_DIR}/stage-debug/lib/libboost_log_setup.a"
                            "${boost_content_SOURCE_DIR}/stage-debug/lib/libboost_container.a"
                            "${boost_content_SOURCE_DIR}/stage-debug/lib/libboost_thread.a"
                            "${boost_content_SOURCE_DIR}/stage-debug/lib/libboost_chrono.a"
                            "${boost_content_SOURCE_DIR}/stage-debug/lib/libboost_json.a"
                            "${boost_content_SOURCE_DIR}/stage-debug/lib/libboost_atomic.a"
                    COMMAND
		    sh ${CMAKE_SOURCE_DIR}/build_boost.sh "${ALL_COMPILE_OPTIONS_SPACED} -DBOOST_ASIO_HAS_STD_INVOKE_RESULT" " ${ALL_LINK_OPTIONS_SPACED}" debug
                    WORKING_DIRECTORY ${boost_content_SOURCE_DIR})
  add_custom_command(
    DEPENDS "${boost_content_SOURCE_DIR}/b2"
    OUTPUT "${boost_content_SOURCE_DIR}/stage-release/lib/libboost_date_time.a"
                            "${boost_content_SOURCE_DIR}/stage-release/lib/libboost_filesystem.a"
                            "${boost_content_SOURCE_DIR}/stage-release/lib/libboost_system.a"
                            "${boost_content_SOURCE_DIR}/stage-release/lib/libboost_program_options.a"
                            "${boost_content_SOURCE_DIR}/stage-release/lib/libboost_log.a"
                            "${boost_content_SOURCE_DIR}/stage-release/lib/libboost_log_setup.a"
                            "${boost_content_SOURCE_DIR}/stage-release/lib/libboost_container.a"
                            "${boost_content_SOURCE_DIR}/stage-release/lib/libboost_thread.a"
                            "${boost_content_SOURCE_DIR}/stage-release/lib/libboost_chrono.a"
                            "${boost_content_SOURCE_DIR}/stage-release/lib/libboost_json.a"
                            "${boost_content_SOURCE_DIR}/stage-release/lib/libboost_atomic.a"
                    COMMAND
		    sh ${CMAKE_SOURCE_DIR}/build_boost.sh "${ALL_COMPILE_OPTIONS_SPACED} -DBOOST_ASIO_HAS_STD_INVOKE_RESULT" " ${ALL_LINK_OPTIONS_SPACED}" release
                    WORKING_DIRECTORY ${boost_content_SOURCE_DIR})
  add_library(boost INTERFACE
    "$<IF:$<CONFIG:Debug>,${boost_content_SOURCE_DIR}/stage-debug/lib/libboost_date_time.a,${boost_content_SOURCE_DIR}/stage-release/lib/libboost_date_time.a>"
    "$<IF:$<CONFIG:Debug>,${boost_content_SOURCE_DIR}/stage-debug/lib/libboost_filesystem.a,${boost_content_SOURCE_DIR}/stage-release/lib/libboost_filesystem.a>"
    "$<IF:$<CONFIG:Debug>,${boost_content_SOURCE_DIR}/stage-debug/lib/libboost_system.a,${boost_content_SOURCE_DIR}/stage-release/lib/libboost_system.a>"
    "$<IF:$<CONFIG:Debug>,${boost_content_SOURCE_DIR}/stage-debug/lib/libboost_program_options.a,${boost_content_SOURCE_DIR}/stage-release/lib/libboost_program_options.a>"
    "$<IF:$<CONFIG:Debug>,${boost_content_SOURCE_DIR}/stage-debug/lib/libboost_log.a,${boost_content_SOURCE_DIR}/stage-release/lib/libboost_log.a>"
    "$<IF:$<CONFIG:Debug>,${boost_content_SOURCE_DIR}/stage-debug/lib/libboost_log_setup.a,${boost_content_SOURCE_DIR}/stage-release/lib/libboost_log_setup.a>"
    "$<IF:$<CONFIG:Debug>,${boost_content_SOURCE_DIR}/stage-debug/lib/libboost_container.a,${boost_content_SOURCE_DIR}/stage-release/lib/libboost_container.a>"
    "$<IF:$<CONFIG:Debug>,${boost_content_SOURCE_DIR}/stage-debug/lib/libboost_thread.a,${boost_content_SOURCE_DIR}/stage-release/lib/libboost_thread.a>"
    "$<IF:$<CONFIG:Debug>,${boost_content_SOURCE_DIR}/stage-debug/lib/libboost_chrono.a,${boost_content_SOURCE_DIR}/stage-release/lib/libboost_chrono.a>"
    "$<IF:$<CONFIG:Debug>,${boost_content_SOURCE_DIR}/stage-debug/lib/libboost_json.a,${boost_content_SOURCE_DIR}/stage-release/lib/libboost_json.a>"
    "$<IF:$<CONFIG:Debug>,${boost_content_SOURCE_DIR}/stage-debug/lib/libboost_atomic.a,${boost_content_SOURCE_DIR}/stage-release/lib/libboost_atomic.a>")
  target_link_libraries(boost INTERFACE
    debug "${boost_content_SOURCE_DIR}/stage-debug/lib/libboost_date_time.a"
    optimized "${boost_content_SOURCE_DIR}/stage-release/lib/libboost_date_time.a"
    debug "${boost_content_SOURCE_DIR}/stage-debug/lib/libboost_system.a"
    optimized "${boost_content_SOURCE_DIR}/stage-release/lib/libboost_system.a"
    debug "${boost_content_SOURCE_DIR}/stage-debug/lib/libboost_program_options.a"
    optimized "${boost_content_SOURCE_DIR}/stage-release/lib/libboost_program_options.a"
    debug "${boost_content_SOURCE_DIR}/stage-debug/lib/libboost_log.a"
    optimized "${boost_content_SOURCE_DIR}/stage-release/lib/libboost_log.a"
    debug "${boost_content_SOURCE_DIR}/stage-debug/lib/libboost_log_setup.a"
    optimized "${boost_content_SOURCE_DIR}/stage-release/lib/libboost_log_setup.a"
    debug "${boost_content_SOURCE_DIR}/stage-debug/lib/libboost_filesystem.a"
    optimized "${boost_content_SOURCE_DIR}/stage-release/lib/libboost_filesystem.a"
    debug "${boost_content_SOURCE_DIR}/stage-debug/lib/libboost_container.a"
    optimized "${boost_content_SOURCE_DIR}/stage-release/lib/libboost_container.a"
    debug "${boost_content_SOURCE_DIR}/stage-debug/lib/libboost_thread.a"
    optimized "${boost_content_SOURCE_DIR}/stage-release/lib/libboost_thread.a"
    debug "${boost_content_SOURCE_DIR}/stage-debug/lib/libboost_chrono.a"
    optimized "${boost_content_SOURCE_DIR}/stage-release/lib/libboost_chrono.a"
    debug "${boost_content_SOURCE_DIR}/stage-debug/lib/libboost_json.a"
    optimized "${boost_content_SOURCE_DIR}/stage-release/lib/libboost_json.a"
    debug "${boost_content_SOURCE_DIR}/stage-debug/lib/libboost_atomic.a"
    optimized "${boost_content_SOURCE_DIR}/stage-release/lib/libboost_atomic.a")
endif()
target_include_directories(boost INTERFACE "${boost_content_SOURCE_DIR}")
target_compile_definitions(boost INTERFACE BOOST_ALL_NO_LIB)
add_custom_target(build-boost DEPENDS boost)
