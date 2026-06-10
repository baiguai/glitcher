#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "==> Installing system packages..."
sudo apt-get update -qq
sudo apt-get install -y -qq \
    build-essential \
    cmake \
    pkg-config \
    git \
    libglfw3-dev \
    libgl-dev

echo "==> Fetching imgui (docking branch) into deps/..."
if [ -d "$SCRIPT_DIR/deps/imgui/.git" ]; then
    echo "    Already cloned, pulling latest..."
    git -C "$SCRIPT_DIR/deps/imgui" pull --ff-only
else
    git clone --branch docking --depth 1 --single-branch \
        https://github.com/ocornut/imgui.git "$SCRIPT_DIR/deps/imgui"
fi

echo "==> Done. Now build with:"
echo "    cmake -B build && cmake --build build"
