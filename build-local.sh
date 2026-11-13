#!/usr/bin/env bash
# Build Zaphod firmware locally in a container (no GitHub Actions needed).
#
# Usage:   ./build-local.sh            # build, output in ./firmware/zaphod.uf2
#          ./build-local.sh --flash    # build, then copy to the keyboard's bootloader drive
#
# Container runtime: uses Docker (Docker Desktop, OrbStack, Colima) if it's
# running, otherwise Apple's `container` tool. Pick one explicitly with
#   CONTAINER=container ./build-local.sh     # Apple container (macOS 26+, Apple Silicon)
#   CONTAINER=docker    ./build-local.sh     # Docker / OrbStack
#   CONTAINER=podman    ./build-local.sh
#
# Extra build options, e.g. USB logging for debugging:
#   EXTRA_CMAKE_ARGS="-DCONFIG_ZMK_USB_LOGGING=y" ./build-local.sh
#
# The first run downloads ZMK + Zephyr (~1-2 GB) into ./.zmk-workspace (or
# $ZMK_WORKSPACE); later runs reuse it and take a minute or two. Delete that
# folder to start from scratch.
set -euo pipefail

cd "$(dirname "$0")"
REPO="$PWD"
WORKSPACE="${ZMK_WORKSPACE:-$REPO/.zmk-workspace}"
SNIPPET="studio-rpc-usb-uart"
PROJECT_FILTER="-hal_.*,+hal_nordic,-acpica,-canopennode,-cmsis-dsp,-cmsis-nn,-fatfs,-libmetal,-liblc3,-mipi-sys-t,-nrf_hw_models,-open-amp,-percepio,-trusted-firmware-a,-uoscore-uedhoc"

if [ -z "${CONTAINER:-}" ]; then
  if command -v docker >/dev/null 2>&1 && docker info >/dev/null 2>&1; then
    CONTAINER=docker
  elif command -v container >/dev/null 2>&1; then
    CONTAINER=container
  elif command -v docker >/dev/null 2>&1; then
    CONTAINER=docker   # installed but not running; the error below says so
  else
    echo "No container runtime found. Install Apple container, OrbStack or Docker Desktop" >&2
    echo "(see docs/setup-mac.md)." >&2
    exit 1
  fi
fi

RUN_ARGS=()
if [ "$(basename "$CONTAINER")" = "container" ]; then
  # Apple container runs each container in its own VM; its services must be up.
  if ! container system status >/dev/null 2>&1; then
    echo "Starting Apple container services..."
    container system start --enable-kernel-install
  fi
  # The ZMK images ship native arm64 builds, so no emulation is needed.
  RUN_ARGS+=(--memory 4g --cpus "$(sysctl -n hw.perflevel0.physicalcpu 2>/dev/null || echo 4)")
fi

# Board target comes from build.yaml ("zaphod" on ZMK v0.3, "zaphod//zmk" on newer ZMK).
BOARD="${BOARD:-$(sed -n 's/^ *- board: *//p' build.yaml | head -1)}"

# Pick the build image matching the ZMK revision in config/west.yml.
ZMK_REV="$(sed -n '/name: zmk/,/import/s/^ *revision: *//p' config/west.yml)"
case "$ZMK_REV" in
  v0.3*) DEFAULT_IMAGE="zmkfirmware/zmk-build-arm:3.5" ;;
  *)     DEFAULT_IMAGE="zmkfirmware/zmk-build-arm:stable" ;;
esac
IMAGE="${IMAGE:-$DEFAULT_IMAGE}"
echo "Building $BOARD with ZMK $ZMK_REV using $IMAGE ($CONTAINER)"

mkdir -p "$WORKSPACE" firmware
rm -f "$WORKSPACE/zaphod.uf2"

# Incremental builds miss some changes (board Kconfig defaults, removed -D
# options), so build from scratch whenever the board, config or options change.
STAMP="$( { echo "$BOARD $IMAGE ${EXTRA_CMAKE_ARGS:-}"; find boards config build.yaml -type f | LC_ALL=C sort | xargs cat; } | shasum | cut -c1-40)"
PRISTINE=auto
if [ "$(cat "$WORKSPACE/.build-stamp" 2>/dev/null)" != "$STAMP" ]; then
  PRISTINE=always
fi

"$CONTAINER" run --rm ${RUN_ARGS[@]+"${RUN_ARGS[@]}"} \
  -v "$WORKSPACE":/workspace \
  -v "$REPO":/zaphod-config:ro \
  -w /workspace \
  "$IMAGE" /bin/bash -euc "
    git config --global --add safe.directory '*'
    # Clear git locks left behind if an earlier build was interrupted.
    find . -path '*/.git/*' -name '*.lock' -type f -delete 2>/dev/null || true
    if [ ! -d .west ]; then
      mkdir -p config && cp /zaphod-config/config/west.yml config/
      west init -l config
    else
      cp /zaphod-config/config/west.yml config/
    fi
    # Skip Zephyr modules the Zaphod (nRF52840) never uses: saves ~1.4 GB.
    west config manifest.project-filter -- '$PROJECT_FILTER'
    west update --narrow -o=--depth=1
    west zephyr-export >/dev/null
    west build -s zmk/app -d build/zaphod -b $BOARD -p $PRISTINE -S $SNIPPET -- \
      -DZMK_CONFIG=/zaphod-config/config \
      -DBOARD_ROOT=/zaphod-config ${EXTRA_CMAKE_ARGS:-}
    cp build/zaphod/zephyr/zmk.uf2 /workspace/zaphod.uf2
  "

cp "$WORKSPACE/zaphod.uf2" firmware/zaphod.uf2
echo "$STAMP" > "$WORKSPACE/.build-stamp"
echo "Built: $REPO/firmware/zaphod.uf2"

if [ "${1:-}" = "--flash" ]; then
  echo "Double-tap the reset button on the Zaphod to enter the bootloader..."
  for _ in $(seq 1 60); do
    for d in /Volumes/* /media/"$USER"/* /run/media/"$USER"/*; do
      if [ -f "$d/INFO_UF2.TXT" ]; then
        # Plain data copy (no macOS metadata): the keyboard restarts the moment
        # the firmware lands, so a later write can fail even though it worked.
        if cat firmware/zaphod.uf2 > "$d/zaphod.uf2" || [ ! -d "$d" ]; then
          echo "Flashed to $d" && exit 0
        fi
      fi
    done 2>/dev/null
    sleep 1
  done
  echo "No UF2 bootloader drive found. Copy firmware/zaphod.uf2 to it manually." >&2
  exit 1
fi
