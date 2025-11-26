#!/bin/bash

# Quick test script for Flameshot on macOS
# Usage: ./test_local.sh [option]

set -e

BUILD_DIR="build_local"
APP_PATH="$BUILD_DIR/src/flameshot.app/Contents/MacOS/flameshot"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo -e "${BLUE}=== Flameshot Quick Test ===${NC}"

# Check if app exists
if [ ! -f "$APP_PATH" ]; then
    echo -e "${RED}Error: Flameshot not built yet!${NC}"
    echo "Run ./build_local.sh first"
    exit 1
fi

case "${1:-run}" in
    "run")
        echo -e "${GREEN}Starting Flameshot...${NC}"
        "$APP_PATH" &
        echo -e "${GREEN}Flameshot is running with PID: $!${NC}"
        ;;

    "gui")
        echo -e "${GREEN}Starting Flameshot GUI capture...${NC}"
        "$APP_PATH" gui &
        ;;

    "screen")
        echo -e "${GREEN}Taking fullscreen capture...${NC}"
        "$APP_PATH" screen -p ~/Desktop/
        echo "Screenshot saved to ~/Desktop/"
        ;;

    "config")
        echo -e "${GREEN}Opening Flameshot config...${NC}"
        "$APP_PATH" config
        ;;

    "version")
        echo -e "${GREEN}Flameshot version:${NC}"
        "$APP_PATH" --version
        ;;

    "kill")
        echo -e "${YELLOW}Stopping all Flameshot processes...${NC}"
        pkill -f flameshot || echo "No Flameshot process found"
        ;;

    "help")
        echo "Usage: ./test_local.sh [option]"
        echo ""
        echo "Options:"
        echo "  run     - Start Flameshot in background (default)"
        echo "  gui     - Start GUI capture mode"
        echo "  screen  - Take fullscreen capture"
        echo "  config  - Open configuration dialog"
        echo "  version - Show version info"
        echo "  kill    - Kill all Flameshot processes"
        echo "  help    - Show this help"
        ;;

    *)
        echo -e "${RED}Unknown option: $1${NC}"
        echo "Run ./test_local.sh help for options"
        exit 1
        ;;
esac