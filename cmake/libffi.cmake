FetchContent_Declare(
  libffi 
  URL https://github.com/libffi/libffi/releases/download/v3.4.5/libffi-3.4.5.tar.gz
)
FetchContent_MakeAvailable()
file(MAKE_DIRECTORY "${libffi_BINARY_DIR}/Debug")
file(MAKE_DIRECTORY "${libffi_BINARY_DIR}/Release")

if(WIN32)
  if(EXISTS "C:\\MSYS2")
    set(MSYS2_ROOT "C:\\MSYS2")
  else()
    set(MSYS2_ROOT "C:\\MSYS64")
  endif()
  set(LIBFFI_ALL_COMPILE_OPTIONS_SPACED "${ALL_C_COMPILE_OPTIONS_SPACED}")
  string(PREPEND LIBFFI_ALL_COMPILE_OPTIONS_SPACED " ")
  string(REPLACE " /" " -" LIBFFI_ALL_COMPILE_OPTIONS_SPACED "${LIBFFI_ALL_COMPILE_OPTIONS_SPACED}")
  string(REPLACE "-MP" "" LIBFFI_ALL_COMPILE_OPTIONS_SPACED "${LIBFFI_ALL_COMPILE_OPTIONS_SPACED}")

  string(STRIP "${LIBFFI_ALL_COMPILE_OPTIONS_SPACED}" LIBFFI_ALL_COMPILE_OPTIONS_SPACED)
  string(APPEND LIBFFI_ALL_COMPILE_OPTIONS_SPACED " -FS")
  add_custom_command(
    OUTPUT "${libffi_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/Makefile"
    COMMAND
    ${MSYS2_ROOT}\\msys2_shell.cmd -here -use-full-path -no-start -defterm -c "'${libffi_SOURCE_DIR}/configure' --prefix='${libffi_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install' $<IF:$<CONFIG:Debug>,--enable-debug,> --disable-shared CC='${libffi_SOURCE_DIR}/msvcc.sh -m64 -MT$<IF:$<CONFIG:Debug>,d,> ${LIBFFI_ALL_COMPILE_OPTIONS_SPACED}' CXX='${libffi_SOURCE_DIR}/msvcc.sh -m64 -MT$<IF:$<CONFIG:Debug>,d,> ${LIBFFI_ALL_COMPILE_OPTIONS_SPACED}' LD=link CPP='${CMAKE_CXX_COMPILER} -nologo -EP' CPPFLAGS=-DFFI_BUILDING"
    && cmake -E touch_nocreate "${libffi_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/Makefile"
    WORKING_DIRECTORY "${libffi_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>")
else()
  string(REPLACE "-nostdinc++" "" LIBFFI_COMPILE_OPTIONS_SPACED "${ALL_C_COMPILE_OPTIONS_SPACED}")
  add_custom_command(
    OUTPUT "${libffi_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/Makefile"
    COMMAND 
    "${libffi_SOURCE_DIR}/configure" "CC=clang ${LIBFFI_COMPILE_OPTIONS_SPACED}" "CXX=clang ${LIBFFI_COMPILE_OPTIONS_SPACED}" --enable-static --disable-shared $<IF:$<CONFIG:Debug>,--enable-debug,> --prefix=${ffmpeg_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install
    && cmake -E touch_nocreate "${ffmpeg_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/Makefile"
    WORKING_DIRECTORY "${ffmpeg_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>")
endif()
