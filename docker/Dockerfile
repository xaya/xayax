# Builds a Docker image that has the various Xaya X connectors installed,
# ready to run on a server.

# We use the libxayagame image as base; while Xaya X does not depend on
# libxayagame directly, it depends on libxayautil and many of the same base
# packages like SQLite or libjson-rpc-cpp, so that reusing them for the
# build from the existing package makes sense.
#
# It seems very hard to get the pre-built foundry binaries to run on Alpine,
# so we base the image on Ubuntu instead.
FROM xaya/libxayagame:ubuntu AS build
RUN apt update && apt -y install \
  autoconf \
  autoconf-archive \
  automake \
  libboost-all-dev \
  build-essential \
  cmake \
  curl \
  git \
  libgflags-dev \
  libtool \
  libmariadb-dev \
  pkg-config \
  python3-pip \
  python3-dev
RUN pip3 install web3

# Install Foundry.  It seems pretty hard to get "foundryup" to run inside
# Docker, so we just download and manually install a specific release.
ARG FOUNDRY_RELEASE="nightly-adb6abae69c7a0d766db123f66686cc890c22dd0"
WORKDIR /usr/local/bin
RUN curl -L "https://github.com/foundry-rs/foundry/releases/download/${FOUNDRY_RELEASE}/foundry_nightly_linux_amd64.tar.gz" | tar zxv

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

# Build and install mypp.
ARG MYPP_VERSION="master"
WORKDIR /usr/src/mypp
RUN git clone -b ${MYPP_VERSION} \
  https://github.com/xaya/mypp .
RUN ./autogen.sh && ./configure && make && make install-strip

# Build and install Xaya X itself.  Make sure to clean out any
# potential garbage copied over in the build context.
WORKDIR /usr/src/xayax
COPY . .
RUN make distclean || true
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
FROM ubuntu:22.04
COPY --from=build /jail /usr/local/
COPY --from=build /etc/ssl/certs/ca-certificates.crt /etc/ssl/certs/
LABEL description="Xaya X connector binaries"
# The image uses /log to store logs and /xayax to store the local state.
# These are not explicitly declared as VOLUME, though, since it is always
# possible to mount in a volume anyway, and without that, it is also possible
# to not use an explicit volume (e.g. for temporary testing containers).
RUN mkdir -p /log /xayax
ENV LD_LIBRARY_PATH "/usr/local/lib64"
ENV GLOG_log_dir "/log"
ENV HOST ""
ENV MAX_REORG_DEPTH "1000"
EXPOSE 8000
EXPOSE 28555
ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
