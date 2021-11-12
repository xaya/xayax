# Builds a Docker image that has the various Xaya X connectors installed,
# ready to run on a server.

# We use the libxayagame image as base; while Xaya X does not depend on
# libxayagame directly, it depends on many of the same base packages like
# SQLite or libjson-rpc-cpp, so that reusing them for the build from the
# existing package makes sense.
FROM xaya/libxayagame AS build
RUN apk add --no-cache \
  autoconf \
  autoconf-archive \
  automake \
  boost-dev \
  build-base \
  ca-certificates \
  cmake \
  git \
  gflags-dev \
  libtool \
  npm \
  pkgconfig \
  py3-pip \
  python3-dev
RUN pip3 install web3
RUN npm install -g truffle

# Build and install libunivalue.
ARG UNIVALUE_VERSION="v1.0.5"
WORKDIR /usr/src/univalue
RUN git clone -b ${UNIVALUE_VERSION} \
  https://github.com/jgarzik/univalue .
RUN ./autogen.sh && ./configure && make && make install-strip

# Build and install websocketpp.
ARG WEBSOCKETPP_VERSION="0.8.2"
WORKDIR /usr/src/websocketpp
RUN git clone -b ${WEBSOCKETPP_VERSION} \
  https://github.com/zaphoyd/websocketpp .
RUN cmake . && make && make install/strip

# Build and install Xaya X itself.  Make sure to clean out any
# potential garbage copied over in the build context.
WORKDIR /usr/src/xayax
COPY . .
RUN make distclean || true
RUN npm install
RUN ln -s ../../node_modules eth/solidity/node_modules
RUN ./autogen.sh && ./configure
# Build the solidity contracts first, so that they are already
# available for the python script that generates contract-constants.cpp.
RUN make -C eth/solidity
RUN make && make install-strip

# Collect the binaries and required libraries.
WORKDIR /jail
RUN mkdir bin
RUN for b in /usr/local/bin/xayax-*; \
  do \
    cp $b bin/; \
    cpld bin/$(basename $b) lib64; \
  done
COPY docker/entrypoint.sh bin/

# Construct the final image.
FROM alpine
COPY --from=build /jail /usr/local/
COPY --from=build /etc/ssl/certs/ca-certificates.crt /etc/ssl/certs/
LABEL description="Xaya X connector binaries"
VOLUME ["/log", "/xayax"]
ENV LD_LIBRARY_PATH "/usr/local/lib64"
ENV GLOG_log_dir "/log"
ENV HOST "127.0.0.1"
EXPOSE 8000
EXPOSE 28555
ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
