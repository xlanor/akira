#!/bin/bash
set -e
source /opt/devkitpro/switchvars.sh

BUILD_DIR="/build"

build_curl() {
    echo "=== Building custom curl with libnx TLS + websockets ==="
    cd ${BUILD_DIR}/library/curl-libnx

    if [ ! -f configure ]; then
        echo "Running buildconf..."
        ./buildconf
    fi

    make distclean 2>/dev/null || true

    LDFLAGS="-specs=${DEVKITPRO}/libnx/switch.specs ${LDFLAGS}" \
    ./configure \
        --prefix=${PORTLIBS_PREFIX} \
        --host=aarch64-none-elf \
        --disable-shared \
        --enable-static \
        --disable-ipv6 \
        --disable-unix-sockets \
        --disable-socketpair \
        --disable-manual \
        --disable-ntlm-wb \
        --disable-threaded-resolver \
        --disable-ldap \
        --disable-ldaps \
        --without-libpsl \
        --without-libssh2 \
        --without-libssh \
        --without-nghttp2 \
        --without-libidn2 \
        --enable-websockets \
        --with-libnx \
        --with-default-ssl-backend=libnx

    sed -i 's/#define HAVE_SOCKETPAIR 1//' lib/curl_config.h

    make -C lib -j$(nproc)
    make DESTDIR="" -C lib install
    make DESTDIR="" -C include install
    make DESTDIR="" install-binSCRIPTS install-pkgconfigDATA

    echo "Custom curl installed to ${PORTLIBS_PREFIX}"

    if [ -f "${PORTLIBS_PREFIX}/lib/libcurl.a" ]; then
        echo "curl installed successfully"
    else
        echo "ERROR: curl installation failed!"
        exit 1
    fi
}

build_akira() {
    echo "=== Building Akira ==="
    cd ${BUILD_DIR}

    # Generate build info file with all installed packages
    echo "=== Generating build info ==="

    # Extract chiaki-ng version from CMakeLists.txt
    CHIAKI_VERSION=$(grep -E "^set\(CHIAKI_VERSION " ${BUILD_DIR}/library/chiaki-ng/CMakeLists.txt | sed 's/.*CHIAKI_VERSION \([0-9.]*\).*/\1/' || echo "unknown")
    if [ "$CHIAKI_VERSION" = "unknown" ]; then
        # Try alternate method - extract major.minor.patch
        CHIAKI_MAJOR=$(grep "CHIAKI_VERSION_MAJOR" ${BUILD_DIR}/library/chiaki-ng/CMakeLists.txt | head -1 | sed 's/[^0-9]*//g')
        CHIAKI_MINOR=$(grep "CHIAKI_VERSION_MINOR" ${BUILD_DIR}/library/chiaki-ng/CMakeLists.txt | head -1 | sed 's/[^0-9]*//g')
        CHIAKI_PATCH=$(grep "CHIAKI_VERSION_PATCH" ${BUILD_DIR}/library/chiaki-ng/CMakeLists.txt | head -1 | sed 's/[^0-9]*//g')
        CHIAKI_VERSION="${CHIAKI_MAJOR}.${CHIAKI_MINOR}.${CHIAKI_PATCH}"
    fi

    # Extract curl version from curlver.h
    CURL_VERSION=$(grep 'LIBCURL_VERSION "' ${BUILD_DIR}/library/curl-libnx/include/curl/curlver.h | sed 's/.*"\([^"]*\)".*/\1/' || echo "unknown")

    # Extract Akira version from CMakeLists.txt
    AKIRA_MAJOR=$(grep 'set(VERSION_MAJOR' ${BUILD_DIR}/CMakeLists.txt | sed 's/[^0-9]*//g')
    AKIRA_MINOR=$(grep 'set(VERSION_MINOR' ${BUILD_DIR}/CMakeLists.txt | sed 's/[^0-9]*//g')
    AKIRA_REVISION=$(grep 'set(VERSION_REVISION' ${BUILD_DIR}/CMakeLists.txt | sed 's/[^0-9]*//g')
    AKIRA_VERSION="${AKIRA_MAJOR}.${AKIRA_MINOR}.${AKIRA_REVISION}"

    echo "Akira ${AKIRA_VERSION}" > ${BUILD_DIR}/resources/build_info.txt
    echo "Build: $(date -u '+%Y-%m-%d %H:%M:%S UTC')" >> ${BUILD_DIR}/resources/build_info.txt
    echo "" >> ${BUILD_DIR}/resources/build_info.txt
    echo "=== Custom Libraries ===" >> ${BUILD_DIR}/resources/build_info.txt
    echo "chiaki-ng ${CHIAKI_VERSION}" >> ${BUILD_DIR}/resources/build_info.txt
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

build_curl
build_akira

echo "Build completed successfully!"
