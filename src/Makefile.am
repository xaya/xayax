lib_LTLIBRARIES = libxayax.la
xayaxdir = $(includedir)/xayax

libxayax_la_CXXFLAGS = \
  $(JSONCPP_CFLAGS) \
  $(ZMQ_CFLAGS) $(SQLITE3_CFLAGS) $(UNIVALUE_CFLAGS) $(GLOG_CFLAGS)
libxayax_la_LIBADD = \
  $(JSONCPP_LIBS) \
  $(ZMQ_LIBS) $(SQLITE3_LIBS) $(UNIVALUE_LIBS) $(GLOG_LIBS) \
  -lstdc++fs
libxayax_la_SOURCES = \
  basechain.cpp \
  controller.cpp \
  database.cpp \
  chainstate.cpp \
  sync.cpp \
  zmqpub.cpp
xayax_HEADERS = \
  basechain.hpp \
  controller.hpp \
  blockdata.hpp
noinst_HEADERS = \
  private/database.hpp \
  private/chainstate.hpp \
  private/sync.hpp \
  private/zmqpub.hpp

check_PROGRAMS = tests
TESTS = tests

tests_CXXFLAGS = \
  $(JSONCPP_CFLAGS) \
  $(ZMQ_CFLAGS) $(SQLITE3_CFLAGS) $(GLOG_CFLAGS) \
  $(GTEST_CFLAGS)
tests_LDADD = $(builddir)/libxayax.la \
  $(JSONCPP_LIBS) \
  $(ZMQ_LIBS) $(SQLITE3_LIBS) $(GLOG_LIBS) \
  $(GTEST_LIBS) \
  -lstdc++fs
check_HEADERS = testutils.hpp
tests_SOURCES = testutils.cpp \
  chainstate_tests.cpp \
  controller_tests.cpp \
  sync_tests.cpp \
  testutils_tests.cpp \
  zmqpub_tests.cpp
