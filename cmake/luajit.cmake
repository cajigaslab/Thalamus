FetchContent_Declare(
  luajit
  GIT_REPOSITORY https://luajit.org/git/luajit.git
  GIT_TAG v2.1)
FetchContent_Populate(luajit)

execute_process(COMMAND git apply "${CMAKE_SOURCE_DIR}/patches/luajit" WORKING_DIRECTORY "${luajit_SOURCE_DIR}")

if(WIN32)
  set(LUA_LIB "${luajit_SOURCE_DIR}/src/lua51_$<CONFIG>.lib")
  add_custom_command(OUTPUT "${LUA_LIB}"
    COMMAND cmake -E env "CFLAGS=${ALL_C_COMPILE_OPTIONS_SPACED} /MT$<IF:$<CONFIG:Debug>,d,>" msvcbuild.bat static
                     && cmake -E copy "${luajit_SOURCE_DIR}/src/lua51.lib" "${LUA_LIB}"
                     && cmake -E touch_nocreate  "${LUA_LIB}"
                     WORKING_DIRECTORY  "${luajit_SOURCE_DIR}/src")
else()
  set(LUA_LIB "${luajit_SOURCE_DIR}/src/libluajit_$<CONFIG>.a")
  add_custom_command(OUTPUT "${LUA_LIB}"
                     COMMAND cmake -E env "CFLAGS=${ALL_C_COMPILE_OPTIONS_SPACED}" MACOSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET} make clean
                          && cmake -E env "CFLAGS=${ALL_C_COMPILE_OPTIONS_SPACED}" MACOSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET} make
                          && cmake -E copy "${luajit_SOURCE_DIR}/src/libluajit.a" "${LUA_LIB}"
                          && cmake -E touch_nocreate "${LUA_LIB}"
                     WORKING_DIRECTORY  "${luajit_SOURCE_DIR}")
endif()

add_library(lua INTERFACE "${LUA_LIB}")
target_include_directories(lua INTERFACE "${luajit_SOURCE_DIR}/src/")
target_link_libraries(lua INTERFACE "${LUA_LIB}")

