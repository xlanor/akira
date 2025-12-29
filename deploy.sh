#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NRO_FILE="${SCRIPT_DIR}/build/akira.nro"
DOCKER_IMAGE="akira-builder"

# Colors
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

# Get Switch IP from argument or environment
SWITCH_IP="${1:-$SWITCH_IP}"
NO_LOG=false

for arg in "$@"; do
    case $arg in
        --no-log)
            NO_LOG=true
            ;;
    esac
done

if [ -z "$SWITCH_IP" ]; then
    echo -e "${RED}[x]${NC} No SWITCH_IP provided"
    echo "Usage: $0 <SWITCH_IP> [--no-log]"
    echo "   or: SWITCH_IP=192.168.x.x $0"
    echo ""
    echo "Options:"
    echo "  --no-log   Deploy without waiting for logs"
    exit 1
fi

if [ ! -f "$NRO_FILE" ]; then
    echo -e "${RED}[x]${NC} NRO not found at $NRO_FILE"
    echo "Run ./dev.sh first to build"
    exit 1
fi

# Kill anything using port 28280 (nxlink server port)
echo -e "${GREEN}[*]${NC} Clearing port 28280..."
sudo fuser -k 28280/tcp 2>/dev/null || true
sleep 0.5

echo -e "${GREEN}[*]${NC} Deploying to Switch at ${SWITCH_IP}..."

if [ "$NO_LOG" = true ]; then
    # Just send the file, don't wait for logs
    docker run --rm \
        --network host \
        -v "${SCRIPT_DIR}:/build" \
        -w /build \
        "$DOCKER_IMAGE" \
        nxlink -a "$SWITCH_IP" /build/build/akira.nro
    echo -e "${GREEN}[*]${NC} Deployed!"
else
    # Setup logging
    LOGS_DIR="${SCRIPT_DIR}/logs"
    mkdir -p "$LOGS_DIR"
    LOG_FILE="${LOGS_DIR}/$(date +%d%m%y%H%M%S).log"

    echo -e "${GREEN}[*]${NC} Logging to: ${LOG_FILE}"
    echo -e "${GREEN}[*]${NC} Press Ctrl+C to stop receiving logs"
    docker run --rm -it --init \
        --network host \
        -v "${SCRIPT_DIR}:/build" \
        -w /build \
        "$DOCKER_IMAGE" \
        nxlink -s -a "$SWITCH_IP" /build/build/akira.nro 2>&1 | tee "$LOG_FILE"
fi

echo -e "${GREEN}[*]${NC} Done"
