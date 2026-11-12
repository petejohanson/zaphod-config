# Building and flashing on a Mac

You do this setup once. Afterwards a firmware update is one command.

## 1. Install the tools

1. **Git.** Open Terminal and run `git --version`. If it isn't installed, macOS
   offers to install the Command Line Tools; accept.
2. **Docker Desktop.** Download it from <https://www.docker.com/products/docker-desktop/>,
   picking **Apple Silicon** or **Intel** to match your Mac (Apple menu → About
   This Mac). Open it once and leave it running. The whale icon in the menu bar
   shows it's up.

## 2. Get the repo

```sh
cd ~
git clone https://github.com/cemicolin/zaphod-config.git
cd zaphod-config
```

To use a branch that hasn't been merged into `main` yet:

```sh
git checkout <branch-name>
```

To pick up later changes made on GitHub:

```sh
git pull
```

## 3. Build

```sh
./build-local.sh
```

- **First run:** downloads the build image and ZMK, roughly 2–3 GB, and takes
  10–20 minutes.
- **Later runs:** take a minute or two.
- **Output:** `firmware/zaphod.uf2`.

On Apple Silicon, Docker may warn that the image's platform (`linux/amd64`)
doesn't match the host. That's expected: it runs through emulation and is just
slower.

## 4. Flash

1. Plug the Zaphod in with USB.
2. Run:

   ```sh
   ./build-local.sh --flash
   ```

3. When the script asks, **double-tap the reset button**. The keyboard appears
   as a USB drive, the script copies the firmware onto it, and the keyboard
   restarts.

To flash by hand instead, double-tap reset and drag `firmware/zaphod.uf2` onto
the drive that appears in Finder.

macOS may say the disk was "not ejected properly" afterwards. That's normal:
the keyboard restarts as soon as the copy finishes.

## 5. Change the keymap without rebuilding (ZMK Studio)

1. Open <https://zmk.studio> in **Chrome** or **Edge**. Safari doesn't support
   the USB connection Studio uses.
2. Connect over USB. Changes save to the keyboard straight away.

To make a Studio change permanent in the repo, copy it into
`config/zaphod.keymap`.

## Editing settings

| To change | Edit |
| --- | --- |
| Keymap | `config/zaphod.keymap` |
| Bluetooth, display and Studio settings | `config/zaphod.conf` |

Then rebuild and flash. To save your edits back to GitHub:

```sh
git add -A
git commit -m "Describe the change"
git push
```

## Troubleshooting

- **`Cannot connect to the Docker daemon`:** Docker Desktop isn't running.
  Open it and wait for the whale icon to settle.
- **`permission denied: ./build-local.sh`:** run `chmod +x build-local.sh`
  once.
- **Build fails after changing the ZMK version:** reset the cached workspace
  with `docker volume rm zaphod-zmk-workspace`, then build again.
- **`No UF2 bootloader drive found`:** the reset double-tap needs to be quick.
  Try again, or flash by hand as described above.
