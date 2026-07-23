set(RPC_STUBS
  ${CMAKE_CURRENT_BINARY_DIR}/rpc-stubs/corerpcclient.h
)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/rpc-stubs)

add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/rpc-stubs/corerpcclient.h
  COMMAND jsonrpcstub ${CMAKE_CURRENT_SOURCE_DIR}/rpc-stubs/core.json
    --cpp-client=CoreRpcClient
    --cpp-client-file=${CMAKE_CURRENT_BINARY_DIR}/rpc-stubs/corerpcclient.h
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/rpc-stubs/core.json
  COMMENT "Generating CoreRpcClient stub"
)

add_executable(xayax-core
  main.cpp
  corechain.cpp
  ${RPC_STUBS}
)

target_compile_definitions(xayax-core PRIVATE
  PACKAGE_VERSION="${PROJECT_VERSION}"
)

target_include_directories(xayax-core PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_CURRENT_BINARY_DIR}
  ${XAYAUTIL_INCLUDE_DIRS}
  ${ETHUTILS_INCLUDE_DIRS}
  ${JSONCPP_INCLUDE_DIRS}
  ${JSONRPCCLIENT_INCLUDE_DIRS}
  ${ZMQ_INCLUDE_DIRS}
  ${GFLAGS_INCLUDE_DIRS}
  ${GLOG_INCLUDE_DIRS}
)

target_link_libraries(xayax-core
  xayax
  ${XAYAUTIL_LDFLAGS}
  ${ETHUTILS_LDFLAGS}
  ${JSONCPP_LDFLAGS}
  ${JSONRPCCLIENT_LDFLAGS}
  ${ZMQ_LDFLAGS}
  ${GFLAGS_LDFLAGS}
  ${GLOG_LDFLAGS}
  -pthread
)

install(TARGETS xayax-core
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

add_subdirectory(tests)
