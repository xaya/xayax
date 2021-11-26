AC_INIT([xayax], [0.1])
AM_INIT_AUTOMAKE([subdir-objects])

AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_HEADERS([config.h])
AM_SILENT_RULES([yes])

AC_PROG_CXX
AM_PROG_AR
AM_PATH_PYTHON([3.7])
PKG_INSTALLDIR

AC_LANG([C++])
LT_INIT

AX_CXX_COMPILE_STDCXX([14], [noext])
AX_CHECK_COMPILE_FLAG([-Wall], [CXXFLAGS="${CXXFLAGS} -Wall"])
AX_CHECK_COMPILE_FLAG([-Werror], [CXXFLAGS="${CXXFLAGS} -Werror"])
AX_CHECK_COMPILE_FLAG([-pedantic], [CXXFLAGS="${CXXFLAGS} -pedantic"])
AX_CHECK_COMPILE_FLAG([-Wno-deprecated],
                      [CXXFLAGS="${CXXFLAGS} -Wno-deprecated"])

# We need websocketpp, which does not have a pkg-config file.
AC_CHECK_HEADER([websocketpp/client.hpp], [],
                [AC_MSG_ERROR([Failed to find websocketpp])])

# Windows defines ERROR, which requires us to tell glog to not define
# it as abbreviated log severity (LOG(ERROR) still works, though, and
# that is all that we actually use in the code).
# See https://hpc.nih.gov/development/glog.html.
CXXFLAGS="${CXXFLAGS} -DGLOG_NO_ABBREVIATED_SEVERITIES"

# Public dependencies (exposed in the headers of xayax to
# users of the library).
AX_PKG_CHECK_MODULES([JSONCPP], [jsoncpp], [])

# Private dependencies of the library itself.
AX_PKG_CHECK_MODULES([ETHUTILS], [], [ethutils])
AX_PKG_CHECK_MODULES([JSONRPCCLIENT], [], [libjsonrpccpp-client])
AX_PKG_CHECK_MODULES([JSONRPCSERVER], [], [libjsonrpccpp-server])
AX_PKG_CHECK_MODULES([GLOG], [], [libglog])
AX_PKG_CHECK_MODULES([SQLITE3], [], [sqlite3])
AX_PKG_CHECK_MODULES([UNIVALUE], [], [libunivalue])
# We use the recv variant taking message_t& over deprecated older functions,
# which requires at least version 4.3.1.
AX_PKG_CHECK_MODULES([ZMQ], [], [libzmq >= 4.3.1])
AX_PKG_CHECK_MODULES([GFLAGS], [], [gflags])

# Private dependencies that are not needed for the library, but only for
# the unit tests and the binaries.
PKG_CHECK_MODULES([GTEST], [gmock gtest_main])

# Stuff that we need for websocketpp.
PKG_CHECK_MODULES([OPENSSL], [openssl])
AX_BOOST_BASE([1.64.0], [], [AC_MSG_ERROR([Boost is not available])])
# FIXME: Checking for boost_system seems to not work with the
# macro (even though it is installed).  Thus we are for now just
# manually adding -lboost_system to the flags.
#AX_BOOST_SYSTEM
WEBSOCKET_CFLAGS="${OPENSSL_CFLAGS} ${BOOST_CPPFLAGS}"
WEBSOCKET_LIBS="${OPENSSL_LIBS} ${BOOST_LDFLAGS} ${BOOST_SYSTEM_LIB}"
WEBSOCKET_LIBS+=" -lboost_system"
AC_SUBST(WEBSOCKET_CFLAGS)
AC_SUBST(WEBSOCKET_LIBS)

AC_CONFIG_FILES([
  Makefile \
  eth/Makefile \
  eth/solidity/Makefile \
  eth/tests/Makefile \
  src/Makefile \
  xayacore/Makefile \
  xayacore/tests/Makefile \
  xayax/Makefile
])
AC_OUTPUT

echo
echo "CXXFLAGS: ${CXXFLAGS}"
