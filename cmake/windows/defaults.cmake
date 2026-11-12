# CMake Windows defaults module (adapted from obs-plugintemplate)

include_guard(GLOBAL)

# Enable find_package targets to become globally available targets
set(CMAKE_FIND_PACKAGE_TARGETS_GLOBAL TRUE)

include(buildspec)

# Never default to a system location: installs go into the workspace "dist" folder.
# (The upstream template defaults to %ALLUSERSPROFILE%/obs-studio/plugins.)
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/dist" CACHE STRING "Plugin staging directory" FORCE)
endif()
