# Building and flashing on a Mac

You do this setup once. Afterwards a firmware update is one command.

## 1. Install the tools

1. **Git.** Open Terminal and run `git --version`. If it isn't installed, macOS
   offers to install the Command Line Tools; accept.
2. **A container tool** to run the ZMK build environment. Pick one:
   - **Apple container** (recommended on Apple Silicon with macOS 26 or later):
     free, from Apple, no background app. See
     [Apple container](#apple-container) below.
   - **OrbStack** (any Apple Silicon or Intel Mac): use it if Apple container
     doesn't work for you. See [OrbStack](#orbstack-fallback) below.
   - **Docker Desktop** also works if you already have it.

### Apple container

1. Check you're on Apple Silicon with macOS 26 (Tahoe) or later: Apple menu →
   About This Mac.
2. Install it with [Homebrew](https://brew.sh):

   ```sh
   brew install container
   ```

   Or download the signed installer (`container-<version>-installer-signed.pkg`)
   from <https://github.com/apple/container/releases> and open it.
3. Start it once:

   ```sh
   container system start
   ```

   When it asks to install the recommended Linux kernel, answer `y`.
   `./build-local.sh` also starts it if needed, but it stops when you log out.
   To keep it running across restarts: `brew services start container`
   (then run `container system kernel set --recommended` once).

`./build-local.sh` uses Docker when Docker or OrbStack is running and Apple
container otherwise. To force Apple container:

```sh
CONTAINER=container ./build-local.sh
```

The ZMK build image has a native Apple Silicon (arm64) version, so nothing
runs under emulation and you don't need Rosetta. Nothing ZMK-related is
installed on the Mac itself: the toolchain lives in the container image.

### OrbStack (fallback)

1. Install it with `brew install --cask orbstack`, or download it from
   <https://orbstack.dev>.
2. Open OrbStack once and finish its setup. It provides the `docker` command,
   so the build script uses it automatically.

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

ZMK and Zephyr are downloaded into the `.zmk-workspace` folder in the repo
(ignored by Git) and reused by later builds.

The build image runs natively on both Apple Silicon and Intel Macs.

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

- **`Cannot connect to the Docker daemon`:** Docker Desktop or OrbStack isn't
  running. Open it and try again, or use Apple container with
  `CONTAINER=container ./build-local.sh`.
- **Apple container errors mentioning the API server, XPC or the kernel:** run
  `container system start`, accept the kernel install if asked, then build
  again.
- **Apple container fails and you can't fix it:** install OrbStack (see above),
  open it, and run `./build-local.sh` again.
- **`permission denied: ./build-local.sh`:** run `chmod +x build-local.sh`
  once.
- **Build fails after changing the ZMK version:** reset the cached workspace
  with `rm -rf .zmk-workspace`, then build again.
- **Low on disk space:** the build uses about 3.5 GB (the build image plus
  `.zmk-workspace`). With Apple container, `container image prune -a` removes
  the image (it downloads again on the next build); `rm -rf .zmk-workspace`
  removes the ZMK sources. To remove Apple container entirely:
  `container system stop`, then `brew uninstall container`.
- **`No UF2 bootloader drive found`:** the reset double-tap needs to be quick.
  Try again, or flash by hand as described above.
