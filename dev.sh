#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
NRO_FILE="${BUILD_DIR}/akira.nro"

# Colors
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

print_status() { echo -e "${GREEN}[*]${NC} $1"; }
print_warning() { echo -e "${YELLOW}[!]${NC} $1"; }
print_error() { echo -e "${RED}[x]${NC} $1"; }

# Get Switch IP from argument or environment
SWITCH_IP="${1:-$SWITCH_IP}"

show_usage() {
    echo "Usage: $0 [SWITCH_IP] [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  --rebuild      Force full rebuild (includes --clean-libs)"
    echo "  --build-only   Build without deploying"
    echo "  --shell        Open shell in build container"
    echo "  --mute-chiaki  Mute chiaki library logs"
    echo "  --clean-libs   Clean library build artifacts (chiaki-ng, curl-libnx)"
    echo ""
    echo "Environment:"
    echo "  SWITCH_IP     IP address of Nintendo Switch"
    echo ""
    echo "On your Switch:"
    echo "  1. Open Homebrew Menu"
    echo "  2. Press Y for NetLoader mode"
    echo "  3. Note the IP address shown"
}

clean_lib_artifacts() {
    print_status "Cleaning library build artifacts..."
    # Clean chiaki-ng build artifacts
    git -C "${SCRIPT_DIR}/library/chiaki-ng" clean -fdx 2>/dev/null || true
    # Clean curl-libnx build artifacts
    git -C "${SCRIPT_DIR}/library/curl-libnx" clean -fdx 2>/dev/null || true
    print_status "Library artifacts cleaned"
}

# Parse arguments
BUILD_ONLY=false
FORCE_REBUILD=false
OPEN_SHELL=false
MUTE_CHIAKI=false
CLEAN_LIBS=false

for arg in "$@"; do
    case $arg in
        --rebuild)
            FORCE_REBUILD=true
            CLEAN_LIBS=true
            shift
            ;;
        --build-only)
            BUILD_ONLY=true
            shift
            ;;
        --shell)
            OPEN_SHELL=true
            shift
            ;;
        --mute-chiaki)
            MUTE_CHIAKI=true
            shift
            ;;
        --clean-libs)
            CLEAN_LIBS=true
            shift
            ;;
        --help|-h)
            show_usage
            exit 0
            ;;
    esac
done

# Clean library artifacts if requested
if [ "$CLEAN_LIBS" = true ]; then
    clean_lib_artifacts
fi

# Docker image name
DOCKER_IMAGE="akira-builder"

# Initialize submodules if needed
if [ ! -f "${SCRIPT_DIR}/library/borealis/README.md" ]; then
    print_status "Initializing submodules..."
    git -C "${SCRIPT_DIR}" submodule update --init --recursive
fi

# Build Docker image if it doesn't exist or --rebuild is specified
IMAGE_EXISTS=$(docker image inspect "$DOCKER_IMAGE" &>/dev/null && echo "yes" || echo "no")

if [ "$FORCE_REBUILD" = "true" ] || [ "$IMAGE_EXISTS" = "no" ]; then
    print_status "Building Docker image..."
    docker build -t "$DOCKER_IMAGE" "${SCRIPT_DIR}"
    if [ $? -ne 0 ]; then
        print_error "Docker build failed"
        exit 1
    fi
fi

# Open shell mode
if [ "$OPEN_SHELL" = true ]; then
    print_status "Opening shell in build container..."
    docker run --rm -it \
        -v "${SCRIPT_DIR}:/build" \
        -w /build \
        "$DOCKER_IMAGE" \
        bash
    exit 0
fi

# Build
print_status "Building..."

ORIGINAL_REVISION=$(grep 'set(VERSION_REVISION' "${SCRIPT_DIR}/CMakeLists.txt" | sed 's/.*"\(.*\)".*/\1/')
DEV_TIMESTAMP=$(date +%d%m%y-%H%M%S)
DEV_REVISION="${ORIGINAL_REVISION}-dev-${DEV_TIMESTAMP}"

cleanup_version() {
    sed -i "s/set(VERSION_REVISION \".*\")/set(VERSION_REVISION \"${ORIGINAL_REVISION}\")/" "${SCRIPT_DIR}/CMakeLists.txt"
}
trap cleanup_version EXIT

print_status "Setting dev version: ${DEV_REVISION}"
sed -i "s/set(VERSION_REVISION \"${ORIGINAL_REVISION}\")/set(VERSION_REVISION \"${DEV_REVISION}\")/" "${SCRIPT_DIR}/CMakeLists.txt"

docker run --rm \
    -v "${SCRIPT_DIR}:/build" \
    -w /build \
    -e "MUTE_CHIAKI=${MUTE_CHIAKI}" \
    "$DOCKER_IMAGE" \
    bash -c "
        set -e
        git config --global --add safe.directory /build
        git config --global --add safe.directory /build/library/borealis
        git config --global --add safe.directory /build/library/chiaki-ng
        git config --global --add safe.directory /build/library/curl-libnx
        chmod +x /build/scripts/build-docker.sh
        /build/scripts/build-docker.sh
    "

if [ ! -f "$NRO_FILE" ]; then
    print_error "Build failed - NRO not found at $NRO_FILE"
    exit 1
fi

print_status "Build successful: $NRO_FILE"

# Deploy if not build-only and IP is provided
if [ "$BUILD_ONLY" = true ]; then
    print_status "Build-only mode - skipping deployment"
    exit 0
fi

if [ -z "$SWITCH_IP" ]; then
    print_warning "No SWITCH_IP provided - skipping deployment"
    echo ""
    echo "To deploy, run: $0 <SWITCH_IP>"
    echo "   or: SWITCH_IP=192.168.x.x $0"
    exit 0
fi

# Kill any lingering nxlink processes
pkill -f nxlink 2>/dev/null || true

# Setup logging
LOGS_DIR="${SCRIPT_DIR}/logs"
mkdir -p "$LOGS_DIR"
LOG_FILE="${LOGS_DIR}/$(date +%d%m%y%H%M%S).log"

print_status "Deploying to Switch at ${SWITCH_IP}..."
print_status "Logging to: ${LOG_FILE}"
print_status "Press Ctrl+C to stop receiving logs"

docker run --rm -it --init \
    --network host \
    -v "${SCRIPT_DIR}:/build" \
    -w /build \
    "$DOCKER_IMAGE" \
    nxlink -s -a "$SWITCH_IP" /build/build/akira.nro 2>&1 | tee "$LOG_FILE"

print_status "Done"
