#!/bin/bash

# Quick local build script for Flameshot on macOS
# Usage: ./build_local.sh [clean|debug|release]

set -e

# Configuration
BUILD_DIR="build_local"
BUILD_TYPE="${1:-Debug}"  # Default to Debug for faster builds

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}=== Flameshot Quick Local Build ===${NC}"

# Handle clean option
if [ "$1" == "clean" ]; then
    echo -e "${YELLOW}Cleaning build directory...${NC}"
    rm -rf "$BUILD_DIR"
    echo -e "${GREEN}Clean complete!${NC}"
    exit 0
fi

# Set build type
if [ "$1" == "release" ]; then
    BUILD_TYPE="Release"
    echo -e "${YELLOW}Building in Release mode (optimized but slower build)${NC}"
elif [ "$1" == "debug" ]; then
    BUILD_TYPE="Debug"
    echo -e "${YELLOW}Building in Debug mode (faster build, with debug symbols)${NC}"
else
    echo -e "${YELLOW}Building in Debug mode (default)${NC}"
fi

# Create build directory
mkdir -p "$BUILD_DIR"

# Configure
echo -e "${GREEN}[1/3] Configuring...${NC}"
cmake -GNinja \
    -S . \
    -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DQt6_DIR=$(brew --prefix qt@6)/lib/cmake/Qt6 \
    -DUSE_MONOCHROME_ICON=True \
    -DENABLE_IMGUR=ON \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# Build
echo -e "${GREEN}[2/3] Building...${NC}"
cmake --build "$BUILD_DIR" --parallel $(sysctl -n hw.ncpu)

# Fix libraries and sign
echo -e "${GREEN}[3/3] Fixing libraries and signing...${NC}"
if [ -f "./packaging/macos/fix_libraries.sh" ]; then
    chmod +x ./packaging/macos/fix_libraries.sh
    ./packaging/macos/fix_libraries.sh "$BUILD_DIR/src/flameshot.app"
fi
codesign --force --deep --sign - "$BUILD_DIR/src/flameshot.app"

echo -e "${GREEN}=== Build Complete! ===${NC}"
echo -e "${GREEN}App location: $BUILD_DIR/src/flameshot.app${NC}"
echo ""
echo "To run the app:"
echo "  ./$BUILD_DIR/src/flameshot.app/Contents/MacOS/flameshot"
echo ""
echo "To open in Finder:"
echo "  open ./$BUILD_DIR/src/flameshot.app"
echo ""
echo "Other options:"
echo "  ./build_local.sh clean    - Clean build directory"
echo "  ./build_local.sh debug    - Build with debug symbols (default)"
echo "  ./build_local.sh release  - Build optimized release version"