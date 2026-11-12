# Building and flashing on Windows

You do this setup once. Afterwards a firmware update is one command.

## 1. Install the tools

1. **Git.** Install it from <https://git-scm.com/download/win>. The default
   options are fine.
2. **Docker Desktop.** Install it from
   <https://www.docker.com/products/docker-desktop/>. Accept the WSL 2 option
   if it asks, and restart if it asks. Open Docker Desktop once and leave it
   running; the whale icon in the system tray shows it's up.

## 2. Get the repo

Open **PowerShell** from the Start menu and run:

```powershell
cd $HOME
git clone https://github.com/cemicolin/zaphod-config.git
cd zaphod-config
```

To use a branch that hasn't been merged into `main` yet:

```powershell
git checkout <branch-name>
```

To pick up later changes made on GitHub:

```powershell
git pull
```

## 3. Allow the build script to run (once)

Windows blocks PowerShell scripts by default. Allow local scripts for your
user account:

```powershell
Set-ExecutionPolicy -Scope CurrentUser RemoteSigned
```

Answer `Y` if prompted.

## 4. Build

```powershell
.\build-local.ps1
```

- **First run:** downloads the build image and ZMK, roughly 2–3 GB, and takes
  10–20 minutes.
- **Later runs:** take a minute or two.
- **Output:** `firmware\zaphod.uf2`.

Docker Desktop may ask for permission to share the folder the first time;
allow it.

## 5. Flash

1. Plug the Zaphod in with USB.
2. Run:

   ```powershell
   .\build-local.ps1 -Flash
   ```

3. When the script asks, **double-tap the reset button**. The keyboard appears
   as a USB drive, the script copies the firmware onto it, and the keyboard
   restarts.

To flash by hand instead, double-tap reset and drag `firmware\zaphod.uf2` onto
the drive that appears in File Explorer.

Windows may show an error that the copy didn't finish or the drive
disappeared. That's normal: the keyboard restarts as soon as the copy
finishes.

## 6. Change the keymap without rebuilding (ZMK Studio)

1. Open <https://zmk.studio> in **Chrome** or **Edge**.
2. Connect over USB. Changes save to the keyboard straight away.

To make a Studio change permanent in the repo, copy it into
`config\zaphod.keymap`.

## Editing settings

| To change | Edit |
| --- | --- |
| Keymap | `config\zaphod.keymap` |
| Bluetooth, display and Studio settings | `config\zaphod.conf` |

Then rebuild and flash. To save your edits back to GitHub:

```powershell
git add -A
git commit -m "Describe the change"
git push
```

## Troubleshooting

- **`error during connect` or `docker: command not found`:** Docker Desktop
  isn't running. Start it and wait for the whale icon to settle.
- **`running scripts is disabled on this system`:** do step 3.
- **Build fails after changing the ZMK version:** reset the cached workspace
  with `docker volume rm zaphod-zmk-workspace`, then build again.
- **`No UF2 bootloader drive found`:** the reset double-tap needs to be quick.
  Try again, or flash by hand as described above.
- **Prefer Git Bash or WSL?** `./build-local.sh` works there too.
  `--flash` won't find the drive from Git Bash or WSL, so flash by hand.
