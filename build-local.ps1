# Build Zaphod firmware locally on Windows with Docker Desktop.
#
# Usage (from PowerShell, in the repo folder):
#   .\build-local.ps1            # build, output in .\firmware\zaphod.uf2
#   .\build-local.ps1 -Flash     # build, then copy to the keyboard's bootloader drive
#
# Same behaviour as build-local.sh; see docs/setup-windows.md.

param(
    [switch]$Flash,
    [string]$Board = "",
    [string]$Image = ""
)

$ErrorActionPreference = "Stop"

$Repo = $PSScriptRoot
Set-Location $Repo

$Volume = "zaphod-zmk-workspace"
$Snippet = "studio-rpc-usb-uart"

# Board target comes from build.yaml ("zaphod" on ZMK v0.3, "zaphod//zmk" on newer ZMK).
if (-not $Board) {
    $m = Select-String -Path "build.yaml" -Pattern '^\s*-\s*board:\s*(\S+)' | Select-Object -First 1
    $Board = $m.Matches[0].Groups[1].Value
}

# Pick the build image matching the ZMK revision in config/west.yml.
$m = Select-String -Path "config\west.yml" -Pattern '^\s*revision:\s*(\S+)' | Select-Object -First 1
$ZmkRev = $m.Matches[0].Groups[1].Value
if (-not $Image) {
    if ($ZmkRev -like "v0.3*") { $Image = "zmkfirmware/zmk-build-arm:3.5" }
    else { $Image = "zmkfirmware/zmk-build-arm:stable" }
}
Write-Host "Building $Board with ZMK $ZmkRev using $Image"

function Invoke-Docker {
    & docker @args
    if ($LASTEXITCODE -ne 0) { throw "docker $($args[0]) failed (exit code $LASTEXITCODE)" }
}

Invoke-Docker volume create $Volume | Out-Null

# Kept on one line so Windows line endings can't sneak into the Linux shell.
$BuildCmd = "set -eu; " +
    "if [ ! -d .west ]; then mkdir -p config && cp /zaphod-config/config/west.yml config/ && west init -l config; " +
    "else cp /zaphod-config/config/west.yml config/; fi; " +
    "west update --narrow -o=--depth=1; " +
    "west zephyr-export >/dev/null; " +
    "west build -s zmk/app -d build/zaphod -b $Board -p auto -S $Snippet -- -DZMK_CONFIG=/zaphod-config/config -DBOARD_ROOT=/zaphod-config; " +
    "mkdir -p /workspace/out && cp build/zaphod/zephyr/zmk.uf2 /workspace/out/zaphod.uf2"

Invoke-Docker run --rm `
    -v "${Volume}:/workspace" `
    -v "${Repo}:/zaphod-config:ro" `
    -w /workspace `
    $Image /bin/bash -c $BuildCmd

$OutDir = Join-Path $Repo "firmware"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
Invoke-Docker run --rm -v "${Volume}:/workspace" -v "${OutDir}:/out" $Image `
    cp /workspace/out/zaphod.uf2 /out/zaphod.uf2
$Uf2 = Join-Path $OutDir "zaphod.uf2"
Write-Host "Built: $Uf2"

if ($Flash) {
    Write-Host "Double-tap the reset button on the Zaphod to enter the bootloader..."
    for ($i = 0; $i -lt 60; $i++) {
        foreach ($drive in Get-PSDrive -PSProvider FileSystem) {
            $info = Join-Path $drive.Root "INFO_UF2.TXT"
            if (Test-Path $info) {
                Copy-Item $Uf2 $drive.Root
                Write-Host "Flashed to $($drive.Root)"
                exit 0
            }
        }
        Start-Sleep -Seconds 1
    }
    Write-Error "No UF2 bootloader drive found. Copy firmware\zaphod.uf2 to it manually."
    exit 1
}
