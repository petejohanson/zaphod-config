#!/usr/bin/env bash
# Build Zaphod firmware locally with Docker (no GitHub Actions needed).
#
# Usage:   ./build-local.sh            # build, output in ./firmware/zaphod.uf2
#          ./build-local.sh --flash    # build, then copy to the keyboard's bootloader drive
#
# Requires Docker (or Podman: CONTAINER=podman ./build-local.sh).
# The first run downloads ZMK + Zephyr (~1-2 GB) into a Docker volume; later
# runs reuse it and take a minute or two.
set -euo pipefail

cd "$(dirname "$0")"
REPO="$PWD"
CONTAINER="${CONTAINER:-docker}"
IMAGE="${IMAGE:-zmkfirmware/zmk-build-arm:3.5}"   # matches ZMK v0.3 (config/west.yml)
VOLUME="zaphod-zmk-workspace"
SNIPPET="studio-rpc-usb-uart"

"$CONTAINER" volume create "$VOLUME" >/dev/null

"$CONTAINER" run --rm \
  -v "$VOLUME":/workspace \
  -v "$REPO":/zaphod-config:ro \
  -w /workspace \
  "$IMAGE" /bin/bash -euc "
    if [ ! -d .west ]; then
      mkdir -p config && cp /zaphod-config/config/west.yml config/
      west init -l config
    else
      cp /zaphod-config/config/west.yml config/
    fi
    west update --narrow -o=--depth=1
    west zephyr-export >/dev/null
    west build -s zmk/app -d build/zaphod -b zaphod -p auto -S $SNIPPET -- \
      -DZMK_CONFIG=/zaphod-config/config \
      -DBOARD_ROOT=/zaphod-config
    mkdir -p /workspace/out && cp build/zaphod/zephyr/zmk.uf2 /workspace/out/zaphod.uf2
  "

mkdir -p firmware
"$CONTAINER" run --rm -v "$VOLUME":/workspace -v "$REPO/firmware":/out "$IMAGE" \
  cp /workspace/out/zaphod.uf2 /out/zaphod.uf2
echo "Built: $REPO/firmware/zaphod.uf2"

if [ "${1:-}" = "--flash" ]; then
  echo "Double-tap the reset button on the Zaphod to enter the bootloader..."
  for _ in $(seq 1 60); do
    for d in /Volumes/* /media/"$USER"/* /run/media/"$USER"/*; do
      if [ -f "$d/INFO_UF2.TXT" ]; then
        cp firmware/zaphod.uf2 "$d/" && echo "Flashed to $d" && exit 0
      fi
    done 2>/dev/null
    sleep 1
  done
  echo "No UF2 bootloader drive found. Copy firmware/zaphod.uf2 to it manually." >&2
  exit 1
fi
