cmake_minimum_required(VERSION 3.10)

project(
    proxygen
)

set(CMAKE_MODULE_PATH
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake"
  # for in-fbsource builds
  "${CMAKE_CURRENT_SOURCE_DIR}/../opensource/fbcode_builder/CMake"
  # For shipit-transformed builds
  "${CMAKE_CURRENT_SOURCE_DIR}/build/fbcode_builder/CMake"
  ${CMAKE_MODULE_PATH})

# PROXYGEN_FBCODE_ROOT is where the top level proxygen/ directory resides, so
# an #include <proxygen/path/to/file> will resolve to
# $PROXYGEN_FBCODE_ROOT/proxygen/path/to/file on disk
set(PROXYGEN_FBCODE_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

# Similarly, PROXYGEN_GENERATED_ROOT is where the top level proxygen/ directory
# resides for generated files, so a #include <proxygen/path/to/generated/file>
# will be at $PROXYGEN_GENERATED_ROOT/proxygen/path/to/generated/file
set(PROXYGEN_GENERATED_ROOT ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${PROXYGEN_GENERATED_ROOT})

# Build-time program requirements.
find_program(PROXYGEN_PYTHON python)
if(NOT PROXYGEN_PYTHON)
    message(FATAL_ERROR "python is required for the proxygen build")
endif()

find_program(PROXYGEN_GPERF gperf)
if(NOT PROXYGEN_GPERF)
    message(FATAL_ERROR "gperf is required for the proxygen build")
endif()

# Dependencies
find_package(folly REQUIRED)
find_package(wangle REQUIRED)
find_package(Fizz REQUIRED)
find_package(mvfst QUIET)
find_package(Zstd REQUIRED)
find_package(ZLIB REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Threads)
find_package(Boost 1.58 REQUIRED
  COMPONENTS
    iostreams
    context
    chrono
    date_time
    filesystem
    program_options
    regex
    system
    thread
)

list(APPEND
    _PROXYGEN_COMMON_COMPILE_OPTIONS
    -std=c++14
    -Wall
    -Wextra
)

SET(GFLAG_DEPENDENCIES "")
SET(PROXYGEN_EXTRA_LINK_LIBRARIES "")
SET(PROXYGEN_EXTRA_INCLUDE_DIRECTORIES "")

find_package(gflags CONFIG QUIET)
if (gflags_FOUND)
  message("module path: ${CMAKE_MODULE_PATH}")
  message(STATUS "Found gflags from package config")
  if (TARGET gflags-shared)
    list(APPEND GFLAG_DEPENDENCIES gflags-shared)
  elseif (TARGET gflags)
    list(APPEND GFLAG_DEPENDENCIES gflags)
  else()
    message(FATAL_ERROR
            "Unable to determine the target name for the GFlags package.")
  endif()
  list(APPEND CMAKE_REQUIRED_LIBRARIES ${GFLAGS_LIBRARIES})
  list(APPEND CMAKE_REQUIRED_INCLUDES ${GFLAGS_INCLUDE_DIR})
else()
  find_package(Gflags REQUIRED MODULE)
  list(APPEND PROXYGEN_EXTRA_LINK_LIBRARIES ${LIBGFLAGS_LIBRARY})
  list(APPEND PROXYGEN_EXTRA_INCLUDE_DIRECTORIES ${LIBGFLAGS_INCLUDE_DIR})
  list(APPEND CMAKE_REQUIRED_LIBRARIES ${LIBGFLAGS_LIBRARY})
  list(APPEND CMAKE_REQUIRED_INCLUDES ${LIBGFLAGS_INCLUDE_DIR})
endif()

include(ProxygenTest)

add_subdirectory(proxygen)

install(
    EXPORT proxygen-exports
    FILE proxygen-targets.cmake
    NAMESPACE proxygen::
    DESTINATION lib/cmake/proxygen/
)
include(CMakePackageConfigHelpers)
configure_package_config_file(
    cmake/proxygen-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/proxygen-config.cmake
    INSTALL_DESTINATION lib/cmake/proxygen/
)
install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/proxygen-config.cmake
    DESTINATION lib/cmake/proxygen/
)

# uninstall target
if(NOT TARGET uninstall)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake_uninstall.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
        IMMEDIATE @ONLY)

    add_custom_target(uninstall
        COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake)
endif()
