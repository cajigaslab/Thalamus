if("${SANITIZER}" STREQUAL thread)
  set(GLIB_SANITIZER -Db_sanitize=thread)
elseif("${SANITIZER}" STREQUAL address)
  set(GLIB_SANITIZER -Db_sanitize=address)
elseif("${SANITIZER}" STREQUAL memory)
  set(GLIB_SANITIZER -Db_sanitize=memory)
endif()

FetchContent_Declare(
  glib 
  GIT_REPOSITORY https://gitlab.gnome.org/GNOME/glib.git
  GIT_TAG        2.81.0
)
FetchContent_Populate(glib)
file(MAKE_DIRECTORY "${glib_BINARY_DIR}/Debug/install")
file(MAKE_DIRECTORY "${glib_BINARY_DIR}/Release/install")

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  set(GLIB_COMPILER CC=clang CXX=clang++)
endif()

add_custom_command(
  OUTPUT "${glib_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/build.ninja"
  COMMAND cmake -E env 
  ${GLIB_COMPILER}
  meson setup "${glib_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>"
    -Dtests=false ${GLIB_SANITIZER} -Dselinux=disabled -Dlibmount=disabled -Db_lundef=false -Ddefault_library=static -Db_vscrt=static_from_buildtype
    --prefix "${glib_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install"
    --buildtype=$<IF:$<CONFIG:Debug>,debug,release>
  && cmake -E touch_nocreate "${glib_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/build.ninja"
  WORKING_DIRECTORY "${glib_SOURCE_DIR}")

if(APPLE OR WIN32)
  set(GLIB_LIB_DIR "${glib_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install/lib")
else()
  set(GLIB_LIB_DIR "${glib_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install/lib/x86_64-linux-gnu")
endif()

set(GLIB_LIBRARIES ${GLIB_LIB_DIR}/libgio-2.0.a
                   ${GLIB_LIB_DIR}/libgmodule-2.0.a
                   ${GLIB_LIB_DIR}/libgobject-2.0.a
                   ${GLIB_LIB_DIR}/libgthread-2.0.a
                   ${GLIB_LIB_DIR}/libglib-2.0.a)
set(GLIB_INCLUDE_DIRS ${glib_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/install/include)
set(GLIB_PKGCONFIG_DIR "${GLIB_LIB_DIR}/pkgconfig")

add_custom_command(DEPENDS ${glib_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/build.ninja
  OUTPUT ${GLIB_LIBRARIES}
  COMMAND ninja install
  WORKING_DIRECTORY "${glib_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>")

add_library(glib INTERFACE ${GLIB_LIBRARIES})
target_include_directories(glib INTERFACE ${GLIB_INCLUDE_DIRS})
target_link_libraries(glib INTERFACE ${GLIB_LIBRARIES})

string(REPLACE "$<IF:$<CONFIG:Debug>,Debug,Release>" "Debug" GLIB_INCLUDE_DIRS_DEBUG "${GLIB_INCLUDE_DIRS}")
string(REPLACE "$<IF:$<CONFIG:Debug>,Debug,Release>" "Release" GLIB_INCLUDE_DIRS_RELEASE "${GLIB_INCLUDE_DIRS}")
string(REPLACE "$<IF:$<CONFIG:Debug>,Debug,Release>" "Debug" GLIB_LIBRARIES_DEBUG "${GLIB_LIBRARIES}")
string(REPLACE "$<IF:$<CONFIG:Debug>,Debug,Release>" "Release" GLIB_LIBRARIES_RELEASE "${GLIB_LIBRARIES}")

set(FIND_GLIB "${FIND_GLIB}set(glib_FOUND 1)\n")
set(FIND_GLIB "${FIND_GLIB}set(glib_INCLUDE_DIRS \"${GLIB_INCLUDE_DIRS_DEBUG}\")\n")
set(FIND_GLIB "${FIND_GLIB}set(glib_LIBRARIES \"${GLIB_LIBRARIES_DEBUG}\")\n")
file(WRITE "${glib_BINARY_DIR}/Debug/Modules/Findglib.cmake" "${FIND_GLIB}")

set(FIND_GLIB "${FIND_GLIB}set(glib_FOUND 1)\n")
set(FIND_GLIB "${FIND_GLIB}set(glib_INCLUDE_DIRS \"${GLIB_INCLUDE_DIRS_RELEASE}\")\n")
set(FIND_GLIB "${FIND_GLIB}set(glib_LIBRARIES \"${GLIB_LIBRARIES_RELEASE}\")\n")
file(WRITE "${glib_BINARY_DIR}/Release/Modules/Findglib.cmake" "${FIND_GLIB}")
