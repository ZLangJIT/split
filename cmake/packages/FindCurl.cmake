find_package(PkgConfig QUIET)
pkg_check_modules(PC_OPENSSL QUIET OPENSSL)

set(CMAKE_FIND_DEBUG_MODE FALSE) # TRUE)

find_path(CURL_INCLUDE_DIRS NAMES curl/curl.h
  PATHS ${LLVM_BUILD_ROOT__ROOTFS}/include
  NO_DEFAULT_PATH
  NO_PACKAGE_ROOT_PATH
  NO_CMAKE_PATH
  NO_CMAKE_ENVIRONMENT_PATH
  NO_SYSTEM_ENVIRONMENT_PATH
  NO_CMAKE_SYSTEM_PATH
  NO_CMAKE_FIND_ROOT_PATH
)
find_library(CURL_LIBRARIES NAMES libcurl.a libcurl-d.a
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

if(CURL_INCLUDE_DIRS AND EXISTS "${CURL_INCLUDE_DIRS}/curl/curl.h")
    set(CURL_VERSION_STRING "8.7.3")
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Curl
                                  FOUND_VAR
                                    CURL_FOUND
                                  REQUIRED_VARS
                                    CURL_INCLUDE_DIRS
                                    CURL_LIBRARIES
                                  VERSION_VAR
                                    CURL_VERSION_STRING)
mark_as_advanced(CURL_INCLUDE_DIRS CURL_LIBRARIES)

if (CURL_FOUND)
    find_package(OpenSSL REQUIRED)
    find_package(ZLIB REQUIRED)
endif()

message(STATUS "Curl: found :        ${CURL_FOUND}")
message(STATUS "Curl: include_dirs : ${CURL_INCLUDE_DIRS}")
message(STATUS "Curl: lib :          ${CURL_LIBRARIES}")
message(STATUS "Curl: version :      ${CURL_VERSION_STRING}")

if (CURL_FOUND AND NOT TARGET LLVM_STATIC_CURL)
  add_library(LLVM_STATIC_CURL UNKNOWN IMPORTED)
  set_target_properties(LLVM_STATIC_CURL PROPERTIES
                        IMPORTED_LOCATION ${CURL_LIBRARIES}
                        INTERFACE_INCLUDE_DIRECTORIES ${CURL_INCLUDE_DIRS})
  set_target_properties(LLVM_STATIC_CURL PROPERTIES INTERFACE_COMPILE_DEFINITIONS "CURL_STATICLIB")
  if (WIN32)
    set_target_properties(LLVM_STATIC_CURL PROPERTIES INTERFACE_LINK_LIBRARIES "${ZLIB_TARGET};${OPENSSL_TARGET};${OPENSSL_CRYPTO_TARGET};ws2_32")
  else()
    set_target_properties(LLVM_STATIC_CURL PROPERTIES INTERFACE_LINK_LIBRARIES "${ZLIB_TARGET};${OPENSSL_TARGET};${OPENSSL_CRYPTO_TARGET}")
  endif()
  set(CURL_TARGET LLVM_STATIC_CURL)
endif()
