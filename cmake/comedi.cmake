FetchContent_Declare(
  comedi 
  GIT_REPOSITORY https://github.com/Linux-Comedi/comedilib.git
  GIT_TAG        r0_12_0
)

FetchContent_Populate(comedi)
file(MAKE_DIRECTORY ${comedi_BINARY_DIR}/Debug)
file(MAKE_DIRECTORY ${comedi_BINARY_DIR}/Release)

execute_process(COMMAND git apply "${CMAKE_SOURCE_DIR}/patches/comedi"
	        WORKING_DIRECTORY ${comedi_SOURCE_DIR})

add_custom_command(OUTPUT "${comedi_SOURCE_DIR}/configure"
                   COMMAND ./autogen.sh
                   && cmake -E touch_nocreate "${comedi_SOURCE_DIR}/configure"
                   WORKING_DIRECTORY "${comedi_SOURCE_DIR}")

add_custom_command(OUTPUT "${comedi_BINARY_DIR}/$<CONFIG>/Makefile"
                   DEPENDS  "${comedi_SOURCE_DIR}/configure"
                   COMMAND 
                   cmake -E env 
                      "CC=${CMAKE_C_COMPILER}"
		      "CFLAGS=${ALL_C_COMPILE_OPTIONS_SPACED}"
		      "LDFLAGS=${ALL_C_LINK_OPTIONS_SPACED}"
                      "${comedi_SOURCE_DIR}/configure"
                      "--prefix=${comedi_BINARY_DIR}/$<CONFIG>/install"
                      --enable-shared=no --enable-static=yes
                   && cmake -E touch_nocreate "${comedi_BINARY_DIR}/$<CONFIG>/Makefile"
                   WORKING_DIRECTORY "${comedi_BINARY_DIR}/$<CONFIG>")


add_custom_command(OUTPUT "${comedi_BINARY_DIR}/$<CONFIG>/install/lib/libcomedi.a"
                   DEPENDS "${comedi_BINARY_DIR}/$<CONFIG>/Makefile"
                   COMMAND
                   make -j ${CPU_COUNT}
                   && make install
                   && cmake -E touch_nocreate "${comedi_BINARY_DIR}/$<CONFIG>/install/lib/libcomedi.a"
                   WORKING_DIRECTORY "${comedi_BINARY_DIR}/$<CONFIG>")

add_library(comedi INTERFACE "${comedi_BINARY_DIR}/$<CONFIG>/install/lib/libcomedi.a")
target_link_libraries(comedi INTERFACE "${comedi_BINARY_DIR}/$<CONFIG>/install/lib/libcomedi.a")
target_include_directories(comedi INTERFACE "${comedi_BINARY_DIR}/$<CONFIG>/install/include")
