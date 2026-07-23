# Builds a Docker image that has the various Xaya X connectors installed,
# ready to run on a server.

# We use the libxayagame image as base; while Xaya X does not depend on
# libxayagame directly, it depends on libxayautil and many of the same base
# packages like SQLite or libjson-rpc-cpp, so that reusing them for the
# build from the existing package makes sense.
FROM xaya/libxayagame:0afa48e35b6d83095d85daa6b8923cec550f8508 AS build
RUN apt update && apt -y install --no-install-recommends \
  build-essential \
  ca-certificates \
  cmake \
  curl \
  git \
  libboost-all-dev \
  libgflags-dev \
  libmariadb-dev \
  libunivalue-dev \
  libwebsocketpp-dev \
  pkg-config \
  python3-dev \
  python3-venv

# Number of parallel cores to use for make builds.
ARG N=1

# Install Foundry.  It seems pretty hard to get "foundryup" to run inside
# Docker, so we just download and manually install a specific release.
ARG FOUNDRY_RELEASE="v1.7.1"
WORKDIR /usr/local/bin
RUN curl -L "https://github.com/foundry-rs/foundry/releases/download/${FOUNDRY_RELEASE}/foundry_${FOUNDRY_RELEASE}_linux_amd64.tar.gz" | tar zxv

# Build and install mypp.
ARG MYPP_COMMIT="abd1f63b8f92543c929b642f49cd81617594b12e"
WORKDIR /usr/src/mypp
RUN git clone https://github.com/xaya/mypp . \
  && git checkout ${MYPP_COMMIT}
RUN rm -rf build && cmake -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build -j${N} && cmake --install build --strip

# Build and install Xaya X itself.  Make sure to clean out any
# potential garbage copied over in the build context.
WORKDIR /usr/src/xayax
COPY . .
RUN rm -rf build && cmake -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build -j${N} && cmake --install build --strip

# Collect the binaries and required libraries.
WORKDIR /jail
RUN mkdir bin
RUN for b in /usr/local/bin/xayax-*; \
  do \
    cp $b bin/; \
    cpld bin/$(basename $b) lib64; \
  done
COPY docker/entrypoint.sh bin/
COPY docker/healthcheck.sh bin/

# Construct the final image.
FROM debian:13-slim
# Install tini as a lightweight init
RUN apt-get update && \
    apt-get upgrade -y && \
    apt-get install -y --no-install-recommends curl tini && \
    rm -rf /var/lib/apt/lists/*

COPY --from=build /jail /usr/local/
COPY --from=build /etc/ssl/certs/ca-certificates.crt /etc/ssl/certs/
# The bundle above is a single file without hashed symlinks, so OpenSSL's
# default directory lookup cannot find it; point directly at the file so
# TLS verification (e.g. wss:// websocket endpoints) works.
ENV SSL_CERT_FILE=/etc/ssl/certs/ca-certificates.crt
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
ENTRYPOINT ["/usr/bin/tini", "-v", "--", "/usr/local/bin/entrypoint.sh"]
