if("${SANITIZER}" STREQUAL thread)
  set(CAIRO_SANITIZER -Db_sanitize=thread)
elseif("${SANITIZER}" STREQUAL address)
  set(CAIRO_SANITIZER -Db_sanitize=address)
elseif("${SANITIZER}" STREQUAL memory)
  set(CAIRO_SANITIZER -Db_sanitize=memory)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  set(CAIRO_COMPILER CC=clang CXX=clang++)
endif()

if(WIN32)
  set(PIXMAN_PKG_CONFIG_ENV "PKG_CONFIG_PATH=${GLIB_PKGCONFIG_DIR};${ZLIB_PKG_CONFIG_DIR}")
else()
  set(PIXMAN_PKG_CONFIG_ENV "PKG_CONFIG_PATH=${GLIB_PKGCONFIG_DIR}:${ZLIB_PKG_CONFIG_DIR}")
endif()

FetchContent_Declare(
  pixman 
  URL https://cairographics.org/releases/pixman-0.43.4.tar.gz
  URL_HASH SHA512=08802916648bab51fd804fc3fd823ac2c6e3d622578a534052b657491c38165696d5929d03639c52c4f29d8850d676a909f0299d1a4c76a07df18a34a896e43d)
FetchContent_MakeAvailable(pixman)
file(MAKE_DIRECTORY "${pixman_BINARY_DIR}/Debug/install")
file(MAKE_DIRECTORY "${pixman_BINARY_DIR}/Release/install")

add_custom_command(
  OUTPUT "${pixman_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/build.ninja"
  COMMAND cmake -E env 
  ${CAIRO_COMPILER}
  "${PIXMAN_PKG_CONFIG_ENV}"
  "CFLAGS=${OSX_TARGET_PARAMETER}"
  meson setup "${pixman_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>"
  -Dtests=disabled ${CAIRO_SANITIZER} -Ddefault_library=static -Db_vscrt=static_from_buildtype 
  --prefix "${pixman_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install"
  --buildtype=$<IF:$<CONFIG:Debug>,debug,release>
  && cmake -E touch_nocreate "${pixman_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/build.ninja"
  WORKING_DIRECTORY "${pixman_SOURCE_DIR}")

if(WIN32 OR APPLE)
  set(PIXMAN_LIBRARIES "${pixman_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install/lib/libpixman-1.a")
  set(PIXMAN_PKGCONFIG_DIR "${pixman_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install/lib/pkgconfig")
else()
  set(PIXMAN_LIBRARIES "${pixman_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install/lib/x86_64-linux-gnu/libpixman-1.a")
  set(PIXMAN_PKGCONFIG_DIR "${pixman_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install/lib/x86_64-linux-gnu/pkgconfig")
endif()
set(PIXMAN_INCLUDE_DIRS "${pixman_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install/include/cairo")

add_custom_command(DEPENDS "${pixman_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/build.ninja"
  OUTPUT ${PIXMAN_LIBRARIES}
  COMMAND ninja install
  WORKING_DIRECTORY "${pixman_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>")

FetchContent_Declare(
  cairo 
  URL https://www.cairographics.org/releases/cairo-1.18.0.tar.xz
  URL_HASH SHA256=243a0736b978a33dee29f9cca7521733b78a65b5418206fef7bd1c3d4cf10b64)
FetchContent_MakeAvailable(cairo)
file(MAKE_DIRECTORY "${cairo_BINARY_DIR}/Debug/install")
file(MAKE_DIRECTORY "${cairo_BINARY_DIR}/Release/install")

if(WIN32)
  set(CAIRO_PKG_CONFIG_ENV "PKG_CONFIG_PATH=${GLIB_PKGCONFIG_DIR};${ZLIB_PKG_CONFIG_DIR};${PIXMAN_PKGCONFIG_DIR}")
else()
  set(CAIRO_PKG_CONFIG_ENV "PKG_CONFIG_PATH=${GLIB_PKGCONFIG_DIR}:${ZLIB_PKG_CONFIG_DIR}:${PIXMAN_PKGCONFIG_DIR}")
endif()

add_custom_command(
  DEPENDS ${PIXMAN_LIBRARIES}
  OUTPUT "${cairo_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/build.ninja"
  COMMAND cmake -E env 
  ${CAIRO_COMPILER}
  "${CAIRO_PKG_CONFIG_ENV}"
  "CFLAGS=${OSX_TARGET_PARAMETER}"
  meson setup "${cairo_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>"
  -Dtests=disabled ${CAIRO_SANITIZER} -Ddefault_library=static -Dpng=disabled -Dfontconfig=disabled -Dfreetype=disabled -Db_vscrt=static_from_buildtype 
  --prefix "${cairo_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install"
  --buildtype=$<IF:$<CONFIG:Debug>,debug,release>
  && cmake -E touch_nocreate "${cairo_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/build.ninja"
  WORKING_DIRECTORY "${cairo_SOURCE_DIR}")

if(WIN32 OR APPLE)
  set(CAIRO_LIBRARIES "${cairo_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install/lib/libcairo.a")
  set(CAIRO_PKGCONFIG_DIR "${cairo_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install/lib/pkgconfig")
else()
  set(CAIRO_LIBRARIES "${cairo_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install/lib/x86_64-linux-gnu/libcairo.a")
  set(CAIRO_PKGCONFIG_DIR "${cairo_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install/lib/x86_64-linux-gnu/pkgconfig")
endif()
set(CAIRO_INCLUDE_DIRS "${cairo_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install/include/cairo")

add_custom_command(DEPENDS "${cairo_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/build.ninja"
  OUTPUT ${CAIRO_LIBRARIES}
  COMMAND ninja install
  WORKING_DIRECTORY "${cairo_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>")

add_library(cairo INTERFACE ${CAIRO_LIBRARIES} ${PIXMAN_LIBRARIES})
target_include_directories(cairo INTERFACE ${CAIRO_INCLUDE_DIRS} ${PIXMAN_INCLUDE_DIRS})
target_link_libraries(cairo INTERFACE ${CAIRO_LIBRARIES} ${PIXMAN_LIBRARIES})
target_compile_definitions(cairo INTERFACE CAIRO_WIN32_STATIC_BUILD)
