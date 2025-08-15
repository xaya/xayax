# Builds a Docker image that has the various Xaya X connectors installed,
# ready to run on a server.

# We use the libxayagame image as base; while Xaya X does not depend on
# libxayagame directly, it depends on libxayautil and many of the same base
# packages like SQLite or libjson-rpc-cpp, so that reusing them for the
# build from the existing package makes sense.
FROM xaya/libxayagame@sha256:2303174359025270fa7b4c931bd741d9455fad7173a2d0b2265ad63cc27fe77a AS build
RUN apt update && apt -y install \
  autoconf \
  autoconf-archive \
  automake \
  libboost-all-dev \
  libunivalue-dev \
  libwebsocketpp-dev \
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

# Install Foundry.  It seems pretty hard to get "foundryup" to run inside
# Docker, so we just download and manually install a specific release.
ARG FOUNDRY_RELEASE="nightly-1186156961f648273299ac067d098ee0e33a0d7e"
WORKDIR /usr/local/bin
RUN curl -L "https://github.com/foundry-rs/foundry/releases/download/${FOUNDRY_RELEASE}/foundry_nightly_linux_amd64.tar.gz" | tar zxv

# Build and install mypp.
ARG MYPP_COMMIT="c3895a6b157d1cb68e9d9c6835ec11b01278312c"
WORKDIR /usr/src/mypp
RUN git clone https://github.com/xaya/mypp . \
  && git checkout ${MYPP_COMMIT}
RUN ./autogen.sh && ./configure && make && make install-strip

# Build and install Xaya X itself.  Make sure to clean out any
# potential garbage copied over in the build context.
WORKDIR /usr/src/xayax
COPY . .
RUN pip3 install --break-system-packages -r requirements.txt
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
FROM debian:13-slim
COPY --from=build /jail /usr/local/
COPY --from=build /etc/ssl/certs/ca-certificates.crt /etc/ssl/certs/
LABEL description="Xaya X connector binaries"
# The image uses /log to store logs and /xayax to store the local state.
# These are not explicitly declared as VOLUME, though, since it is always
# possible to mount in a volume anyway, and without that, it is also possible
# to not use an explicit volume (e.g. for temporary testing containers).
RUN mkdir -p /log /xayax
ENV LD_LIBRARY_PATH="/usr/local/lib64"
ENV GLOG_log_dir="/log"
ENV HOST=""
ENV MAX_REORG_DEPTH="1000"
EXPOSE 8000
EXPOSE 28555
ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
