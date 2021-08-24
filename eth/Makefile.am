bin_PROGRAMS = xayax-eth

EXTRA_DIST = rpc-stubs/eth.json

RPC_STUBS = \
  rpc-stubs/ethrpcclient.h
BUILT_SOURCES = $(RPC_STUBS)
CLEANFILES = $(RPC_STUBS)

xayax_eth_CXXFLAGS = \
  -I$(top_srcdir)/src \
  $(JSONCPP_CFLAGS) $(JSONRPCCLIENT_CFLAGS) \
  $(GFLAGS_CFLAGS) $(GLOG_CFLAGS)
xayax_eth_LDADD = \
  $(top_builddir)/src/libxayax.la \
  $(JSONCPP_LIBS) $(JSONRPCCLIENT_LIBS) \
  $(GFLAGS_LIBS) $(GLOG_LIBS)
xayax_eth_SOURCES = main.cpp \
  ethchain.cpp
noinst_HEADERS = \
  ethchain.hpp \
  $(RPC_STUBS)

rpc-stubs/ethrpcclient.h: $(srcdir)/rpc-stubs/eth.json
	jsonrpcstub "$<" --cpp-client=EthRpcClient --cpp-client-file="$@"
