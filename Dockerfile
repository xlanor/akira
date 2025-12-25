FROM devkitpro/devkita64:latest

RUN apt-get update && apt-get install -y \
    autoconf automake libtool pkg-config \
    protobuf-compiler \
    python3 python3-pip \
    && rm -rf /var/lib/apt/lists/* \
    && pip3 install --break-system-packages protobuf grpcio-tools

RUN dkp-pacman -Syu --noconfirm && \
    dkp-pacman -S --noconfirm \
    switch-dev \
    switch-sdl2 \
    switch-mbedtls \
    switch-zlib \
    switch-bzip2 \
    switch-freetype \
    switch-libass \
    switch-libfribidi \
    switch-harfbuzz \
    switch-mesa \
    switch-glfw \
    switch-glad \
    switch-libjson-c \
    switch-dav1d \
    switch-ffmpeg \
    switch-libopus \
    switch-miniupnpc \
    dkp-toolchain-vars \
    && dkp-pacman -Scc --noconfirm

WORKDIR /build
