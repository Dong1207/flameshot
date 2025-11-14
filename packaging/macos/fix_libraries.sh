#!/bin/bash

# Script to fix library dependencies for Flameshot.app

APP_PATH="${1:-build/src/flameshot.app}"

if [ ! -d "$APP_PATH" ]; then
    echo "Error: App not found at $APP_PATH"
    exit 1
fi

echo "Fixing libraries for: $APP_PATH"

# Create Frameworks directory if it doesn't exist
FRAMEWORKS_DIR="$APP_PATH/Contents/Frameworks"
mkdir -p "$FRAMEWORKS_DIR"

# Copy brotli libraries
echo "Copying brotli libraries..."
for lib in libbrotlicommon.1.dylib libbrotlidec.1.dylib libbrotlienc.1.dylib; do
    if [ -f "/opt/homebrew/opt/brotli/lib/$lib" ]; then
        cp "/opt/homebrew/opt/brotli/lib/$lib" "$FRAMEWORKS_DIR/"
        echo "  Copied $lib"
    fi
done

# Fix library paths using install_name_tool
echo "Fixing library paths..."
cd "$FRAMEWORKS_DIR"

# Fix brotlidec dependency on brotlicommon
if [ -f "libbrotlidec.1.dylib" ]; then
    install_name_tool -change \
        "@rpath/libbrotlicommon.1.dylib" \
        "@executable_path/../Frameworks/libbrotlicommon.1.dylib" \
        "libbrotlidec.1.dylib"
fi

# Fix brotlienc dependency on brotlicommon
if [ -f "libbrotlienc.1.dylib" ]; then
    install_name_tool -change \
        "@rpath/libbrotlicommon.1.dylib" \
        "@executable_path/../Frameworks/libbrotlicommon.1.dylib" \
        "libbrotlienc.1.dylib"
fi

# Check all Qt frameworks for brotli dependencies
for framework in Qt*.framework; do
    if [ -d "$framework" ]; then
        BINARY="$framework/Versions/A/$(basename $framework .framework)"
        if [ -f "$BINARY" ]; then
            # Check if this framework uses brotli
            if otool -L "$BINARY" 2>/dev/null | grep -q "brotli"; then
                echo "  Fixing $framework..."
                # Fix references to brotli libraries
                install_name_tool -change \
                    "@rpath/libbrotlidec.1.dylib" \
                    "@executable_path/../Frameworks/libbrotlidec.1.dylib" \
                    "$BINARY" 2>/dev/null || true
                install_name_tool -change \
                    "@rpath/libbrotlicommon.1.dylib" \
                    "@executable_path/../Frameworks/libbrotlicommon.1.dylib" \
                    "$BINARY" 2>/dev/null || true
            fi
        fi
    fi
done

echo "Library fix complete!"

# Verify the main executable
echo ""
echo "Verifying main executable dependencies..."
cd - > /dev/null
otool -L "$APP_PATH/Contents/MacOS/flameshot" | grep -E "(brotli|@rpath)" || echo "No @rpath or brotli dependencies in main executable"