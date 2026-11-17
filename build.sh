#!/bin/bash

# Build the project

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
LINK_ROOT="${LINK_ROOT:-$SCRIPT_DIR/../link}"
LOOPER_BUILD_PATH="$SCRIPT_DIR/build"

cmake -S "$SCRIPT_DIR" -B "$LOOPER_BUILD_PATH" \
  -DUSE_ABLETON_LINK=ON \
  -DABLETON_LINK_ROOT="$LINK_ROOT" \
  && cmake --build "$LOOPER_BUILD_PATH" --target video_looper -j8 \
  && "$LOOPER_BUILD_PATH/video_looper.app/Contents/MacOS/video_looper" 