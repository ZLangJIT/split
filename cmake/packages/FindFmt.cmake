find_package(PkgConfig QUIET)
pkg_check_modules(PC_OPENSSL QUIET OPENSSL)

set(CMAKE_FIND_DEBUG_MODE FALSE) # TRUE)

find_path(FMT_INCLUDE_DIRS NAMES fmt/core.h
  PATHS ${LLVM_BUILD_ROOT__ROOTFS}/include
  NO_DEFAULT_PATH
  NO_PACKAGE_ROOT_PATH
  NO_CMAKE_PATH
  NO_CMAKE_ENVIRONMENT_PATH
  NO_SYSTEM_ENVIRONMENT_PATH
  NO_CMAKE_SYSTEM_PATH
  NO_CMAKE_FIND_ROOT_PATH
)
find_library(FMT_LIBRARIES NAMES libfmt.a libfmtd.a
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

if(FMT_INCLUDE_DIRS AND EXISTS "${FMT_INCLUDE_DIRS}/fmt/core.h")
    set(FMT_VERSION_STRING "10.2.2")
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Fmt
                                  FOUND_VAR
                                    FMT_FOUND
                                  REQUIRED_VARS
                                    FMT_INCLUDE_DIRS
                                    FMT_LIBRARIES
                                  VERSION_VAR
                                    FMT_VERSION_STRING)
mark_as_advanced(FMT_INCLUDE_DIRS FMT_LIBRARIES)

message(STATUS "FMT: found :        ${FMT_FOUND}")
message(STATUS "FMT: include_dirs : ${FMT_INCLUDE_DIRS}")
message(STATUS "FMT: lib :          ${FMT_LIBRARIES}")
message(STATUS "FMT: version :      ${FMT_VERSION_STRING}")

if (FMT_FOUND AND NOT TARGET LLVM_STATIC_FMT)
  add_library(LLVM_STATIC_FMT UNKNOWN IMPORTED)
  set_target_properties(LLVM_STATIC_FMT PROPERTIES
                        IMPORTED_LOCATION ${FMT_LIBRARIES}
                        INTERFACE_INCLUDE_DIRECTORIES ${FMT_INCLUDE_DIRS})
  set(FMT_TARGET LLVM_STATIC_FMT)
endif()
