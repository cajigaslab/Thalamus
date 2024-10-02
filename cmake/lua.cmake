FetchContent_Declare(
  lua
  URL https://lua.org/ftp/lua-5.4.6.tar.gz
  URL_HASH SHA256=7d5ea1b9cb6aa0b59ca3dde1c6adcb57ef83a1ba8e5432c0ecd06bf439b3ad88)
FetchContent_MakeAvailable(lua)

add_library(lua
  "${lua_SOURCE_DIR}/src/lapi.c"
  "${lua_SOURCE_DIR}/src/lcode.c"
  "${lua_SOURCE_DIR}/src/lctype.c"
  "${lua_SOURCE_DIR}/src/ldebug.c"
  "${lua_SOURCE_DIR}/src/ldo.c"
  "${lua_SOURCE_DIR}/src/ldump.c"
  "${lua_SOURCE_DIR}/src/lfunc.c"
  "${lua_SOURCE_DIR}/src/lgc.c"
  "${lua_SOURCE_DIR}/src/llex.c"
  "${lua_SOURCE_DIR}/src/lmem.c"
  "${lua_SOURCE_DIR}/src/lobject.c"
  "${lua_SOURCE_DIR}/src/lopcodes.c"
  "${lua_SOURCE_DIR}/src/lparser.c"
  "${lua_SOURCE_DIR}/src/lstate.c"
  "${lua_SOURCE_DIR}/src/lstring.c"
  "${lua_SOURCE_DIR}/src/ltable.c"
  "${lua_SOURCE_DIR}/src/ltm.c"
  "${lua_SOURCE_DIR}/src/lundump.c"
  "${lua_SOURCE_DIR}/src/lvm.c"
  "${lua_SOURCE_DIR}/src/lzio.c"
  "${lua_SOURCE_DIR}/src/lauxlib.c"
  "${lua_SOURCE_DIR}/src/lbaselib.c"
  "${lua_SOURCE_DIR}/src/lcorolib.c"
  "${lua_SOURCE_DIR}/src/ldblib.c"
  "${lua_SOURCE_DIR}/src/liolib.c"
  "${lua_SOURCE_DIR}/src/lmathlib.c"
  "${lua_SOURCE_DIR}/src/loadlib.c"
  "${lua_SOURCE_DIR}/src/loslib.c"
  "${lua_SOURCE_DIR}/src/lstrlib.c"
  "${lua_SOURCE_DIR}/src/ltablib.c"
  "${lua_SOURCE_DIR}/src/lutf8lib.c"
  "${lua_SOURCE_DIR}/src/linit.c")
target_include_directories(lua PUBLIC "${lua_SOURCE_DIR}/src")
