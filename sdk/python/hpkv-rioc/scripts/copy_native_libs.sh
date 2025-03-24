#!/bin/bash
set -e

# Get the absolute path of the workspace root
WORKSPACE_ROOT=""
RIOC_BUILD_DIR="$WORKSPACE_ROOT/api/rioc/build"
SDK_RUNTIMES_DIR="$WORKSPACE_ROOT/api/sdk/python/hpkv-rioc/src/hpkv_rioc/runtimes"

# Create runtimes directory structure
mkdir -p "$SDK_RUNTIMES_DIR/linux-x64/native"
mkdir -p "$SDK_RUNTIMES_DIR/linux-arm64/native"
mkdir -p "$SDK_RUNTIMES_DIR/win-x64/native"
mkdir -p "$SDK_RUNTIMES_DIR/osx-x64/native"
mkdir -p "$SDK_RUNTIMES_DIR/osx-arm64/native"

# Copy Linux libraries if they exist
if [ -f "$RIOC_BUILD_DIR/librioc.so" ]; then
    cp "$RIOC_BUILD_DIR/librioc.so" "$SDK_RUNTIMES_DIR/linux-x64/native/"
    cp "$RIOC_BUILD_DIR/librioc.so" "$SDK_RUNTIMES_DIR/linux-arm64/native/"
    echo "Copied Linux libraries"
fi

# Copy Windows libraries if they exist
if [ -f "$RIOC_BUILD_DIR/rioc.dll" ]; then
    cp "$RIOC_BUILD_DIR/rioc.dll" "$SDK_RUNTIMES_DIR/win-x64/native/"
    echo "Copied Windows libraries"
fi

# Copy macOS libraries if they exist
if [ -f "$RIOC_BUILD_DIR/librioc.dylib" ]; then
    cp "$RIOC_BUILD_DIR/librioc.dylib" "$SDK_RUNTIMES_DIR/osx-x64/native/"
    cp "$RIOC_BUILD_DIR/librioc.dylib" "$SDK_RUNTIMES_DIR/osx-arm64/native/"
    echo "Copied macOS libraries"
fi

echo "Native libraries copied successfully!" 