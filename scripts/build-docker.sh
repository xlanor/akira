#!/bin/bash
set -e
source /opt/devkitpro/switchvars.sh

BUILD_DIR="/build"

build_akira() {
    echo "=== Building Akira ==="
    cd ${BUILD_DIR}

    echo "=== Generating build info ==="

    CHIAKI_VERSION=$(grep -E "^set\(CHIAKI_VERSION " ${BUILD_DIR}/library/chiaki-ng/CMakeLists.txt | sed 's/.*CHIAKI_VERSION \([0-9.]*\).*/\1/' || echo "unknown")
    if [ "$CHIAKI_VERSION" = "unknown" ]; then
        CHIAKI_MAJOR=$(grep "CHIAKI_VERSION_MAJOR" ${BUILD_DIR}/library/chiaki-ng/CMakeLists.txt | head -1 | sed 's/[^0-9]*//g')
        CHIAKI_MINOR=$(grep "CHIAKI_VERSION_MINOR" ${BUILD_DIR}/library/chiaki-ng/CMakeLists.txt | head -1 | sed 's/[^0-9]*//g')
        CHIAKI_PATCH=$(grep "CHIAKI_VERSION_PATCH" ${BUILD_DIR}/library/chiaki-ng/CMakeLists.txt | head -1 | sed 's/[^0-9]*//g')
        CHIAKI_VERSION="${CHIAKI_MAJOR}.${CHIAKI_MINOR}.${CHIAKI_PATCH}"
    fi

    CURL_VERSION=$(curl-config --version 2>/dev/null | sed 's/libcurl //' || echo "unknown")
    FFMPEG_VERSION=$(pkg-config --modversion libavcodec 2>/dev/null || echo "unknown")

    AKIRA_MAJOR=$(grep 'set(VERSION_MAJOR' ${BUILD_DIR}/CMakeLists.txt | sed 's/[^0-9]*//g')
    AKIRA_MINOR=$(grep 'set(VERSION_MINOR' ${BUILD_DIR}/CMakeLists.txt | sed 's/[^0-9]*//g')
    AKIRA_REVISION=$(grep 'set(VERSION_REVISION' ${BUILD_DIR}/CMakeLists.txt | sed 's/[^0-9]*//g')
    AKIRA_VERSION="${AKIRA_MAJOR}.${AKIRA_MINOR}.${AKIRA_REVISION}"

    AKIRA_COMMIT=$(cd ${BUILD_DIR} && git rev-parse --short HEAD 2>/dev/null || echo "unknown")

    echo "Akira ${AKIRA_VERSION}" > ${BUILD_DIR}/resources/build_info.txt
    echo "Commit: ${AKIRA_COMMIT}" >> ${BUILD_DIR}/resources/build_info.txt
    echo "Build: $(date -u '+%Y-%m-%d %H:%M:%S UTC')" >> ${BUILD_DIR}/resources/build_info.txt
    echo "" >> ${BUILD_DIR}/resources/build_info.txt
    echo "=== Libraries ===" >> ${BUILD_DIR}/resources/build_info.txt
    echo "chiaki-ng ${CHIAKI_VERSION}" >> ${BUILD_DIR}/resources/build_info.txt
    echo "ffmpeg ${FFMPEG_VERSION} (nvtegra)" >> ${BUILD_DIR}/resources/build_info.txt
    echo "curl ${CURL_VERSION} (libnx TLS)" >> ${BUILD_DIR}/resources/build_info.txt
    echo "" >> ${BUILD_DIR}/resources/build_info.txt
    echo "=== DevkitPro Packages ===" >> ${BUILD_DIR}/resources/build_info.txt
    dkp-pacman -Q >> ${BUILD_DIR}/resources/build_info.txt

    rm -rf build

    CMAKE_EXTRA_ARGS=""
    if [ "$MUTE_CHIAKI" = "true" ]; then
        echo "Chiaki logs will be muted"
        CMAKE_EXTRA_ARGS="-DMUTE_CHIAKI_LOGS=ON"
    fi

    cmake -B build \
        -DPLATFORM_SWITCH=ON \
        -DCMAKE_BUILD_TYPE=Debug \
        $CMAKE_EXTRA_ARGS

    make -C build akira.nro -j$(nproc)

    echo "=== Build complete ==="
    ls -la build/*.nro 2>/dev/null || echo "NRO not found - check build logs"
}

echo "Starting Akira build..."
echo "DEVKITPRO: ${DEVKITPRO}"
echo "PORTLIBS_PREFIX: ${PORTLIBS_PREFIX}"

build_akira

echo "Build completed successfully!"
