cmake_minimum_required(VERSION 3.12)

project(llvm_project_prebuilt_deps C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_POLICY_DEFAULT_CMP0074 NEW)
set(CMAKE_POLICY_DEFAULT_CMP0075 NEW)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

include(./build_root.cmake)

build_root_init(${CMAKE_CURRENT_SOURCE_DIR}/cmake/packages ${CMAKE_CURRENT_BINARY_DIR}/BUILD_ROOT)

if (true)

# tmpfile does not depend on any libs
build_root_add_cmake_package(${CMAKE_CURRENT_SOURCE_DIR}/deps/tmpfile . tmpfile
    "" # no extra c flags
    "" # no extra cxx flags
    "
      -D BUILD_SHARED_LIBS=OFF
    "
)

# fmt does not depend on any libs
build_root_add_cmake_package(${CMAKE_CURRENT_SOURCE_DIR}/deps/fmt . fmt
    "" # no extra c flags
    "" # no extra cxx flags
    "
      -D BUILD_SHARED_LIBS=OFF
    "
)

# openssl does not depend on any libs
build_root_add_cmake_package(${CMAKE_CURRENT_SOURCE_DIR}/deps/openssl-cmake . openssl
    "" # no extra c flags
    "" # no extra cxx flags
    "
      -D BUILD_SHARED_LIBS=OFF
    "
)

# zlib does not depend on any libs
build_root_add_cmake_package(${CMAKE_CURRENT_SOURCE_DIR}/deps/zlib . zlib
    "" # no extra c flags
    "" # no extra cxx flags
    "
      -D BUILD_SHARED_LIBS=OFF
    "
)

# curl depends on openssl and zlib
build_root_add_cmake_package(${CMAKE_CURRENT_SOURCE_DIR}/deps/curl . curl
    "" # no extra c flags
    "" # no extra cxx flags
    "
      -D BUILD_SHARED_LIBS=OFF
    "
)
endif()

find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(Curl REQUIRED)
find_package(Fmt REQUIRED)
find_package(TMPFILE REQUIRED)

add_executable(split split_main.cpp)
target_compile_definitions(split PUBLIC "NOMINMAX")
target_include_directories(split PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_directories(split PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(split PRIVATE ${CURL_TARGET} ${ZLIB_TARGET} ${OPENSSL_TARGET} ${OPENSSL_CRYPTO_TARGET} ${FMT_TARGET} ${TMPFILE_TARGET})
target_link_options(split PRIVATE -static)

set(INSTALL_BIN_DIR "${CMAKE_INSTALL_PREFIX}/bin" CACHE PATH "Installation directory for executables")
set(INSTALL_LIB_DIR "${CMAKE_INSTALL_PREFIX}/lib" CACHE PATH "Installation directory for libraries")
set(INSTALL_INC_DIR "${CMAKE_INSTALL_PREFIX}/include" CACHE PATH "Installation directory for headers")

install(TARGETS split
        RUNTIME DESTINATION "${INSTALL_BIN_DIR}"
        ARCHIVE DESTINATION "${INSTALL_LIB_DIR}"
        LIBRARY DESTINATION "${INSTALL_LIB_DIR}" )
