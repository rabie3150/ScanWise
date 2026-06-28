cmake_minimum_required(VERSION 3.28)
project(ScanWise VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Static runtime for minimal deployment size
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_compile_options(/W4 /WX-)
    # Size-first release build to hit the <10 MB goal
    add_compile_options($<$<CONFIG:Release>:/Os>)
    add_compile_options($<$<CONFIG:Release>:/GL>)
    add_compile_options($<$<CONFIG:Debug>:/Od>)
    add_compile_options($<$<CONFIG:Debug>:/Zi>)
    add_link_options($<$<CONFIG:Release>:/LTCG>)
    add_link_options($<$<CONFIG:Release>:/INCREMENTAL:NO>)
    add_link_options($<$<CONFIG:Release>:/OPT:REF,ICF>)
endif()

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# -----------------------------------------------------------------------------
# Dependencies
# -----------------------------------------------------------------------------
# Sources are expected to be cloned under deps/ before configuring.
# Run: git clone --branch 4.11.0 --depth 1 https://github.com/opencv/opencv.git deps/opencv
#      git clone --branch v2.4.4 --depth 1 https://github.com/libharu/libharu.git deps/libharu

if(NOT EXISTS ${CMAKE_SOURCE_DIR}/deps/opencv/CMakeLists.txt)
    message(FATAL_ERROR "OpenCV source not found. Run the clone commands in README.md.")
endif()

if(NOT EXISTS ${CMAKE_SOURCE_DIR}/deps/libharu/CMakeLists.txt)
    message(FATAL_ERROR "libharu source not found. Run the clone commands in README.md.")
endif()

# OpenCV static build options
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_opencv_core   ON  CACHE BOOL "" FORCE)
set(BUILD_opencv_imgproc ON CACHE BOOL "" FORCE)
set(BUILD_opencv_imgcodecs OFF CACHE BOOL "" FORCE)
set(BUILD_opencv_highgui   OFF CACHE BOOL "" FORCE)
set(BUILD_opencv_videoio   OFF CACHE BOOL "" FORCE)
set(BUILD_opencv_video     OFF CACHE BOOL "" FORCE)
set(BUILD_opencv_dnn       OFF CACHE BOOL "" FORCE)
set(BUILD_opencv_ml        OFF CACHE BOOL "" FORCE)
set(BUILD_opencv_objdetect OFF CACHE BOOL "" FORCE)
set(BUILD_opencv_features2d OFF CACHE BOOL "" FORCE)
set(BUILD_opencv_calib3d   OFF CACHE BOOL "" FORCE)
set(BUILD_opencv_flann     OFF CACHE BOOL "" FORCE)
set(BUILD_opencv_photo     OFF CACHE BOOL "" FORCE)
set(BUILD_opencv_stitching OFF CACHE BOOL "" FORCE)
set(BUILD_opencv_gapi      OFF CACHE BOOL "" FORCE)
set(BUILD_opencv_python2   OFF CACHE BOOL "" FORCE)
set(BUILD_opencv_python3   OFF CACHE BOOL "" FORCE)
set(BUILD_opencv_java      OFF CACHE BOOL "" FORCE)
set(BUILD_opencv_js        OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS            OFF CACHE BOOL "" FORCE)
set(BUILD_PERF_TESTS       OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES         OFF CACHE BOOL "" FORCE)
set(BUILD_APPS             OFF CACHE BOOL "" FORCE)
set(BUILD_DOCS             OFF CACHE BOOL "" FORCE)
set(WITH_OPENCL            OFF CACHE BOOL "" FORCE)
set(WITH_CUDA              OFF CACHE BOOL "" FORCE)
set(WITH_EIGEN             OFF CACHE BOOL "" FORCE)
set(WITH_TBB               OFF CACHE BOOL "" FORCE)
set(WITH_OPENMP            OFF CACHE BOOL "" FORCE)
set(WITH_IPP               OFF CACHE BOOL "" FORCE)
set(WITH_ITT               OFF CACHE BOOL "" FORCE)
set(WITH_OPENEXR           OFF CACHE BOOL "" FORCE)
set(WITH_WEBP              OFF CACHE BOOL "" FORCE)
set(WITH_TIFF              OFF CACHE BOOL "" FORCE)
set(WITH_JPEG              OFF CACHE BOOL "" FORCE)
set(WITH_PNG               OFF CACHE BOOL "" FORCE)
set(WITH_JASPER            OFF CACHE BOOL "" FORCE)
set(CV_ENABLE_INTRINSICS   ON  CACHE BOOL "" FORCE)

add_subdirectory(deps/opencv EXCLUDE_FROM_ALL)

# libharu options
set(LIBHPDF_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CMAKE_DISABLE_FIND_PACKAGE_ZLIB ON CACHE BOOL "" FORCE)
add_subdirectory(deps/libharu EXCLUDE_FROM_ALL)

# -----------------------------------------------------------------------------
# Core engine library
# -----------------------------------------------------------------------------
add_library(scanwise_engine STATIC
    engine/src/engine.cpp
    engine/src/edge_detect.cpp
    engine/src/perspective.cpp
    engine/src/filters.cpp
    engine/src/export.cpp
)

target_include_directories(scanwise_engine
    PUBLIC
        ${CMAKE_SOURCE_DIR}/engine/include
        ${CMAKE_SOURCE_DIR}/deps/opencv/modules/core/include
        ${CMAKE_SOURCE_DIR}/deps/opencv/modules/imgproc/include
        ${CMAKE_SOURCE_DIR}/deps/libharu/include
        ${CMAKE_BINARY_DIR}/deps/libharu/include
        ${CMAKE_BINARY_DIR}
)

target_link_libraries(scanwise_engine
    PUBLIC
        opencv_core
        opencv_imgproc
        hpdf
)

# -----------------------------------------------------------------------------
# Engine unit tests (console app, no UI)
# -----------------------------------------------------------------------------
add_executable(scanwise_engine_tests
    engine/tests/test_engine.cpp
)

target_link_libraries(scanwise_engine_tests PRIVATE scanwise_engine)


# -----------------------------------------------------------------------------
# Windows UI app (WinUI 3 / C++/WinRT)
# -----------------------------------------------------------------------------
if(WIN32)
    add_subdirectory(app)
endif()
