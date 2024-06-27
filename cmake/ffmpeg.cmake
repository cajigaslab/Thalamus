FetchContent_Declare(
  ffmpeg 
  GIT_REPOSITORY https://Killamanjarl:ATBBeJrsn9LYrVtLMPayDv9kUZ4607A4599C@bitbucket.org/bijanlab/ffmpeg.git
  GIT_TAG        pesaran/6.1
)
FetchContent_Populate(ffmpeg)
file(MAKE_DIRECTORY "${ffmpeg_BINARY_DIR}/Debug/Modules")
file(MAKE_DIRECTORY "${ffmpeg_BINARY_DIR}/Release/Modules")


if(WIN32)
  if(EXISTS "C:\\MSYS2")
    set(MSYS2_ROOT "C:\\MSYS2")
  else()
    set(MSYS2_ROOT "C:\\MSYS64")
  endif()
  set(FFMPEG_ALL_COMPILE_OPTIONS_SPACED "${ALL_COMPILE_OPTIONS_SPACED}")
  string(PREPEND FFMPEG_ALL_COMPILE_OPTIONS_SPACED " ")
  string(REPLACE " /" " -" FFMPEG_ALL_COMPILE_OPTIONS_SPACED "${FFMPEG_ALL_COMPILE_OPTIONS_SPACED}")
  string(REPLACE "-MP" "" FFMPEG_ALL_COMPILE_OPTIONS_SPACED "${FFMPEG_ALL_COMPILE_OPTIONS_SPACED}")


  string(STRIP "${FFMPEG_ALL_COMPILE_OPTIONS_SPACED}" FFMPEG_ALL_COMPILE_OPTIONS_SPACED)
  if("${CMAKE_CXX_COMPILER_ID}" MATCHES "MSVC")
    string(APPEND FFMPEG_ALL_COMPILE_OPTIONS_SPACED " -FS")
    set(FFMPEG_LIB_PREFIX lib)
    add_custom_command(
      OUTPUT "${ffmpeg_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/Makefile"
      DEPENDS sdl
      COMMAND
      cmake -E env "PATH=$<TARGET_FILE_DIR:sdl2-config>;$ENV{PATH}"
      ${MSYS2_ROOT}\\msys2_shell.cmd -here -use-full-path -no-start -defterm -c "'${CMAKE_SOURCE_DIR}/config_ffmpeg_msvc.bash' '${ffmpeg_SOURCE_DIR}/configure' '${ffmpeg_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install' '-MT$<IF:$<CONFIG:Debug>,d,> ${FFMPEG_ALL_COMPILE_OPTIONS_SPACED}' $<IF:$<CONFIG:Debug>,--enable-debug,>"
      && cmake -E touch_nocreate "${ffmpeg_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/Makefile"
      WORKING_DIRECTORY "${ffmpeg_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>")
  else()
    set(FFMPEG_LIB_PREFIX)
    add_custom_command(
      OUTPUT "${ffmpeg_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/Makefile"
      DEPENDS sdl
      COMMAND
      ${MSYS2_ROOT}\\msys2_shell.cmd -here -use-full-path -no-start -defterm -c "'${CMAKE_SOURCE_DIR}/config_ffmpeg_clang.bash' '${ffmpeg_SOURCE_DIR}/configure' '${ffmpeg_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install' '${FFMPEG_ALL_COMPILE_OPTIONS_SPACED}' $<IF:$<CONFIG:Debug>,--enable-debug,>"
      && cmake -E touch_nocreate "${ffmpeg_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/Makefile"
      WORKING_DIRECTORY "${ffmpeg_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>")
  endif()
  set(FFMPEG_MAKE_COMMAND ${MSYS2_ROOT}\\msys2_shell.cmd -here -use-full-path -no-start -defterm -c \"${CMAKE_SOURCE_DIR}/make_ffmpeg.bash ${CPU_COUNT}\")
else()
  string(REPLACE "-nostdinc++" "" FFMPEG_COMPILE_OPTIONS_SPACED "${ALL_COMPILE_OPTIONS_SPACED}")
  add_custom_command(
    OUTPUT "${ffmpeg_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/Makefile"
    DEPENDS zlib_processed sdl
    COMMAND cmake -E env 
    "PKG_CONFIG_PATH=${ZLIB_PKG_CONFIG_DIR}:${SDL_PKG_CONFIG_DIR}"
    "${ffmpeg_SOURCE_DIR}/configure" --cc=clang "--extra-cflags=${FFMPEG_COMPILE_OPTIONS_SPACED} ${OSX_TARGET_PARAMETER}" --arch=x86_64 --enable-static --disable-shared $<IF:$<CONFIG:Debug>,--enable-debug,> --prefix=${ffmpeg_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install
    && cmake -E touch_nocreate "${ffmpeg_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/Makefile"
    WORKING_DIRECTORY "${ffmpeg_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>")
  set(FFMPEG_MAKE_COMMAND make -j ${CPU_COUNT} && make install)
endif()

if(WIN32)
  set(FFMPEG_OUTPUT_LIBRARIES
    "${ffmpeg_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install/lib/${FFMPEG_LIB_PREFIX}avcodec.lib"
    "${ffmpeg_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install/lib/${FFMPEG_LIB_PREFIX}avdevice.lib"
    "${ffmpeg_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install/lib/${FFMPEG_LIB_PREFIX}avfilter.lib"
    "${ffmpeg_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install/lib/${FFMPEG_LIB_PREFIX}avformat.lib"
    "${ffmpeg_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install/lib/${FFMPEG_LIB_PREFIX}avutil.lib"
    "${ffmpeg_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install/lib/${FFMPEG_LIB_PREFIX}swresample.lib"
    "${ffmpeg_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install/lib/${FFMPEG_LIB_PREFIX}swscale.lib")
  set(FFMPEG_LIBRARIES "${FFMPEG_OUTPUT_LIBRARIES}"
        Ws2_32.lib Secur32.lib Bcrypt.lib Mfplat.lib Ole32.lib User32.lib dxguid.lib uuid.lib Mfuuid.lib strmiids.lib)
  set(FFMPEG_EXECUTABLE "${ffmpeg_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install/bin/ffmpeg.exe")
  set(FFPLAY_EXECUTABLE "${ffmpeg_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install/bin/ffplay.exe")
  set(FFPROBE_EXECUTABLE "${ffmpeg_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install/bin/ffprobe.exe")
else()
  set(FFMPEG_OUTPUT_LIBRARIES
    "${ffmpeg_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install/lib/libavcodec.a"
    "${ffmpeg_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install/lib/libavdevice.a"
    "${ffmpeg_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install/lib/libavfilter.a"
    "${ffmpeg_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install/lib/libavformat.a"
    "${ffmpeg_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install/lib/libavutil.a"
    "${ffmpeg_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install/lib/libswresample.a"
    "${ffmpeg_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install/lib/libswscale.a")
  set(FFMPEG_LIBRARIES "${FFMPEG_OUTPUT_LIBRARIES}")
  set(FFMPEG_EXECUTABLE "${ffmpeg_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install/bin/ffmpeg")
  set(FFPLAY_EXECUTABLE "${ffmpeg_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install/bin/ffplay")
  set(FFPROBE_EXECUTABLE "${ffmpeg_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install/bin/ffprobe")
endif()

if(WIN32)
  add_custom_command(
    OUTPUT "${FFMPEG_OUTPUT_LIBRARIES}"
    DEPENDS "${ffmpeg_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/Makefile"
    COMMAND ${FFMPEG_MAKE_COMMAND}
    && cmake -E touch_nocreate "${ffmpeg_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install/lib/${FFMPEG_LIB_PREFIX}avcodec.a"
    && cmake "-DFILES=${ffmpeg_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install/lib/lib*.a" -P "${CMAKE_SOURCE_DIR}/a_to_lib.cmake"
    WORKING_DIRECTORY "${ffmpeg_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>")
else()
  add_custom_command(
    OUTPUT "${FFMPEG_OUTPUT_LIBRARIES}"
    DEPENDS "${ffmpeg_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/Makefile"
    COMMAND ${FFMPEG_MAKE_COMMAND}
    && cmake -E touch_nocreate "${ffmpeg_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install/lib/libavcodec.a"
    WORKING_DIRECTORY "${ffmpeg_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>")
endif()

set(FFMPEG_FOUND 1)
set(FFMPEG_INCLUDE_DIRS
  "${ffmpeg_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install/include")
string(REPLACE "$<IF:$<CONFIG:Debug>,Debug,Release>" "Debug" FFMPEG_INCLUDE_DIRS_DEBUG "${FFMPEG_INCLUDE_DIRS}")
string(REPLACE "$<IF:$<CONFIG:Debug>,Debug,Release>" "Release" FFMPEG_INCLUDE_DIRS_RELEASE "${FFMPEG_INCLUDE_DIRS}")
string(REPLACE "$<IF:$<CONFIG:Debug>,Debug,Release>" "Debug" FFMPEG_LIBRARIES_DEBUG "${FFMPEG_LIBRARIES}")
string(REPLACE "$<IF:$<CONFIG:Debug>,Debug,Release>" "Release" FFMPEG_LIBRARIES_RELEASE "${FFMPEG_LIBRARIES}")




set(FIND_FFMPEG "")
set(FIND_FFMPEG "${FIND_FFMPEG}execute_process(COMMAND ${ffmpeg_BINARY_DIR}/Debug/install/bin/ffmpeg -version\n")
set(FIND_FFMPEG "${FIND_FFMPEG}                OUTPUT_VARIABLE FFMPEG_OUTPUT)\n")
set(FIND_FFMPEG "${FIND_FFMPEG}string(REGEX MATCHALL \"lib[^/]+\" VERSIONS \"\${FFMPEG_OUTPUT}\")\n")
set(FIND_FFMPEG "${FIND_FFMPEG}foreach(line \${VERSIONS})\n")
set(FIND_FFMPEG "${FIND_FFMPEG}  string(REGEX REPLACE \"[a-z ]\" \"\" version \${line})\n")
set(FIND_FFMPEG "${FIND_FFMPEG}  string(REGEX REPLACE \"[ .0-9]\" \"\" lib \${line})\n")
set(FIND_FFMPEG "${FIND_FFMPEG}  set(FFMPEG_\${lib}_VERSION \${version})\n")
set(FIND_FFMPEG "${FIND_FFMPEG}  message(\"FFMPEG_\${lib}_VERSION \${FFMPEG_\${lib}_VERSION}\")\n")
set(FIND_FFMPEG "${FIND_FFMPEG}endforeach()\n")
set(FIND_FFMPEG "${FIND_FFMPEG}set(FFMPEG_FOUND 1)\n")
set(FIND_FFMPEG "${FIND_FFMPEG}set(FFMPEG_INCLUDE_DIRS \"${FFMPEG_INCLUDE_DIRS_DEBUG}\")\n")
set(FIND_FFMPEG "${FIND_FFMPEG}set(FFMPEG_LIBRARIES \"${FFMPEG_LIBRARIES_DEBUG}\")\n")
file(WRITE "${ffmpeg_BINARY_DIR}/Debug/Modules/FindFFMPEG.cmake" "${FIND_FFMPEG}")
set(FIND_FFMPEG "")
set(FIND_FFMPEG "${FIND_FFMPEG}execute_process(COMMAND ${ffmpeg_BINARY_DIR}/Release/install/bin/ffmpeg -version\n")
set(FIND_FFMPEG "${FIND_FFMPEG}                OUTPUT_VARIABLE FFMPEG_OUTPUT)\n")
set(FIND_FFMPEG "${FIND_FFMPEG}string(REGEX MATCHALL \"lib[^/]+\" VERSIONS \"\${FFMPEG_OUTPUT}\")\n")
set(FIND_FFMPEG "${FIND_FFMPEG}foreach(line \${VERSIONS})\n")
set(FIND_FFMPEG "${FIND_FFMPEG}  string(REGEX REPLACE \"[a-z ]\" \"\" version \${line})\n")
set(FIND_FFMPEG "${FIND_FFMPEG}  string(REGEX REPLACE \"[ .0-9]\" \"\" lib \${line})\n")
set(FIND_FFMPEG "${FIND_FFMPEG}  set(FFMPEG_\${lib}_VERSION \${version})\n")
set(FIND_FFMPEG "${FIND_FFMPEG}  message(\"FFMPEG_\${lib}_VERSION \${FFMPEG_\${lib}_VERSION}\")\n")
set(FIND_FFMPEG "${FIND_FFMPEG}endforeach()\n")
set(FIND_FFMPEG "${FIND_FFMPEG}set(FFMPEG_FOUND 1)\n")
set(FIND_FFMPEG "${FIND_FFMPEG}set(FFMPEG_INCLUDE_DIRS \"${FFMPEG_INCLUDE_DIRS_RELEASE}\")\n")
set(FIND_FFMPEG "${FIND_FFMPEG}set(FFMPEG_LIBRARIES \"${FFMPEG_LIBRARIES_RELEASE}\")\n")
file(WRITE "${ffmpeg_BINARY_DIR}/Release/Modules/FindFFMPEG.cmake" "${FIND_FFMPEG}")

list(GET FFMPEG_LIBRARIES 0 FIRST_FFMPEG_LIB)
message("FIRST_FFMPEG_LIB ${FIRST_FFMPEG_LIB} ${FFMPEG_LIBRARIES}")

add_library(ffmpeg INTERFACE "${FIRST_FFMPEG_LIB}")
target_include_directories(ffmpeg INTERFACE ${FFMPEG_INCLUDE_DIRS})
target_link_libraries(ffmpeg INTERFACE ${FFMPEG_LIBRARIES})
if(WIN32)
elseif(APPLE)
  target_link_libraries(ffmpeg INTERFACE bz2)
else()
  target_link_libraries(ffmpeg INTERFACE va X11 lzma va-drm va-x11 vdpau Xext Xv asound bz2)
endif()
