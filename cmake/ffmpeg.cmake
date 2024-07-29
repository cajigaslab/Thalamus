FetchContent_Declare(
  ffmpeg 
  GIT_REPOSITORY https://github.com/FFmpeg/FFmpeg.git
  GIT_TAG        n6.1
)
FetchContent_Populate(ffmpeg)
file(MAKE_DIRECTORY "${ffmpeg_BINARY_DIR}/Debug/Modules")
file(MAKE_DIRECTORY "${ffmpeg_BINARY_DIR}/Release/Modules")

execute_process(COMMAND git apply "${CMAKE_SOURCE_DIR}/patches/ffmpeg" WORKING_DIRECTORY "${ffmpeg_SOURCE_DIR}")

set(FFTOOL_OBJECTS 
    "${ffmpeg_BINARY_DIR}/$<CONFIG>/fftools/ffmpeg_dec.o"
    "${ffmpeg_BINARY_DIR}/$<CONFIG>/fftools/ffmpeg_demux.o"
    "${ffmpeg_BINARY_DIR}/$<CONFIG>/fftools/ffmpeg_enc.o"
    "${ffmpeg_BINARY_DIR}/$<CONFIG>/fftools/ffmpeg_filter.o"
    "${ffmpeg_BINARY_DIR}/$<CONFIG>/fftools/ffmpeg_hw.o"
    "${ffmpeg_BINARY_DIR}/$<CONFIG>/fftools/ffmpeg_mux.o"
    "${ffmpeg_BINARY_DIR}/$<CONFIG>/fftools/ffmpeg_mux_init.o"
    "${ffmpeg_BINARY_DIR}/$<CONFIG>/fftools/ffmpeg_opt.o"
    "${ffmpeg_BINARY_DIR}/$<CONFIG>/fftools/objpool.o"
    "${ffmpeg_BINARY_DIR}/$<CONFIG>/fftools/sync_queue.o"
    "${ffmpeg_BINARY_DIR}/$<CONFIG>/fftools/thread_queue.o"
    "${ffmpeg_BINARY_DIR}/$<CONFIG>/fftools/cmdutils.o"
    "${ffmpeg_BINARY_DIR}/$<CONFIG>/fftools/opt_common.o")

if(WIN32)
  if(EXISTS "C:\\MSYS2")
    set(MSYS2_ROOT "C:\\MSYS2")
  else()
    set(MSYS2_ROOT "C:\\MSYS64")
  endif()
  set(FFMPEG_ALL_COMPILE_OPTIONS_SPACED "${ALL_C_COMPILE_OPTIONS_SPACED}")
  string(PREPEND FFMPEG_ALL_COMPILE_OPTIONS_SPACED " ")
  string(REPLACE " /" " -" FFMPEG_ALL_COMPILE_OPTIONS_SPACED "${FFMPEG_ALL_COMPILE_OPTIONS_SPACED}")
  string(REPLACE "-MP" "" FFMPEG_ALL_COMPILE_OPTIONS_SPACED "${FFMPEG_ALL_COMPILE_OPTIONS_SPACED}")


  string(STRIP "${FFMPEG_ALL_COMPILE_OPTIONS_SPACED}" FFMPEG_ALL_COMPILE_OPTIONS_SPACED)
  if("${CMAKE_CXX_COMPILER_ID}" MATCHES "MSVC")
    string(APPEND FFMPEG_ALL_COMPILE_OPTIONS_SPACED " -FS")
    add_custom_command(
      OUTPUT "${ffmpeg_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/Makefile"
      DEPENDS sdl
      COMMAND
      ${MSYS2_ROOT}\\msys2_shell.cmd -here -use-full-path -no-start -defterm -c "export 'PKG_CONFIG_PATH=${SDL_PKG_CONFIG_DIR}' && '${CMAKE_SOURCE_DIR}/config_ffmpeg_msvc.bash' '${ffmpeg_SOURCE_DIR}/configure' '${ffmpeg_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install' '-MT$<IF:$<CONFIG:Debug>,d,> ${FFMPEG_ALL_COMPILE_OPTIONS_SPACED}' $<IF:$<CONFIG:Debug>,--enable-debug,>"
      && cmake -E touch_nocreate "${ffmpeg_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/Makefile"
      WORKING_DIRECTORY "${ffmpeg_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>")
  else()
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
  string(REPLACE "-nostdinc++" "" FFMPEG_COMPILE_OPTIONS_SPACED "${ALL_C_COMPILE_OPTIONS_SPACED}")
  add_custom_command(
    OUTPUT "${ffmpeg_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/Makefile"
    DEPENDS zlib_processed sdl
    COMMAND cmake -E env 
    "PKG_CONFIG_PATH=${ZLIB_PKG_CONFIG_DIR}:${SDL_PKG_CONFIG_DIR}"
    "${ffmpeg_SOURCE_DIR}/configure" --cc=clang "--extra-cflags=${FFMPEG_COMPILE_OPTIONS_SPACED} ${OSX_TARGET_PARAMETER}" --arch=x86_64 --enable-static --disable-shared --enable-pic --disable-sndio $<IF:$<CONFIG:Debug>,--enable-debug,> --prefix=${ffmpeg_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install
    && cmake -E touch_nocreate "${ffmpeg_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/Makefile"
    WORKING_DIRECTORY "${ffmpeg_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>")
  set(FFMPEG_MAKE_COMMAND make -j ${CPU_COUNT} && make install)
endif()

if(WIN32)
  set(FFMPEG_OUTPUT_LIBRARIES
    "${ffmpeg_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install/lib/libavcodec.a"
    "${ffmpeg_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install/lib/libavdevice.a"
    "${ffmpeg_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install/lib/libavfilter.a"
    "${ffmpeg_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install/lib/libavformat.a"
    "${ffmpeg_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install/lib/libavutil.a"
    "${ffmpeg_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install/lib/libswresample.a"
    "${ffmpeg_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install/lib/libswscale.a")
  set(FFMPEG_LIBRARIES "${FFMPEG_OUTPUT_LIBRARIES}"
    Ws2_32.lib Secur32.lib Bcrypt.lib Mfplat.lib Ole32.lib User32.lib dxguid.lib uuid.lib Mfuuid.lib strmiids.lib Kernel32.lib Psapi.lib)
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
set(FFMPEG_PKG_CONFIG_DIR "${ffmpeg_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install/lib/pkgconfig")

add_custom_command(
  OUTPUT "${FFMPEG_OUTPUT_LIBRARIES}" ${FFTOOL_OBJECTS} "${ffmpeg_BINARY_DIR}/$<CONFIG>/ffbuild/config.mak" 
  DEPENDS "${ffmpeg_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/Makefile"
  COMMAND ${FFMPEG_MAKE_COMMAND}
  && cmake -E touch_nocreate "${ffmpeg_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install/lib/libavcodec.a"
  WORKING_DIRECTORY "${ffmpeg_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>")

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


if(WIN32)
  set(THALAMUS_EXPORT "__declspec(dllexport)")
  set(FFMPEG_OUT_ARG "-Fo")
else()
  set(THALAMUS_EXPORT "__attribute__((visibility(\"default\")))")
  set(FFMPEG_OUT_ARG "-o")
endif()

file(READ "${ffmpeg_SOURCE_DIR}/fftools/ffmpeg.c" FFMPEG_C_SOURCE)
string(REPLACE "int main" "int ffmpeg_main_impl(int argc, char** argv);int ffmpeg_main_impl" THALAMUS_FFMPEG_C_SOURCE "${FFMPEG_C_SOURCE}")
file(WRITE "${CMAKE_BINARY_DIR}/thalamus_ffmpeg.c" "${THALAMUS_FFMPEG_C_SOURCE}")

file(READ "${ffmpeg_SOURCE_DIR}/fftools/ffprobe.c" FFPROBE_C_SOURCE)
string(REPLACE "int main" "int ffprobe_main_impl(int argc, char** argv);int ffprobe_main_impl" THALAMUS_FFPROBE_C_SOURCE "${FFPROBE_C_SOURCE}")
string(REPLACE "void show_help_default" "void ffprobe_show_help_default(const char* opt, const char* arg);void ffprobe_show_help_default" 
	THALAMUS_FFPROBE_C_SOURCE "${THALAMUS_FFPROBE_C_SOURCE}")
string(REPLACE "show_help_default" "ffprobe_show_help_default" THALAMUS_FFPROBE_C_SOURCE "${THALAMUS_FFPROBE_C_SOURCE}")
string(REPLACE "program_name" "ffprobe_program_name" THALAMUS_FFPROBE_C_SOURCE "${THALAMUS_FFPROBE_C_SOURCE}")
string(REPLACE "program_birth_year" "ffprobe_program_birth_year" THALAMUS_FFPROBE_C_SOURCE "${THALAMUS_FFPROBE_C_SOURCE}")
file(WRITE "${CMAKE_BINARY_DIR}/thalamus_ffprobe.c" "${THALAMUS_FFPROBE_C_SOURCE}")

file(READ "${ffmpeg_SOURCE_DIR}/fftools/ffplay.c" FFPLAY_C_SOURCE)
string(REPLACE "int main" "int ffplay_main_impl(int argc, char** argv);int ffplay_main_impl" THALAMUS_FFPLAY_C_SOURCE "${FFPLAY_C_SOURCE}")
string(REPLACE "void show_help_default" "void ffplay_show_help_default(const char* opt, const char* arg);void ffplay_show_help_default" THALAMUS_FFPLAY_C_SOURCE "${THALAMUS_FFPLAY_C_SOURCE}")
string(REPLACE "program_name" "ffplay_program_name" THALAMUS_FFPLAY_C_SOURCE "${THALAMUS_FFPLAY_C_SOURCE}")
string(REPLACE "program_birth_year" "ffplay_program_birth_year" THALAMUS_FFPLAY_C_SOURCE "${THALAMUS_FFPLAY_C_SOURCE}")
file(WRITE "${CMAKE_BINARY_DIR}/thalamus_ffplay.c" "${THALAMUS_FFPLAY_C_SOURCE}")

add_custom_command(
  OUTPUT "${CMAKE_BINARY_DIR}/ffmpeg_$<CONFIG>.rsp"
  DEPENDS "${ffmpeg_BINARY_DIR}/$<CONFIG>/ffbuild/config.mak" 
  COMMAND cmake "-DCMAKE_BUILD_TYPE=$<CONFIG>" "-Dffmpeg_SOURCE_DIR=${ffmpeg_SOURCE_DIR}" "-Dffmpeg_BINARY_DIR=${ffmpeg_BINARY_DIR}" -P "${CMAKE_SOURCE_DIR}/get_ffmpeg_args.cmake"
  && cmake -E touch_nocreate "${CMAKE_BINARY_DIR}/ffmpeg_$<CONFIG>.rsp"
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

add_custom_command(
  OUTPUT "${CMAKE_BINARY_DIR}/thalamus_ffmpeg_$<CONFIG>.o"
  DEPENDS "${CMAKE_BINARY_DIR}/ffmpeg_$<CONFIG>.rsp"
  COMMAND "${CMAKE_C_COMPILER}" -c ${FFMPEG_OUT_ARG}thalamus_ffmpeg_$<CONFIG>.o "-I${ffmpeg_SOURCE_DIR}/fftools" "-I${ffmpeg_SOURCE_DIR}" "-I${ffmpeg_BINARY_DIR}/$<CONFIG>" "@${CMAKE_BINARY_DIR}/ffmpeg_$<CONFIG>.rsp" "${CMAKE_BINARY_DIR}/thalamus_ffmpeg.c"
  && cmake -E touch_nocreate "${CMAKE_BINARY_DIR}/thalamus_ffmpeg_$<CONFIG>.o"
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

add_custom_command(
  OUTPUT "${CMAKE_BINARY_DIR}/thalamus_ffprobe_$<CONFIG>.o"
  DEPENDS "${CMAKE_BINARY_DIR}/ffmpeg_$<CONFIG>.rsp"
  COMMAND "${CMAKE_C_COMPILER}" -c ${FFMPEG_OUT_ARG}thalamus_ffprobe_$<CONFIG>.o "-I${ffmpeg_SOURCE_DIR}/fftools" "-I${ffmpeg_SOURCE_DIR}" "-I${ffmpeg_BINARY_DIR}/$<CONFIG>" "@${CMAKE_BINARY_DIR}/ffmpeg_$<CONFIG>.rsp" "${CMAKE_BINARY_DIR}/thalamus_ffprobe.c"
  && cmake -E touch_nocreate "${CMAKE_BINARY_DIR}/thalamus_ffprobe_$<CONFIG>.o"
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

add_custom_command(
  OUTPUT "${CMAKE_BINARY_DIR}/thalamus_ffplay_$<CONFIG>.o"
  DEPENDS "${CMAKE_BINARY_DIR}/ffmpeg_$<CONFIG>.rsp"
  COMMAND "${CMAKE_C_COMPILER}" -c ${FFMPEG_OUT_ARG}thalamus_ffplay_$<CONFIG>.o "-I${SDL_INCLUDE}" "-I${ffmpeg_SOURCE_DIR}/fftools" "-I${ffmpeg_SOURCE_DIR}" "-I${ffmpeg_BINARY_DIR}/$<CONFIG>" "@${CMAKE_BINARY_DIR}/ffmpeg_$<CONFIG>.rsp" "${CMAKE_BINARY_DIR}/thalamus_ffplay.c"
  && cmake -E touch_nocreate "${CMAKE_BINARY_DIR}/thalamus_ffplay_$<CONFIG>.o"
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

add_library(thalamus_ffmpeg 
    "${CMAKE_BINARY_DIR}/thalamus_ffmpeg_$<CONFIG>.o"
    "${CMAKE_BINARY_DIR}/thalamus_ffprobe_$<CONFIG>.o"
    "${CMAKE_BINARY_DIR}/thalamus_ffplay_$<CONFIG>.o"
    ${FFTOOL_OBJECTS})
target_link_libraries(thalamus_ffmpeg PUBLIC ffmpeg)
set_target_properties(thalamus_ffmpeg PROPERTIES LINKER_LANGUAGE C)
add_dependencies(thalamus_ffmpeg ffmpeg)

