lib_LTLIBRARIES = libxayax.la
xayaxdir = $(includedir)/xayax

libxayax_la_CXXFLAGS = \
  $(JSONCPP_CFLAGS) \
  $(ZMQ_CFLAGS) $(SQLITE3_CFLAGS) $(GLOG_CFLAGS)
libxayax_la_LIBADD = \
  $(JSONCPP_LIBS) \
  $(ZMQ_LIBS) $(SQLITE3_LIBS) $(GLOG_LIBS)
libxayax_la_SOURCES = \
  database.cpp \
  chainstate.cpp
xayax_HEADERS = \
  blockdata.hpp
noinst_HEADERS = \
  private/database.hpp \
  private/chainstate.hpp

check_PROGRAMS = tests
TESTS = tests

tests_CXXFLAGS = \
  $(JSONCPP_CFLAGS) \
  $(SQLITE3_CFLAGS) $(GLOG_CFLAGS) \
  $(GTEST_CFLAGS)
tests_LDADD = $(builddir)/libxayax.la \
  $(JSONCPP_LIBS) \
  $(SQLITE3_LIBS) $(GLOG_LIBS) \
  $(GTEST_LIBS)
check_HEADERS = testutils.hpp
tests_SOURCES = testutils.cpp \
  chainstate_tests.cpp
