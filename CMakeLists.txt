cmake_minimum_required(VERSION 3.16)
project(xayax VERSION 1.4.1 LANGUAGES CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_SHARED_LIBS "Build shared libraries" ON)

add_compile_options(-Wall -Werror -pedantic -Wno-deprecated)
add_compile_definitions(GLOG_NO_ABBREVIATED_SEVERITIES)

find_package(PkgConfig REQUIRED)
pkg_check_modules(JSONCPP REQUIRED jsoncpp)
pkg_check_modules(XAYAUTIL REQUIRED libxayautil)
pkg_check_modules(JSONRPCCLIENT REQUIRED libjsonrpccpp-client)
pkg_check_modules(JSONRPCSERVER REQUIRED libjsonrpccpp-server)
pkg_check_modules(GLOG REQUIRED libglog)
pkg_check_modules(PROTOBUF REQUIRED protobuf)
pkg_check_modules(SQLITE3 REQUIRED sqlite3)
pkg_check_modules(UNIVALUE REQUIRED libunivalue)
pkg_check_modules(MYPP REQUIRED mypp)
pkg_check_modules(MARIADB REQUIRED mariadb)
pkg_check_modules(ZMQ REQUIRED libzmq>=4.3.1)
pkg_check_modules(GFLAGS REQUIRED gflags)
pkg_check_modules(ETHUTILS REQUIRED ethutils)
pkg_check_modules(OPENSSL REQUIRED openssl)
pkg_check_modules(GTEST REQUIRED gmock gtest_main)

find_package(Boost REQUIRED COMPONENTS system)

include(CheckIncludeFileCXX)
set(CMAKE_REQUIRED_INCLUDES ${OPENSSL_INCLUDE_DIRS} ${Boost_INCLUDE_DIRS})
set(CMAKE_REQUIRED_LIBRARIES ${OPENSSL_LDFLAGS} ${Boost_LIBRARIES})
check_include_file_cxx("websocketpp/client.hpp" HAVE_WEBSOCKETPP)
if(NOT HAVE_WEBSOCKETPP)
  message(FATAL_ERROR "Failed to find websocketpp")
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(VENV_PYTHON "${CMAKE_BINARY_DIR}/venv/bin/python3")
set(VENV_STAMP "${CMAKE_BINARY_DIR}/venv/.stamp")

add_custom_command(
  OUTPUT ${VENV_STAMP}
  COMMAND ${Python3_EXECUTABLE} -m venv ${CMAKE_BINARY_DIR}/venv
  COMMAND ${VENV_PYTHON} -m pip install -r ${CMAKE_SOURCE_DIR}/requirements.txt
  COMMAND ${CMAKE_COMMAND} -E touch ${VENV_STAMP}
  COMMENT "Setting up Python venv"
)

add_custom_target(python-env ALL DEPENDS ${VENV_STAMP})

configure_file(
  ${CMAKE_SOURCE_DIR}/config.h.in
  ${CMAKE_BINARY_DIR}/config.h
)

include(CTest)

add_subdirectory(src)
add_subdirectory(xayacore)
add_subdirectory(eth)
add_subdirectory(xayax)
