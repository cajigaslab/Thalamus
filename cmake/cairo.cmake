if("${SANITIZER}" STREQUAL thread)
  set(CAIRO_SANITIZER -Db_sanitize=thread)
elseif("${SANITIZER}" STREQUAL address)
  set(CAIRO_SANITIZER -Db_sanitize=address)
elseif("${SANITIZER}" STREQUAL memory)
  set(CAIRO_SANITIZER -Db_sanitize=memory)
endif()

FetchContent_Declare(
  cairo 
  URL https://www.cairographics.org/releases/cairo-1.18.0.tar.xz
  URL_HASH SHA256=243a0736b978a33dee29f9cca7521733b78a65b5418206fef7bd1c3d4cf10b64)
FetchContent_Populate(cairo)
file(MAKE_DIRECTORY "${cairo_BINARY_DIR}/Debug/install")
file(MAKE_DIRECTORY "${cairo_BINARY_DIR}/Release/install")

add_custom_command(
  OUTPUT "${cairo_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/build.ninja"
  COMMAND cmake -E env 
  CC=clang 
  CXX=clang++
  meson setup "${cairo_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>"
  -Dtests=disabled ${CAIRO_SANITIZER} -Ddefault_library=static -Dpng=disabled -Dfontconfig=disabled -Dfreetype=disabled -Db_vscrt=static_from_buildtype 
  --prefix "${cairo_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install"
  --buildtype=$<IF:$<CONFIG:Debug>,debug,release>
  && cmake -E touch_nocreate "${cairo_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/build.ninja"
  WORKING_DIRECTORY "${cairo_SOURCE_DIR}")

set(CAIRO_LIBRARIES "${cairo_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install/lib/x86_64-linux-gnu/libcairo.a"
                    "${cairo_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install/lib/x86_64-linux-gnu/libpixman-1.a")
set(CAIRO_INCLUDE_DIRS "${cairo_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install/include/cairo")
set(CAIRO_PKGCONFIG_DIR "${cairo_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install/lib/x86_64-linux-gnu/pkgconfig")

add_custom_command(DEPENDS "${cairo_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/build.ninja"
  OUTPUT ${CAIRO_LIBRARIES}
  COMMAND ninja install
  WORKING_DIRECTORY "${cairo_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>")

add_library(cairo INTERFACE ${CAIRO_LIBRARIES})
target_include_directories(cairo INTERFACE ${CAIRO_INCLUDE_DIRS})
target_link_libraries(cairo INTERFACE ${CAIRO_LIBRARIES})
