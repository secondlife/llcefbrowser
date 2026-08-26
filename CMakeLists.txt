# ==============================================================================
# llCefBrowser

# ==============================================================================
cmake_minimum_required(VERSION 3.30)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ==============================================================================
# Main project/solution
#
project(llcefbrowser)

# ==============================================================================
# Grab and build CEF DLL Wrapper
# (Define a CEF package URL or local DIR in the CMake command line)
#
if(NOT DEFINED CEF_PACKAGE_URL)
  if(NOT DEFINED CEF_PACKAGE_DIR)
    message(FATAL_ERROR "Required variable CEF_PACKAGE_URL or CEF_PACKAGE_DIR is not defined")
  endif()
endif()

include(FetchContent)
if(DEFINED CEF_PACKAGE_URL)
  fetchcontent_declare(cef_prebuild URL ${CEF_PACKAGE_URL} DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
elseif(DEFINED CEF_PACKAGE_DIR)
  fetchcontent_declare(cef_prebuild SOURCE_DIR ${CEF_PACKAGE_DIR})
endif()
fetchcontent_makeavailable(cef_prebuild)

# A Spotify Automated Builds package (CEF_PACKAGE_URL's usual source) has
# cmake/, include/, etc. directly at its archive root. The internally-built,
# media-codec-enabled package (both CEF_PACKAGE_URL and CEF_PACKAGE_DIR can
# point at this one -- see tools/build.bat and autobuild.xml) nests all of
# that one level deeper, under a "cef" subdirectory instead, which is what
# build-cmd.sh's own -DCEF_PACKAGE_DIR=.../packages/cef already compensates
# for by pointing directly at that subdirectory. CEF_PACKAGE_URL has no
# equivalent chance to compensate ahead of time, so detect which layout was
# actually unpacked here instead of hard-coding one.
if(EXISTS "${cef_prebuild_SOURCE_DIR}/cmake")
  # Flat layout: fetchcontent_makeavailable() above already found this same
  # directory's own top-level CMakeLists.txt and add_subdirectory()'d it
  # automatically, which is what defines the libcef_dll_wrapper target,
  # with its own binary dir directly under cef_prebuild_BINARY_DIR.
  set(CEF_ROOT "${cef_prebuild_SOURCE_DIR}")
  set(CEF_WRAPPER_BINARY_DIR "${cef_prebuild_BINARY_DIR}")
elseif(EXISTS "${cef_prebuild_SOURCE_DIR}/cef/cmake")
  # Nested layout: CEF's own CMakeLists.txt (and therefore the
  # libcef_dll_wrapper target it defines) lives one level down from where
  # fetchcontent_makeavailable() automatically looked, so its automatic
  # add_subdirectory() found nothing there and silently added nothing.
  # Do it explicitly ourselves instead, now that we know the real location --
  # its binary dir ends up one level down too, to match.
  set(CEF_ROOT "${cef_prebuild_SOURCE_DIR}/cef")
  set(CEF_WRAPPER_BINARY_DIR "${cef_prebuild_BINARY_DIR}/cef")
  add_subdirectory("${CEF_ROOT}" "${CEF_WRAPPER_BINARY_DIR}")
else()
  message(FATAL_ERROR "Could not find CEF's cmake/ directory under either "
    "${cef_prebuild_SOURCE_DIR} or ${cef_prebuild_SOURCE_DIR}/cef -- "
    "unrecognized CEF package layout")
endif()
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CEF_ROOT}/cmake")
find_package(CEF REQUIRED)

# ==============================================================================
# llCefBrowser library
#
set(LLCEFBROWSER_PUBLIC_HEADERS
  include/llCefBrowserHandle.h
  include/llCefBrowserManager.h
  include/llCefBrowserLib.h
  include/llCefBrowserLibInitOptions.h
  include/llCefBrowserJavaScriptBridge.h
  include/llCefBrowserLibDebug.h
  include/llCefBrowserVersion.h
)

set(LLCEFBROWSER_SRCS
  src/llCefBrowser.h
  src/llCefBrowser.cpp
  src/llCefBrowserManager.cpp
  src/llCefBrowserManagerImpl.h
  src/llCefBrowserManagerImpl.cpp
  src/llCefBrowserLib.cpp
  src/llCefBrowserLibInitOptionsAccess.h
  src/llCefBrowserMessageRouterAccess.h
  src/llCefBrowserPixelBuffer.h
)

add_library(
  llcefbrowser
  STATIC
  ${LLCEFBROWSER_PUBLIC_HEADERS}
  ${LLCEFBROWSER_SRCS}
)

target_include_directories(
  llcefbrowser
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    "${CEF_ROOT}/include"
    "${CEF_ROOT}/include/.."
)

target_link_libraries(
  llcefbrowser
  "${CEF_ROOT}/Release/libcef.lib"
  "${CEF_WRAPPER_BINARY_DIR}/libcef_dll_wrapper/Release/libcef_dll_wrapper.lib"
)

# Required for C++20 compatibility in CEF
target_compile_definitions(
  llcefbrowser
  PUBLIC
  NOMINMAX
)

# Gather source and headers in a convenient layout for Visual Studio
source_group("include" FILES ${LLCEFBROWSER_PUBLIC_HEADERS})
source_group("src" FILES ${LLCEFBROWSER_SRCS})

# ==============================================================================
# Populate the version header file from the template using CEF and Chromium
# version numbers along with some git params for the library version
#
file(REMOVE "${PROJECT_SOURCE_DIR}/include/llCefBrowserVersion.h")
file(STRINGS ${CEF_ROOT}/include/cef_version.h CEF_VERSION_MAJOR_STR
     REGEX "\#define CEF_VERSION_MAJOR "
)
file(STRINGS ${CEF_ROOT}/include/cef_version.h CEF_VERSION_MINOR_STR
     REGEX "\#define CEF_VERSION_MINOR "
)
file(STRINGS ${CEF_ROOT}/include/cef_version.h CEF_VERSION_BUILD_STR
     REGEX "\#define CEF_VERSION_BUILD "
)
file(STRINGS ${CEF_ROOT}/include/cef_version.h CEF_VERSION_PATCH_STR
     REGEX "\#define CEF_VERSION_PATCH "
)
file(STRINGS ${CEF_ROOT}/include/cef_version.h CHROME_VERSION_MAJOR_STR
     REGEX "\#define CHROME_VERSION_MAJOR "
)
file(STRINGS ${CEF_ROOT}/include/cef_version.h CHROME_VERSION_MINOR_STR
     REGEX "\#define CHROME_VERSION_MINOR "
)
file(STRINGS ${CEF_ROOT}/include/cef_version.h CHROME_VERSION_BUILD_STR
     REGEX "\#define CHROME_VERSION_BUILD "
)
file(STRINGS ${CEF_ROOT}/include/cef_version.h CHROME_VERSION_PATCH_STR
     REGEX "\#define CHROME_VERSION_PATCH "
)
execute_process(
  COMMAND git rev-list HEAD --count
  OUTPUT_VARIABLE GIT_COMMIT_COUNT
  OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET
)
execute_process(
  COMMAND git rev-parse --short HEAD
  OUTPUT_VARIABLE GIT_COMMIT_HASH
  OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET
)
configure_file(
  "${PROJECT_SOURCE_DIR}/include/llCefBrowserVersion.h.in"
  "${PROJECT_SOURCE_DIR}/include/llCefBrowserVersion.h"
)

# ==============================================================================
# Example applications - common
#
# Off by default for a parent project that add_subdirectory()'s this repo
# just to reuse llcefbrowser itself (or, like llcefshm-example, just its
# vendored third-party sources) -- these two apps are standalone, in-process
# demos with no shared-memory transport at all, unrelated to and not runnable
# from within a parent project's own solution. Mirrors llshmframe's own
# LLSHMFRAME_BUILD_EXAMPLES option.
option(LLCEFBROWSER_BUILD_EXAMPLES "Build llcefbrowser's own example applications" ON)
if(LLCEFBROWSER_BUILD_EXAMPLES)

# CMAKE_CURRENT_SOURCE_DIR, not CMAKE_SOURCE_DIR: this must resolve relative
# to this file even when llcefbrowser is add_subdirectory()'d from a parent
# project, not just when it's the top-level project itself.
set(THIRD-PARTY-PREFIX ${CMAKE_CURRENT_SOURCE_DIR}/examples/third-party)
set(GLAD_FOLDER "glad-0.1.36")
set(IMGUI_FOLDER "imgui-1.92.2")
set(JSON_FOLDER "json-3.12.0")
if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
  set(GLFW_FOLDER "glfw-3.4.bin.WIN64")
  set(GLFW_LIB_PATH "lib-vc2022/glfw3.lib")
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
endif()

set(LLCEFBROWSER_EXAMPLES_THIRD_PARTY_FILES
  ${THIRD-PARTY-PREFIX}/${GLAD_FOLDER}/src/glad.c
  ${THIRD-PARTY-PREFIX}/${IMGUI_FOLDER}/imgui.cpp
  ${THIRD-PARTY-PREFIX}/${IMGUI_FOLDER}/imgui_draw.cpp
  ${THIRD-PARTY-PREFIX}/${IMGUI_FOLDER}/imgui_impl_glfw.cpp
  ${THIRD-PARTY-PREFIX}/${IMGUI_FOLDER}/imgui_impl_opengl2.cpp
  ${THIRD-PARTY-PREFIX}/${IMGUI_FOLDER}/imgui_tables.cpp
  ${THIRD-PARTY-PREFIX}/${IMGUI_FOLDER}/imgui_widgets.cpp
)

# ==============================================================================
# Example application - Single browser OpenGL example
#
add_executable(single-browser-example
  examples/single-browser/src/single-browser-example.cpp
  examples/single-browser/src/single-browser-example.h
  ${LLCEFBROWSER_EXAMPLES_THIRD_PARTY_FILES}
)
target_include_directories(
  single-browser-example
  PUBLIC
  examples/single-browser/src/single-browser-example.h
  ${THIRD-PARTY-PREFIX}/${GLAD_FOLDER}/include
  ${THIRD-PARTY-PREFIX}/${GLFW_FOLDER}/include
  ${THIRD-PARTY-PREFIX}/${JSON_FOLDER}/include
  ${THIRD-PARTY-PREFIX}/${IMGUI_FOLDER}
)
target_link_libraries(
  single-browser-example
  PRIVATE
  ${THIRD-PARTY-PREFIX}/${GLFW_FOLDER}/${GLFW_LIB_PATH}
  opengl32
  comctl32
  llcefbrowser
)
source_group(
  "Third Party Files"
  FILES
  ${LLCEFBROWSER_EXAMPLES_THIRD_PARTY_FILES}
)

# ==============================================================================
# Example application - Multiple browsers OpenGL example
#
add_executable(multiple-browsers-example
  examples/multiple-browsers/src/multiple-browsers-example.cpp
  examples/multiple-browsers/src/multiple-browsers-example.h
  ${LLCEFBROWSER_EXAMPLES_THIRD_PARTY_FILES}
)
target_include_directories(
  multiple-browsers-example
  PUBLIC
  examples/multiple-browsers/src/multiple-browsers-example.h
  "${THIRD-PARTY-PREFIX}/${GLAD_FOLDER}/include"
  "${THIRD-PARTY-PREFIX}/${GLFW_FOLDER}/include"
  "${THIRD-PARTY-PREFIX}/${JSON_FOLDER}/include"
  ${THIRD-PARTY-PREFIX}/${IMGUI_FOLDER}
)
target_link_libraries(
  multiple-browsers-example
  PRIVATE
  ${THIRD-PARTY-PREFIX}/${GLFW_FOLDER}/${GLFW_LIB_PATH}
  opengl32
  comctl32
  llcefbrowser
)
set_property(
  DIRECTORY
  PROPERTY
  VS_STARTUP_PROJECT
  multiple-browsers-example
)
source_group(
  "Third Party Files"
  FILES
  ${LLCEFBROWSER_EXAMPLES_THIRD_PARTY_FILES}
)

endif() # LLCEFBROWSER_BUILD_EXAMPLES

# ==============================================================================
# Copy necessary runtime files
#
# Unconditional, not inside the option guard above -- needed by any consumer
# of this library's runtime (llcefshm-example's own cefshm_producer.exe,
# the Viewer's SLCefProducer.exe, not just the two example apps), all of
# which rely on this same unconditional-per-target default output location.
#
if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
  set(DESTINATION_DIR "${CMAKE_BINARY_DIR}/Release")
  file(MAKE_DIRECTORY "${DESTINATION_DIR}")
  foreach(file_path IN LISTS CEF_BINARY_FILES)
    set(SOURCE_FILE "${CEF_ROOT}/Release/${file_path}")
    file(COPY "${SOURCE_FILE}" DESTINATION "${DESTINATION_DIR}")
  endforeach()
  set(DESTINATION_DIR "${CMAKE_BINARY_DIR}/Release")
  foreach(file_path IN LISTS CEF_RESOURCE_FILES)
    set(SOURCE_FILE "${CEF_ROOT}/Resources/${file_path}")
    file(COPY "${SOURCE_FILE}" DESTINATION "${DESTINATION_DIR}")
  endforeach()
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
endif()

# ==============================================================================
# General options
#
add_dependencies(llcefbrowser libcef_dll_wrapper)
set(CMAKE_CONFIGURATION_TYPES "Release")
