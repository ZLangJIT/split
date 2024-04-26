find_package(PkgConfig QUIET)
pkg_check_modules(PC_TMPFILE QUIET TMPFILE)

set(CMAKE_FIND_DEBUG_MODE FALSE) # TRUE)

find_path(TMPFILE_INCLUDE_DIRS NAMES tmpfile/tmpfile.h
  PATHS ${LLVM_BUILD_ROOT__ROOTFS}/include
  NO_DEFAULT_PATH
  NO_PACKAGE_ROOT_PATH
  NO_CMAKE_PATH
  NO_CMAKE_ENVIRONMENT_PATH
  NO_SYSTEM_ENVIRONMENT_PATH
  NO_CMAKE_SYSTEM_PATH
  NO_CMAKE_FIND_ROOT_PATH
)
find_library(TMPFILE_LIBRARIES NAMES libtmpfile.a
  PATHS ${LLVM_BUILD_ROOT__ROOTFS}/lib
  NO_DEFAULT_PATH
  NO_PACKAGE_ROOT_PATH
  NO_CMAKE_PATH
  NO_CMAKE_ENVIRONMENT_PATH
  NO_SYSTEM_ENVIRONMENT_PATH
  NO_CMAKE_SYSTEM_PATH
  NO_CMAKE_FIND_ROOT_PATH
)

set(CMAKE_FIND_DEBUG_MODE FALSE)

if(TMPFILE_INCLUDE_DIRS AND EXISTS "${TMPFILE_INCLUDE_DIRS}/tmpfile/tmpfile.h")
    set(TMPFILE_VERSION_STRING "1.0")
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(TMPFILE
                                  FOUND_VAR
                                    TMPFILE_FOUND
                                  REQUIRED_VARS
                                    TMPFILE_INCLUDE_DIRS
                                    TMPFILE_LIBRARIES
                                  VERSION_VAR
                                    TMPFILE_VERSION_STRING)
mark_as_advanced(TMPFILE_INCLUDE_DIRS TMPFILE_LIBRARIES)

message(STATUS "TmpFile: found :        ${TMPFILE_FOUND}")
message(STATUS "TmpFile: include_dirs : ${TMPFILE_INCLUDE_DIRS}")
message(STATUS "TmpFile: lib :          ${TMPFILE_LIBRARIES}")
message(STATUS "TmpFile: version :      ${TMPFILE_VERSION_STRING}")

if (TMPFILE_FOUND AND NOT TARGET LLVM_STATIC_TMPFILE)
  add_library(LLVM_STATIC_TMPFILE UNKNOWN IMPORTED)
  set_target_properties(LLVM_STATIC_TMPFILE PROPERTIES
                        IMPORTED_LOCATION ${TMPFILE_LIBRARIES}
                        INTERFACE_INCLUDE_DIRECTORIES ${TMPFILE_INCLUDE_DIRS})
  set(TMPFILE_TARGET LLVM_STATIC_TMPFILE)
endif()
