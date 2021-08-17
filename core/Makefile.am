SUBDIRS = tests

bin_PROGRAMS = xayax-core

EXTRA_DIST = rpc-stubs/core.json

RPC_STUBS = \
  rpc-stubs/corerpcclient.h
BUILT_SOURCES = $(RPC_STUBS)
CLEANFILES = $(RPC_STUBS)

xayax_core_CXXFLAGS = \
  -I$(top_srcdir)/src \
  $(JSONCPP_CFLAGS) $(JSONRPCCLIENT_CFLAGS) $(ZMQ_CFLAGS) \
  $(GFLAGS_CFLAGS) $(GLOG_CFLAGS)
xayax_core_LDADD = \
  $(top_builddir)/src/libxayax.la \
  $(JSONCPP_LIBS) $(JSONRPCCLIENT_LIBS) $(ZMQ_LIBS) \
  $(GFLAGS_LIBS) $(GLOG_LIBS)
xayax_core_SOURCES = main.cpp \
  corechain.cpp
noinst_HEADERS = \
  corechain.hpp \
  $(RPC_STUBS)

rpc-stubs/corerpcclient.h: $(srcdir)/rpc-stubs/core.json
	jsonrpcstub "$<" --cpp-client=CoreRpcClient --cpp-client-file="$@"
