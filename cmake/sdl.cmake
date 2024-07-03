FetchContent_Declare(
  sdl
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG release-2.28.5)
FetchContent_Populate(sdl)
file(MAKE_DIRECTORY ${sdl_BINARY_DIR}/Debug)
file(MAKE_DIRECTORY ${sdl_BINARY_DIR}/Release)

if(WIN32)
  set(SDL_LIB_FILES "${sdl_BINARY_DIR}/$<CONFIG>/install/lib/SDL2-static$<$<CONFIG:Debug>:d>.lib")
else()
  set(SDL_LIB_FILES "${sdl_BINARY_DIR}/$<CONFIG>/install/lib/libSDL2$<$<CONFIG:Debug>:d>.a")
endif()

add_custom_command(OUTPUT "${sdl_BINARY_DIR}/$<CONFIG>/CMakeCache.txt"
                   COMMAND 
                   cmake "${sdl_SOURCE_DIR}" -Wno-dev 
		      -DSDL_LIBSAMPLERATE=OFF
		      -DSDL_SNDIO=OFF
                      -DCMAKE_POSITION_INDEPENDENT_CODE=ON
                      -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
                      -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
                      -DCMAKE_LINKER=${CMAKE_LINKER}
                      -DBUILD_SHARED_LIBS=OFF
                      "-DCMAKE_CXX_FLAGS=${ALL_COMPILE_OPTIONS_SPACED}" 
                      "-DCMAKE_C_FLAGS=${ALL_COMPILE_OPTIONS_SPACED}"
                      "-DCMAKE_BUILD_TYPE=$<CONFIG>" "-DCMAKE_INSTALL_PREFIX=${sdl_BINARY_DIR}/$<CONFIG>/install"
                      -DCMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}
                      -DCMAKE_POLICY_DEFAULT_CMP0091=NEW
                      -DCMAKE_MSVC_RUNTIME_LIBRARY=${CMAKE_MSVC_RUNTIME_LIBRARY}
                      -DCMAKE_MSVC_RUNTIME_LIBRARY_DEFAULT=${CMAKE_MSVC_RUNTIME_LIBRARY}
                      -G "${CMAKE_GENERATOR}"
                   && cmake -E touch_nocreate "${sdl_BINARY_DIR}/$<CONFIG>/CMakeCache.txt"
                   WORKING_DIRECTORY "${sdl_BINARY_DIR}/$<CONFIG>")
add_custom_command(OUTPUT ${SDL_LIB_FILES}
                   DEPENDS "${sdl_BINARY_DIR}/$<CONFIG>/CMakeCache.txt"
                   COMMAND 
                   echo SDL BUILD
                   && cmake --build . --config "$<CONFIG>" --parallel ${CPU_COUNT}
                   && cmake --install . --config "$<CONFIG>"
                   && cmake -E touch_nocreate ${SDL_LIB_FILES}
                   WORKING_DIRECTORY ${sdl_BINARY_DIR}/$<CONFIG>)
      
set(SDL_INCLUDE "${sdl_BINARY_DIR}/$<CONFIG>/install/include/SDL2")
set(SDL_PKG_CONFIG_DIR "${sdl_BINARY_DIR}/$<CONFIG>/install/lib/pkgconfig")

add_library(sdl INTERFACE ${SDL_LIB_FILES})
target_link_libraries(sdl INTERFACE ${SDL_LIB_FILES})
if(WIN32)
  target_link_libraries(sdl INTERFACE User32.lib Gdi32.lib Setupapi.lib Advapi32.lib Imm32.lib Winmm.lib Shell32.lib Ole32.lib Oleaut32.lib Version.lib Shlwapi.lib Vfw32.lib)
else()
  target_link_libraries(sdl INTERFACE xcb xcb-shm xcb-xfixes xcb-shape)
endif()
target_include_directories(sdl INTERFACE "${SDL_INCLUDE}")

add_executable(sdl2-config src/sdl2-config.cpp)
target_link_libraries(sdl2-config absl::strings absl::str_format)
target_compile_definitions(sdl2-config PRIVATE "SDL_INCLUDE=\"${SDL_INCLUDE}\"" "SDL_LIB_FILES=\"${SDL_LIB_FILES}\"")
add_dependencies(sdl sdl2-config)
