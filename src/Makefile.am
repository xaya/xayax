lib_LTLIBRARIES = libxayax.la
xayaxdir = $(includedir)/xayax

EXTRA_DIST = rpc-stubs/xaya.json

RPC_STUBS = \
  rpc-stubs/xayarpcclient.h \
  rpc-stubs/xayarpcserverstub.h
BUILT_SOURCES = $(RPC_STUBS)
CLEANFILES = $(RPC_STUBS)

libxayax_la_CXXFLAGS = \
  $(JSONCPP_CFLAGS) $(JSONRPCSERVER_CFLAGS) \
  $(ZMQ_CFLAGS) $(SQLITE3_CFLAGS) $(UNIVALUE_CFLAGS) \
  $(GFLAGS_CFLAGS) $(GLOG_CFLAGS)
libxayax_la_LIBADD = \
  $(JSONCPP_LIBS) $(JSONRPCSERVER_LIBS) \
  $(ZMQ_LIBS) $(SQLITE3_LIBS) $(UNIVALUE_LIBS) \
  $(GFLAGS_LIBS) $(GLOG_LIBS) \
  -lstdc++fs
libxayax_la_SOURCES = \
  basechain.cpp \
  controller.cpp \
  chainstate.cpp \
  database.cpp \
  pending.cpp \
  sync.cpp \
  zmqpub.cpp
xayax_HEADERS = \
  basechain.hpp \
  blockdata.hpp \
  controller.hpp \
  rpcutils.hpp
noinst_HEADERS = \
  private/database.hpp \
  private/chainstate.hpp \
  private/pending.hpp \
  private/sync.hpp \
  private/zmqpub.hpp \
  $(RPC_STUBS)

check_PROGRAMS = tests
TESTS = tests

tests_CXXFLAGS = \
  $(JSONCPP_CFLAGS) $(JSONRPCCLIENT_CFLAGS) \
  $(ZMQ_CFLAGS) $(SQLITE3_CFLAGS) $(GLOG_CFLAGS) \
  $(GTEST_CFLAGS)
tests_LDADD = $(builddir)/libxayax.la \
  $(JSONCPP_LIBS) $(JSONRPCCLIENT_LIBS) \
  $(ZMQ_LIBS) $(SQLITE3_LIBS) $(GLOG_LIBS) \
  $(GTEST_LIBS) \
  -lstdc++fs
check_HEADERS = testutils.hpp
tests_SOURCES = testutils.cpp \
  chainstate_tests.cpp \
  controller_tests.cpp \
  pending_tests.cpp \
  sync_tests.cpp \
  testutils_tests.cpp \
  zmqpub_tests.cpp

rpc-stubs/xayarpcclient.h: $(srcdir)/rpc-stubs/xaya.json
	jsonrpcstub "$<" --cpp-client=XayaRpcClient --cpp-client-file="$@"
rpc-stubs/xayarpcserverstub.h: $(srcdir)/rpc-stubs/xaya.json
	jsonrpcstub "$<" --cpp-server=XayaRpcServerStub --cpp-server-file="$@"
