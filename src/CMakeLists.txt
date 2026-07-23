find_package(Protobuf REQUIRED)

set(PROTO_SRCS ${CMAKE_CURRENT_BINARY_DIR}/proto/blockdata.pb.cc)
set(PROTO_HDRS ${CMAKE_CURRENT_BINARY_DIR}/proto/blockdata.pb.h)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/proto)

add_custom_command(
  OUTPUT ${PROTO_SRCS} ${PROTO_HDRS}
  COMMAND ${Protobuf_PROTOC_EXECUTABLE}
    -I${CMAKE_SOURCE_DIR}
    --cpp_out=${CMAKE_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/proto/blockdata.proto
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/proto/blockdata.proto
  COMMENT "Generating protobuf C++ files from blockdata.proto"
)

set(RPC_STUBS
  ${CMAKE_CURRENT_BINARY_DIR}/rpc-stubs/xayarpcclient.h
  ${CMAKE_CURRENT_BINARY_DIR}/rpc-stubs/xayarpcserverstub.h
)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/rpc-stubs)

add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/rpc-stubs/xayarpcclient.h
  COMMAND jsonrpcstub ${CMAKE_CURRENT_SOURCE_DIR}/rpc-stubs/xaya.json
    --cpp-client=XayaRpcClient
    --cpp-client-file=${CMAKE_CURRENT_BINARY_DIR}/rpc-stubs/xayarpcclient.h
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/rpc-stubs/xaya.json
  COMMENT "Generating XayaRpcClient stub"
)

add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/rpc-stubs/xayarpcserverstub.h
  COMMAND jsonrpcstub ${CMAKE_CURRENT_SOURCE_DIR}/rpc-stubs/xaya.json
    --cpp-server=XayaRpcServerStub
    --cpp-server-file=${CMAKE_CURRENT_BINARY_DIR}/rpc-stubs/xayarpcserverstub.h
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/rpc-stubs/xaya.json
  COMMENT "Generating XayaRpcServerStub stub"
)

set(XAYAX_SOURCES
  basechain.cpp
  blockcache.cpp
  blockdata.cpp
  cache/mysql.cpp
  controller.cpp
  chainstate.cpp
  database.cpp
  jsonutils.cpp
  pending.cpp
  rpcutils.cpp
  sync.cpp
  zmqpub.cpp
  ${PROTO_SRCS}
)

set(XAYAX_HEADERS
  basechain.hpp
  blockcache.hpp
  blockdata.hpp
  controller.hpp
  rpcutils.hpp
)

set(XAYAX_PRIVATE_HEADERS
  private/database.hpp
  private/chainstate.hpp
  private/jsonutils.hpp
  private/pending.hpp
  private/sync.hpp
  private/zmqpub.hpp
)

add_library(xayax ${XAYAX_SOURCES} ${PROTO_HDRS} ${RPC_STUBS})

target_include_directories(xayax
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}>
  PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/proto
    ${XAYAUTIL_INCLUDE_DIRS}
    ${JSONCPP_INCLUDE_DIRS}
    ${JSONRPCSERVER_INCLUDE_DIRS}
    ${ZMQ_INCLUDE_DIRS}
    ${SQLITE3_INCLUDE_DIRS}
    ${UNIVALUE_INCLUDE_DIRS}
    ${MYPP_INCLUDE_DIRS}
    ${MARIADB_INCLUDE_DIRS}
    ${PROTOBUF_INCLUDE_DIRS}
    ${GFLAGS_INCLUDE_DIRS}
    ${GLOG_INCLUDE_DIRS}
)

target_link_libraries(xayax
  PRIVATE
    ${XAYAUTIL_LDFLAGS}
    ${JSONCPP_LDFLAGS}
    ${JSONRPCSERVER_LDFLAGS}
    ${ZMQ_LDFLAGS}
    ${SQLITE3_LDFLAGS}
    ${UNIVALUE_LDFLAGS}
    ${MYPP_LDFLAGS}
    ${MARIADB_LDFLAGS}
    ${PROTOBUF_LDFLAGS}
    ${GFLAGS_LDFLAGS}
    ${GLOG_LDFLAGS}
    -lstdc++fs
)

install(TARGETS xayax
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(FILES ${XAYAX_HEADERS}
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/xayax
)

add_executable(tests
  testutils.cpp
  blockcache_tests.cpp
  blockdata_tests.cpp
  chainstate_tests.cpp
  controller_tests.cpp
  jsonutils_tests.cpp
  pending_tests.cpp
  rpcutils_tests.cpp
  sync_tests.cpp
  testutils_tests.cpp
  zmqpub_tests.cpp
)

target_include_directories(tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}
  ${JSONCPP_INCLUDE_DIRS}
  ${JSONRPCCLIENT_INCLUDE_DIRS}
  ${ZMQ_INCLUDE_DIRS}
  ${SQLITE3_INCLUDE_DIRS}
  ${GLOG_INCLUDE_DIRS}
  ${MYPP_INCLUDE_DIRS}
  ${MARIADB_INCLUDE_DIRS}
  ${GTEST_INCLUDE_DIRS}
)

target_link_libraries(tests
  xayax
  ${JSONCPP_LDFLAGS}
  ${JSONRPCCLIENT_LDFLAGS}
  ${ZMQ_LDFLAGS}
  ${SQLITE3_LDFLAGS}
  ${GLOG_LDFLAGS}
  ${MYPP_LDFLAGS}
  ${MARIADB_LDFLAGS}
  ${GTEST_LDFLAGS}
  -lstdc++fs
)

add_test(NAME xayax-unit-tests COMMAND tests)
