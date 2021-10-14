SUBDIRS = solidity tests

noinst_LTLIBRARIES = libethchain.la
bin_PROGRAMS = xayax-eth

EXTRA_DIST = rpc-stubs/eth.json

RPC_STUBS = \
  rpc-stubs/ethrpcclient.h
BUILT_SOURCES = $(RPC_STUBS)
CLEANFILES = $(RPC_STUBS)

libethchain_la_CXXFLAGS = \
  -I$(top_srcdir)/src \
  $(JSONCPP_CFLAGS) $(JSONRPCCLIENT_CFLAGS) \
  $(WEBSOCKET_CFLAGS) $(GFLAGS_CFLAGS) $(GLOG_CFLAGS)
libethchain_la_LIBADD = \
  $(top_builddir)/src/libxayax.la \
  $(JSONCPP_LIBS) $(JSONRPCCLIENT_LIBS) \
  $(WEBSOCKET_LIBS) $(GFLAGS_LIBS) $(GLOG_LIBS)
libethchain_la_SOURCES = \
  abi.cpp \
  ethchain.cpp \
  websocket.cpp
noinst_HEADERS = \
  abi.hpp \
  ethchain.hpp \
  websocket.hpp \
  $(RPC_STUBS)

xayax_eth_CXXFLAGS = \
  -I$(top_srcdir)/src \
  $(WEBSOCKET_CFLAGS) $(GFLAGS_CFLAGS) $(GLOG_CFLAGS)
xayax_eth_LDADD = \
  $(builddir)/libethchain.la \
  $(top_builddir)/src/libxayax.la \
  $(WEBSOCKET_LIBS) $(GFLAGS_LIBS) $(GLOG_LIBS)
xayax_eth_SOURCES = main.cpp

check_PROGRAMS = tests-unit
TESTS = tests-unit

tests_unit_CXXFLAGS = \
  $(GLOG_CFLAGS) $(GTEST_CFLAGS)
tests_unit_LDADD = $(builddir)/libethchain.la \
  $(GLOG_LIBS) $(GTEST_LIBS)
tests_unit_SOURCES = \
  abi_tests.cpp

rpc-stubs/ethrpcclient.h: $(srcdir)/rpc-stubs/eth.json
	jsonrpcstub "$<" --cpp-client=EthRpcClient --cpp-client-file="$@"
