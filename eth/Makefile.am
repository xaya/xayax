SUBDIRS = solidity tests

noinst_PYTHON = gen-contract-constants.py
noinst_LTLIBRARIES = libethchain.la
bin_PROGRAMS = xayax-eth

EXTRA_DIST = rpc-stubs/eth.json

RPC_STUBS = \
  rpc-stubs/ethrpcclient.h
BUILT_SOURCES = contract-constants.cpp $(RPC_STUBS)
CLEANFILES = contract-constants.cpp $(RPC_STUBS)

libethchain_la_CXXFLAGS = \
  -I$(top_srcdir)/src \
  $(ETHUTILS_CFLAGS) \
  $(JSONCPP_CFLAGS) $(JSONRPCCLIENT_CFLAGS) \
  $(WEBSOCKET_CFLAGS) $(GFLAGS_CFLAGS) $(GLOG_CFLAGS)
libethchain_la_LIBADD = \
  $(top_builddir)/src/libxayax.la \
  $(ETHUTILS_LIBS) \
  $(JSONCPP_LIBS) $(JSONRPCCLIENT_LIBS) \
  $(WEBSOCKET_LIBS) $(GFLAGS_LIBS) $(GLOG_LIBS)
libethchain_la_SOURCES = \
  contract-constants.cpp \
  ethchain.cpp \
  hexutils.cpp \
  pending.cpp \
  websocket.cpp
noinst_HEADERS = \
  contract-constants.hpp \
  ethchain.hpp \
  hexutils.hpp \
  pending.hpp \
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
  -I$(top_srcdir)/src \
  $(GLOG_CFLAGS) $(GTEST_CFLAGS)
tests_unit_LDADD = $(builddir)/libethchain.la \
  $(GLOG_LIBS) $(GTEST_LIBS)
tests_unit_SOURCES = \
  pending_tests.cpp

contract-constants.cpp: gen-contract-constants.py
	$(srcdir)/gen-contract-constants.py >$@
	
rpc-stubs/ethrpcclient.h: $(srcdir)/rpc-stubs/eth.json
	jsonrpcstub "$<" --cpp-client=EthRpcClient --cpp-client-file="$@"
