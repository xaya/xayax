lib_LTLIBRARIES = libxayax.la
xayaxdir = $(includedir)/xayax

libxayax_la_CXXFLAGS = \
  $(JSONCPP_CFLAGS) \
  $(ZMQ_CFLAGS) $(SQLITE3_CFLAGS) $(GLOG_CFLAGS)
libxayax_la_LIBADD = \
  $(JSONCPP_LIBS) \
  $(ZMQ_LIBS) $(SQLITE3_LIBS) $(GLOG_LIBS)
libxayax_la_SOURCES = \
  database.cpp
xayax_HEADERS =
noinst_HEADERS = \
  private/database.hpp
