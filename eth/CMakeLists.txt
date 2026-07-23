set(RPC_STUBS
  ${CMAKE_CURRENT_BINARY_DIR}/rpc-stubs/ethrpcclient.h
)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/rpc-stubs)

add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/rpc-stubs/ethrpcclient.h
  COMMAND jsonrpcstub ${CMAKE_CURRENT_SOURCE_DIR}/rpc-stubs/eth.json
    --cpp-client=EthRpcClient
    --cpp-client-file=${CMAKE_CURRENT_BINARY_DIR}/rpc-stubs/ethrpcclient.h
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/rpc-stubs/eth.json
  COMMENT "Generating EthRpcClient stub"
)

add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/contract-constants.cpp
  COMMAND ${VENV_PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/gen-contract-constants.py
    > ${CMAKE_CURRENT_BINARY_DIR}/contract-constants.cpp
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/gen-contract-constants.py
    python-env
  COMMENT "Generating contract constants from Solidity build"
)

set(ETHCHAIN_SOURCES
  contract-constants.cpp
  ethchain.cpp
  hexutils.cpp
  pending.cpp
  websocket.cpp
)

set(ETHCHAIN_HEADERS
  contract-constants.hpp
  ethchain.hpp
  hexutils.hpp
  pending.hpp
  websocket.hpp
)

add_library(ethchain STATIC
  ${ETHCHAIN_SOURCES}
  ${ETHCHAIN_HEADERS}
  ${RPC_STUBS}
)

add_dependencies(ethchain solidity-build)

target_include_directories(ethchain
  PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${ETHUTILS_INCLUDE_DIRS}
    ${JSONCPP_INCLUDE_DIRS}
    ${JSONRPCCLIENT_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIRS}
    ${Boost_INCLUDE_DIRS}
    ${GFLAGS_INCLUDE_DIRS}
    ${GLOG_INCLUDE_DIRS}
)

target_link_libraries(ethchain
  xayax
  ${ETHUTILS_LDFLAGS}
  ${JSONCPP_LDFLAGS}
  ${JSONRPCCLIENT_LDFLAGS}
  ${OPENSSL_LDFLAGS}
  Boost::system
  ${GFLAGS_LDFLAGS}
  ${GLOG_LDFLAGS}
)

add_executable(xayax-eth main.cpp)

target_compile_definitions(xayax-eth PRIVATE
  PACKAGE_VERSION="${PROJECT_VERSION}"
)

target_include_directories(xayax-eth PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${JSONCPP_INCLUDE_DIRS}
  ${OPENSSL_INCLUDE_DIRS}
  ${Boost_INCLUDE_DIRS}
  ${GFLAGS_INCLUDE_DIRS}
  ${GLOG_INCLUDE_DIRS}
)

target_link_libraries(xayax-eth
  ethchain
  xayax
  ${JSONCPP_LDFLAGS}
  ${OPENSSL_LDFLAGS}
  Boost::system
  ${GFLAGS_LDFLAGS}
  ${GLOG_LDFLAGS}
  -pthread
)

add_executable(tests-unit
  pending_tests.cpp
)

target_include_directories(tests-unit PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${JSONCPP_INCLUDE_DIRS}
  ${GLOG_INCLUDE_DIRS}
  ${GTEST_INCLUDE_DIRS}
)

target_link_libraries(tests-unit
  ethchain
  ${JSONCPP_LDFLAGS}
  ${GLOG_LDFLAGS}
  ${GTEST_LDFLAGS}
)

add_test(NAME eth-unit-tests COMMAND tests-unit)

install(TARGETS xayax-eth
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

add_subdirectory(solidity)
add_subdirectory(tests)
