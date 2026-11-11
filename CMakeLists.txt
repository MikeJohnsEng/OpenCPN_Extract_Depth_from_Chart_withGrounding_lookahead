# ---------------------------------------------------------------------------
# cm93depth_pi - CM93 least-depth plugin for OpenCPN
#
# Two ways to build:
#
#  1. In-tree (works on every platform): copy this folder into an OpenCPN
#     source checkout as  plugins/cm93depth_pi  and build OpenCPN as usual.
#     OpenCPN's plugins/CMakeLists.txt picks up every *_pi folder.
#
#  2. Standalone, against an installed OpenCPN:
#       cmake -S . -B build -DOCPN_API_DIR=/path/to/OpenCPN/include
#       cmake --build build
#     Linux and macOS resolve OpenCPN's symbols when the plugin is loaded.
#     Windows additionally needs the OpenCPN import library:
#       -DOCPN_IMPORT_LIB=C:/path/to/opencpn.lib
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
# ---------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.10)
project(cm93depth_pi CXX)

set(PACKAGE_NAME cm93depth_pi)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(CM93DEPTH_BUILD_TESTS "Build the wx-free unit tests" OFF)

set(IN_TREE OFF)
if (TARGET ocpn::opencpn)
  set(IN_TREE ON)
endif ()

# ---- core (no wxWidgets) ----------------------------------------------------
set(CORE_SRC
    src/cm93_reader.cpp src/cm93_reader.h
    src/depth_engine.cpp src/depth_engine.h
    src/lookahead.cpp
    src/s57_reader.cpp src/s57_reader.h src/s57_codes.cpp
    src/enc_index.cpp src/enc_index.h
    src/nmea_out.cpp src/nmea_out.h
    src/serial_port.cpp src/serial_port.h
)

find_package(LibLZMA QUIET)

if (NOT QT_ANDROID)
  if (NOT DEFINED wxWidgets_USE_FILE)
    set(wxWidgets_USE_LIBS base core adv)
    find_package(wxWidgets REQUIRED)
  endif ()
  include(${wxWidgets_USE_FILE})
endif ()

set(PLUGIN_SRC
    src/cm93depth_pi.cpp src/cm93depth_pi.h
    src/depth_panel.cpp src/depth_panel.h
    src/prefs_dialog.cpp src/prefs_dialog.h
    src/icons.cpp src/icons.h
    src/alarm_dialog.cpp src/alarm_dialog.h src/alarm_logic.h
)

add_library(${PACKAGE_NAME} SHARED ${CORE_SRC} ${PLUGIN_SRC})
target_include_directories(${PACKAGE_NAME} PRIVATE src)

if (LIBLZMA_FOUND)
  target_compile_definitions(${PACKAGE_NAME} PRIVATE HAVE_LZMA)
  target_include_directories(${PACKAGE_NAME} PRIVATE ${LIBLZMA_INCLUDE_DIRS})
  target_link_libraries(${PACKAGE_NAME} ${LIBLZMA_LIBRARIES})
  message(STATUS "${PACKAGE_NAME}: .xz compressed CM93 cells supported")
else ()
  message(STATUS "${PACKAGE_NAME}: liblzma not found, .xz cells will be skipped")
endif ()

# OpenGL: only to draw the look-ahead zone when OpenCPN runs in OpenGL mode
if (NOT QT_ANDROID)
  find_package(OpenGL QUIET)
endif ()
if (OPENGL_FOUND)
  target_include_directories(${PACKAGE_NAME} PRIVATE ${OPENGL_INCLUDE_DIR})
  target_link_libraries(${PACKAGE_NAME} ${OPENGL_LIBRARIES})
else ()
  target_compile_definitions(${PACKAGE_NAME} PRIVATE CM93DEPTH_NO_GL)
  message(STATUS "${PACKAGE_NAME}: OpenGL not found, zone drawn in non-OpenGL mode only")
endif ()

if ("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang|GNU")
  target_compile_options(${PACKAGE_NAME} PRIVATE -fvisibility=hidden -Wall)
endif ()
if (MSVC)
  target_compile_definitions(${PACKAGE_NAME} PRIVATE
      __MSVC__ _CRT_NONSTDC_NO_DEPRECATE _CRT_SECURE_NO_DEPRECATE NOMINMAX)
  target_compile_options(${PACKAGE_NAME} PRIVATE /utf-8 /EHsc)
endif ()

target_link_libraries(${PACKAGE_NAME} ${wxWidgets_LIBRARIES})

if (IN_TREE)
  target_link_libraries(${PACKAGE_NAME} ocpn::opencpn)
else ()
  if (NOT OCPN_API_DIR)
    message(FATAL_ERROR
        "Standalone build: set -DOCPN_API_DIR=<dir containing ocpn_plugin.h>, "
        "e.g. the include/ folder of an OpenCPN source checkout. "
        "Or copy this folder to OpenCPN's plugins/ and build in-tree.")
  endif ()
  target_include_directories(${PACKAGE_NAME} PRIVATE ${OCPN_API_DIR})
  if (WIN32)
    if (NOT OCPN_IMPORT_LIB)
      message(FATAL_ERROR
          "Windows standalone build needs -DOCPN_IMPORT_LIB=<opencpn.lib>. "
          "Easier: build in-tree inside the OpenCPN source.")
    endif ()
    target_link_libraries(${PACKAGE_NAME} ${OCPN_IMPORT_LIB})
    # ocpn_plugin.h marks its classes DECL_EXP, which inside a plugin means
    # dllexport: the compiler then emits and exports every inline member of
    # every API class, including classes newer than the OpenCPN being linked
    # against (a newer header than the program), whose functions its
    # opencpn.exe does not export -> "unresolved external" at link time.
    # Declaring them plainly avoids that; calls into OpenCPN still resolve
    # through the import library, and the plugin exports only create_pi /
    # destroy_pi (CM93_PLUGIN_EXPORT).
    target_compile_definitions(${PACKAGE_NAME} PRIVATE DECL_EXP=)
  elseif (APPLE)
    set_target_properties(${PACKAGE_NAME} PROPERTIES
        LINK_FLAGS "-undefined dynamic_lookup")
  endif ()
endif ()

# ---- install -----------------------------------------------------------------
if (IN_TREE)
  if (UNIX AND NOT APPLE)
    install(TARGETS ${PACKAGE_NAME} LIBRARY DESTINATION ${PREFIX_PLUGINS})
  elseif (WIN32)
    install(TARGETS ${PACKAGE_NAME} RUNTIME DESTINATION "plugins")
  elseif (APPLE)
    install(TARGETS ${PACKAGE_NAME} LIBRARY DESTINATION
        ${CMAKE_BINARY_DIR}/OpenCPN.app/Contents/PlugIns)
  endif ()
else ()
  if (UNIX AND NOT APPLE)
    # Debian/Ubuntu packages load plugins from /usr/lib/opencpn
    install(TARGETS ${PACKAGE_NAME} LIBRARY DESTINATION lib/opencpn)
  endif ()
endif ()

# ---- tests ---------------------------------------------------------------------
if (CM93DEPTH_BUILD_TESTS)
  add_executable(test_core tests/test_core.cpp
      src/cm93_reader.cpp src/depth_engine.cpp src/lookahead.cpp src/nmea_out.cpp
      src/s57_reader.cpp src/s57_codes.cpp src/enc_index.cpp)
  target_include_directories(test_core PRIVATE src tests)
  if (LIBLZMA_FOUND)
    target_compile_definitions(test_core PRIVATE HAVE_LZMA)
    target_include_directories(test_core PRIVATE ${LIBLZMA_INCLUDE_DIRS})
    target_link_libraries(test_core ${LIBLZMA_LIBRARIES})
  endif ()
  if (UNIX)
    add_executable(test_serial tests/test_serial.cpp src/serial_port.cpp)
    target_include_directories(test_serial PRIVATE src)
  endif ()
endif ()
