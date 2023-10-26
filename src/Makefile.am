lib_LTLIBRARIES = libxayax.la
xayaxdir = $(includedir)/xayax

RPC_STUBS = \
  rpc-stubs/xayarpcclient.h \
  rpc-stubs/xayarpcserverstub.h

PROTOS = \
  proto/blockdata.proto
PROTOHEADERS = $(PROTOS:.proto=.pb.h)
PROTOSOURCES = $(PROTOS:.proto=.pb.cc)

EXTRA_DIST = $(PROTOS) rpc-stubs/xaya.json
BUILT_SOURCES = $(RPC_STUBS) $(PROTOHEADERS)
CLEANFILES = $(RPC_STUBS) $(PROTOHEADERS) $(PROTOSOURCES)

libxayax_la_CXXFLAGS = \
  $(XAYAUTIL_CFLAGS) \
  $(JSONCPP_CFLAGS) $(JSONRPCSERVER_CFLAGS) \
  $(ZMQ_CFLAGS) $(SQLITE3_CFLAGS) $(UNIVALUE_CFLAGS) \
  $(PROTOBUF_CFLAGS) $(GFLAGS_CFLAGS) $(GLOG_CFLAGS)
libxayax_la_LIBADD = \
  $(XAYAUTIL_LIBS) \
  $(JSONCPP_LIBS) $(JSONRPCSERVER_LIBS) \
  $(ZMQ_LIBS) $(SQLITE3_LIBS) $(UNIVALUE_LIBS) \
  $(PROTOBUF_LIBS) $(GFLAGS_LIBS) $(GLOG_LIBS) \
  -lstdc++fs
libxayax_la_SOURCES = \
  basechain.cpp \
  blockdata.cpp \
  controller.cpp \
  chainstate.cpp \
  database.cpp \
  jsonutils.cpp \
  pending.cpp \
  rpcutils.cpp \
  sync.cpp \
  zmqpub.cpp \
  $(PROTOSOURCES)
xayax_HEADERS = \
  basechain.hpp \
  blockdata.hpp \
  controller.hpp \
  rpcutils.hpp
noinst_HEADERS = \
  private/database.hpp \
  private/chainstate.hpp \
  private/jsonutils.hpp \
  private/pending.hpp \
  private/sync.hpp \
  private/zmqpub.hpp \
  $(PROTOHEADERS) $(RPC_STUBS)

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
  blockdata_tests.cpp \
  chainstate_tests.cpp \
  controller_tests.cpp \
  jsonutils_tests.cpp \
  pending_tests.cpp \
  rpcutils_tests.cpp \
  sync_tests.cpp \
  testutils_tests.cpp \
  zmqpub_tests.cpp

rpc-stubs/xayarpcclient.h: $(srcdir)/rpc-stubs/xaya.json
	jsonrpcstub "$<" --cpp-client=XayaRpcClient --cpp-client-file="$@"
rpc-stubs/xayarpcserverstub.h: $(srcdir)/rpc-stubs/xaya.json
	jsonrpcstub "$<" --cpp-server=XayaRpcServerStub --cpp-server-file="$@"

proto/%.pb.h proto/%.pb.cc: $(top_srcdir)/src/proto/%.proto
	protoc -I$(top_srcdir) --cpp_out=$(top_builddir) "$<"
