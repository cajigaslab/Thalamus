FetchContent_Declare(
  opencv
  GIT_REPOSITORY https://github.com/opencv/opencv.git
  GIT_TAG 4.8.1)
FetchContent_Populate(opencv)
file(MAKE_DIRECTORY ${opencv_BINARY_DIR}/Debug)
file(MAKE_DIRECTORY ${opencv_BINARY_DIR}/Release)

if(MSVC_VERSION MATCHES "^192[0-9]$")
  set(OPENCV_LIBS_FOLDER vc16)
elseif(MSVC_VERSION MATCHES "^193[0-9]$")
  set(OPENCV_LIBS_FOLDER vc17)
endif()

set(OPENCV_LIB_FILES)
if(WIN32)
  set(OPENCV_LIBS "IlmImf$<$<CONFIG:Debug>:d>"
                  "ippicvmt"
                  "ippiw$<$<CONFIG:Debug>:d>"
                  "ittnotify$<$<CONFIG:Debug>:d>"
                  "libjpeg-turbo$<$<CONFIG:Debug>:d>"
                  "libopenjp2$<$<CONFIG:Debug>:d>"
                  "libpng$<$<CONFIG:Debug>:d>"
                  "libtiff$<$<CONFIG:Debug>:d>"
                  "opencv_stitching481$<$<CONFIG:Debug>:d>"
                  "opencv_calib3d481$<$<CONFIG:Debug>:d>"
                  "opencv_flann481$<$<CONFIG:Debug>:d>"
                  "opencv_features2d481$<$<CONFIG:Debug>:d>"
                  "opencv_core481$<$<CONFIG:Debug>:d>"
                  "opencv_imgcodecs481$<$<CONFIG:Debug>:d>"
                  "opencv_imgproc481$<$<CONFIG:Debug>:d>"
                  "opencv_highgui481$<$<CONFIG:Debug>:d>"
                  "opencv_objdetect481$<$<CONFIG:Debug>:d>"
                  "opencv_videoio481$<$<CONFIG:Debug>:d>")
  if("${CMAKE_CXX_COMPILER_ID}" MATCHES "MSVC")
    if("${MSVC_VERSION}" LESS "1940")
      set(OPENCV_STATICLIB_PREFIX "x64/vc17/")
    else()
      set(OPENCV_STATICLIB_PREFIX "/")
    endif()
  else()
    set(OPENCV_STATICLIB_PREFIX)
  endif()
  foreach(LIB ${OPENCV_LIBS})
    list(APPEND OPENCV_LIB_FILES "${opencv_BINARY_DIR}/$<CONFIG>/install/${OPENCV_STATICLIB_PREFIX}staticlib/${LIB}.lib")
  endforeach()
else()
  set(OPENCV_LIBS opencv_stitching
                  opencv_calib3d
                  opencv_flann
                  opencv_features2d
                  opencv_highgui
                  opencv_videoio
                  opencv_imgcodecs
                  opencv_imgproc
                  opencv_objdetect
                  opencv_core)
  set(OPENCV_THIRDPARTY_LIBS libjpeg-turbo libpng libtiff libopenjp2 IlmImf ippiw ittnotify ippicv)
  foreach(LIB ${OPENCV_LIBS})
    list(APPEND OPENCV_LIB_FILES "${opencv_BINARY_DIR}/$<CONFIG>/install/lib/lib${LIB}.a")
  endforeach()
  foreach(LIB ${OPENCV_THIRDPARTY_LIBS})
    list(APPEND OPENCV_LIB_FILES "${opencv_BINARY_DIR}/$<CONFIG>/install/lib/opencv4/3rdparty/lib${LIB}.a")
  endforeach()
endif()
 
add_custom_command(OUTPUT "${opencv_BINARY_DIR}/$<CONFIG>/CMakeCache.txt"
                   DEPENDS ${ZLIB_PROCESSED_HEADER_PATHS} ffmpeg
                   COMMAND 
                   cmake "${opencv_SOURCE_DIR}" -Wno-dev 
                      -DCMAKE_POSITION_INDEPENDENT_CODE=ON
                      -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
                      -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
                      -DCMAKE_LINKER=${CMAKE_LINKER}
                      "-DCMAKE_MODULE_PATH=${ffmpeg_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/Modules"
                      "-DCMAKE_EXE_LINKER_FLAGS=${ALL_LINK_OPTIONS_SPACED}"
                      "-DCMAKE_CXX_FLAGS=${ALL_COMPILE_OPTIONS_SPACED}" 
                      "-DCMAKE_C_FLAGS=${ALL_COMPILE_OPTIONS_SPACED}"
                      -DOPENCV_SKIP_VISIBILITY_HIDDEN=ON
                      -DBUILD_TESTS=OFF -DBUILD_EXAMPLES=OFF -DBUILD_PERF_TESTS=OFF
                      -DBUILD_SHARED_LIBS=OFF -DWITH_EIGEN=OFF  -DOPENCV_PYTHON_SKIP_DETECTION=ON
                      -DBUILD_JPEG=ON -DBUILD_PNG=ON -DWITH_WEBP=OFF -DBUILD_TIFF=ON -DBUILD_ZLIB=OFF
                      -DBUILD_OPENJPEG=ON -DBUILD_OPENEXR=ON -DWITH_GSTREAMER=OFF -DWITH_FFMPEG=ON 
                      -DOPENCV_FFMPEG_USE_FIND_PACKAGE=ON -DWITH_1394=OFF -DWITH_GTK=OFF -DWITH_VTK=OFF
                      -DBUILD_LIST=core,imgproc,imgcodecs,highgui,videoio,calib3d,flann,features2d,stitching,objdetect
                      -DBUILD_opencv_python2=OFF -DBUILD_opencv_python3=OFF
                      "-DCMAKE_BUILD_TYPE=$<CONFIG>" "-DCMAKE_INSTALL_PREFIX=${opencv_BINARY_DIR}/$<CONFIG>/install"
                      -DCMAKE_CXX_STANDARD=${CMAKE_CXX_STANDARD} "-DZLIB_LIBRARY=$<TARGET_FILE:zlibstatic>"
                      -DCMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}
                      "-DZLIB_ROOT=${ZLIB_PROCESSED_HEADER_DIR}"
                      -DCMAKE_POLICY_DEFAULT_CMP0091=NEW
                      -DCMAKE_MSVC_RUNTIME_LIBRARY=${CMAKE_MSVC_RUNTIME_LIBRARY}
                      -DCMAKE_MSVC_RUNTIME_LIBRARY_DEFAULT=${CMAKE_MSVC_RUNTIME_LIBRARY}
                      -G "${CMAKE_GENERATOR}"
                   && cmake -E touch_nocreate "${opencv_BINARY_DIR}/$<CONFIG>/CMakeCache.txt"
                   WORKING_DIRECTORY "${opencv_BINARY_DIR}/$<CONFIG>")
add_custom_command(OUTPUT ${OPENCV_LIB_FILES}
                   DEPENDS "${opencv_BINARY_DIR}/$<CONFIG>/CMakeCache.txt"
                   COMMAND 
                   echo OPENCV BUILD
                   && cmake --build . --config "$<CONFIG>" --parallel ${CPU_COUNT}
                   && cmake --install . --config "$<CONFIG>"
                   && cmake -E touch_nocreate ${OPENCV_LIB_FILES}
                   WORKING_DIRECTORY ${opencv_BINARY_DIR}/$<CONFIG>)
      
#string(REGEX REPLACE "$<CONFIG>" "Debug" OPENCV_DEBUG_LIBS "${OPENCV_LIB_FILES}")
#string(REGEX REPLACE "$<CONFIG>" "Release" OPENCV_RELEASE_LIBS "${OPENCV_LIB_FILES}")
#add_library(opencv-debug INTERFACE ${OPENCV_DEBUG_LIBS})
#add_library(opencv-release INTERFACE ${OPENCV_RELEASE_LIBS})

add_library(opencv INTERFACE ${OPENCV_LIB_FILES})
add_custom_target(build-opencv DEPENDS opencv)
if(WIN32)
  set(OPENCV_INCLUDE "${opencv_BINARY_DIR}/$<CONFIG>/install/include")
  target_link_libraries(opencv INTERFACE ${OPENCV_LIB_FILES})
elseif(APPLE)
  set(OPENCV_INCLUDE "${opencv_BINARY_DIR}/$<CONFIG>/install/include/opencv4")
  target_link_libraries(opencv INTERFACE ${OPENCV_LIB_FILES} lzma iconv
    "-framework VideoToolbox" "-framework AudioToolbox" "-framework CoreVideo"
    "-framework CoreFoundation" "-framework CoreMedia" "-framework OpenCL"
    "-framework Accelerate" "-framework AVFoundation"
    dl)
else()
  set(OPENCV_INCLUDE "${opencv_BINARY_DIR}/$<CONFIG>/install/include/opencv4")
  target_link_libraries(opencv INTERFACE ${OPENCV_LIB_FILES} dl)
endif()
target_include_directories(opencv INTERFACE "${OPENCV_INCLUDE}")
