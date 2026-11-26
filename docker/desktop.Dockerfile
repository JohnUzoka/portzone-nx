# Desktop (Linux) build environment for PortZone. Matches the host's Ubuntu so the binary runs under WSLg.
FROM ubuntu:26.04
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      build-essential cmake ninja-build pkg-config git ca-certificates \
      xorg-dev libgl-dev libegl-dev libwayland-dev libxkbcommon-dev wayland-protocols \
      libcurl4-openssl-dev zlib1g-dev libdbus-1-dev \
    && rm -rf /var/lib/apt/lists/*
