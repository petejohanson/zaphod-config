# Zaphod ZMK config

Firmware config for the Zaphod keyboard.

| What | Where |
| --- | --- |
| Keymap | `config/zaphod.keymap` (or edit live at <https://zmk.studio>) |
| Settings (Bluetooth, display widgets, Studio) | `config/zaphod.conf` |
| ZMK version | `config/west.yml` (`revision:`) |
| Board definition, ZMK v0.3 | `boards/arm/zaphod/` |
| Board definition, newer ZMK (Zephyr 4.x) | `boards/petejohanson/zaphod/` |

## Building

- **Locally:** step-by-step setup guides for
  [macOS](docs/setup-mac.md) and [Windows](docs/setup-windows.md).
  Run `./build-local.sh` (Mac/Linux) or `.\build-local.ps1` (Windows) to get
  `firmware/zaphod.uf2`. Add `--flash` / `-Flash` to copy it to the keyboard
  after you double-tap reset. On a Mac this uses Apple container, OrbStack or
  Docker Desktop; on Windows, Docker Desktop.
- **GitHub Actions:** every push builds a `.uf2` you can download from the
  Actions tab.

## Upgrading to a newer ZMK release

The board exists in two formats because ZMK v0.3 uses Zephyr 3.5 and newer
ZMK uses Zephyr 4.x, which changed how boards are defined. Both folders can't
exist together on newer ZMK, so upgrading is:

1. In `config/west.yml`, set `revision:` to the new release tag (e.g. `v0.4`).
2. In `build.yaml`, change `board: zaphod` to `board: zaphod//zmk`.
3. Delete `boards/arm/zaphod/`.
4. Run `./build-local.sh`, flash, and use it for a day. To go back, revert
   those three changes.

Your keymap, settings and anything saved with ZMK Studio carry over.

The newer-ZMK board has been built against ZMK `main` (Zephyr 4.1, LVGL 9),
but not yet tested on the hardware.

Keep display code changes in sync: the bongo cat and status screen code exists
in both board folders, ported to each version's graphics library (LVGL 8 vs 9).
