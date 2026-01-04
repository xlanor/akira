FROM devkitpro/devkita64:latest

RUN apt-get update && apt-get install -y \
    autoconf automake libtool pkg-config \
    protobuf-compiler python3-protobuf \
    python3 python3-pip \
    && rm -rf /var/lib/apt/lists/*

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
    switch-libopus \
    switch-miniupnpc \
    dkp-toolchain-vars \
    && dkp-pacman -Scc --noconfirm

COPY library/ffmpeg /tmp/ffmpeg
WORKDIR /tmp/ffmpeg
RUN source ${DEVKITPRO}/switchvars.sh && \
    ./configure --prefix=${PORTLIBS_PREFIX} \
        --enable-gpl --disable-shared --enable-static \
        --cross-prefix=aarch64-none-elf- --enable-cross-compile \
        --arch=aarch64 --cpu=cortex-a57 --target-os=horizon \
        --enable-pic --disable-runtime-cpudetect \
        --disable-programs --disable-debug --disable-doc \
        --enable-asm --enable-neon --disable-autodetect \
        --disable-avdevice --disable-encoders --disable-muxers \
        --enable-swscale --enable-swresample \
        --enable-zlib --enable-bzlib --enable-libdav1d \
        --enable-nvtegra \
        --extra-cflags='-D__SWITCH__ -D_GNU_SOURCE -O2 -march=armv8-a -mtune=cortex-a57 -mtp=soft -fPIC -ftls-model=local-exec' \
        --extra-cxxflags='-D__SWITCH__ -D_GNU_SOURCE -O2 -march=armv8-a -mtune=cortex-a57 -mtp=soft -fPIC -ftls-model=local-exec' \
        --extra-ldflags='-fPIE -L${PORTLIBS_PREFIX}/lib -L${DEVKITPRO}/libnx/lib' && \
    make -j$(nproc) && \
    make DESTDIR="" install && \
    rm -rf /tmp/ffmpeg

WORKDIR /build
