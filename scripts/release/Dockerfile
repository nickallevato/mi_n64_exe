# Release build environment: Ubuntu 22.04 (glibc 2.35), so the binaries run on most current Linux distros.
# Mirrors BanjoRecomp's CI: clang 15 (with GCC 12's libstdc++ for constexpr std::string, linked statically by the
# build script), and SDL2 built from source (with Wayland, X11, PulseAudio and ALSA).
FROM ubuntu:22.04

ARG DEBIAN_FRONTEND=noninteractive
ARG SDL2_VERSION=2.30.3

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential ca-certificates cmake curl file git ninja-build pkg-config \
        g++-12 libstdc++-12-dev \
        clang-15 lld-15 llvm-15 \
        libdbus-1-dev libfreetype-dev libgtk-3-dev zlib1g-dev \
        libasound2-dev libpulse-dev libsamplerate0-dev \
        libwayland-dev wayland-protocols libxkbcommon-dev libdecor-0-dev libegl-dev libdrm-dev libgbm-dev \
        libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev libxfixes-dev \
    && rm -rf /var/lib/apt/lists/*

RUN curl -sSfL "https://github.com/libsdl-org/SDL/releases/download/release-${SDL2_VERSION}/SDL2-${SDL2_VERSION}.tar.gz" \
        | tar -xz -C /tmp \
    && cd /tmp/SDL2-${SDL2_VERSION} && ./configure --prefix=/usr/local && make -j"$(nproc)" && make install \
    && rm -rf /tmp/SDL2-${SDL2_VERSION} && ldconfig

# linuxdeploy, extracted so it runs without FUSE.
RUN curl -sSfL -o /tmp/linuxdeploy.AppImage \
        "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage" \
    && chmod +x /tmp/linuxdeploy.AppImage && cd /opt && /tmp/linuxdeploy.AppImage --appimage-extract \
    && mv /opt/squashfs-root /opt/linuxdeploy && chmod -R a+rX /opt/linuxdeploy && rm /tmp/linuxdeploy.AppImage

RUN git config --system --add safe.directory '*'
