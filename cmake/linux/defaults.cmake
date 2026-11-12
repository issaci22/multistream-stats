# CMake Linux defaults module (adapted from obs-plugintemplate 3e7d7ac)
#
# Changes vs. upstream template:
#  * No CPack: .github/scripts/package-linux.sh builds the .deb (dpkg-deb) and the
#    manual .tar.xz from `cmake --install --component system|user`, so the package
#    metadata, stripping and checksums are explicit and reviewable.
#  * libobs / obs-frontend-api are always required (the dock needs the frontend API);
#    a fallback finds them by header/library when no CMake package files are installed.

include_guard(GLOBAL)

# Set default installation directories
include(GNUInstallDirs)

if(CMAKE_INSTALL_LIBDIR MATCHES "(CMAKE_SYSTEM_PROCESSOR)")
  string(REPLACE "CMAKE_SYSTEM_PROCESSOR" "${CMAKE_SYSTEM_PROCESSOR}" CMAKE_INSTALL_LIBDIR "${CMAKE_INSTALL_LIBDIR}")
endif()

# Enable find_package targets to become globally available targets
set(CMAKE_FIND_PACKAGE_TARGETS_GLOBAL TRUE)

find_package(libobs QUIET)

if(NOT TARGET OBS::libobs)
  message(STATUS "libobs CMake package not found; falling back to header/library search")
  find_path(libobs_INCLUDE_DIR NAMES obs-module.h PATHS /usr/include /usr/local/include PATH_SUFFIXES obs REQUIRED)
  find_library(libobs_LIBRARY NAMES obs REQUIRED)
  add_library(OBS::libobs UNKNOWN IMPORTED)
  set_target_properties(
    OBS::libobs
    PROPERTIES IMPORTED_LOCATION "${libobs_LIBRARY}" INTERFACE_INCLUDE_DIRECTORIES "${libobs_INCLUDE_DIR}"
  )

  find_path(obs-frontend-api_INCLUDE_DIR NAMES obs-frontend-api.h PATHS /usr/include /usr/local/include PATH_SUFFIXES obs REQUIRED)
  find_library(obs-frontend-api_LIBRARY NAMES obs-frontend-api REQUIRED)
  add_library(OBS::obs-frontend-api UNKNOWN IMPORTED)
  set_target_properties(
    OBS::obs-frontend-api
    PROPERTIES
      IMPORTED_LOCATION "${obs-frontend-api_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${obs-frontend-api_INCLUDE_DIR}"
  )

  # The top-level CMakeLists calls find_package() for both; they are satisfied above.
  macro(find_package)
    if(NOT "${ARGV0}" STREQUAL libobs AND NOT "${ARGV0}" STREQUAL obs-frontend-api)
      _find_package(${ARGV})
    endif()
  endmacro()
endif()
