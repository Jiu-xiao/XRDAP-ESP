# Build the firmware. Usage: scripts\build.ps1 -LibxrRoot D:\src\libxr-c [-IdfPath C:\esp\esp-idf-v5.5.2] [-Build <dir>] [build|flash|...]
# Needs ESP-IDF v5.5.x with its tools installed (install.ps1 done once). Extra arguments go to idf.py.
param(
  [Parameter(Mandatory = $true)][string]$LibxrRoot,
  [string]$IdfPath = $(if ($env:IDF_PATH) { $env:IDF_PATH } else { "$HOME\esp\esp-idf-v5.5.2" }),
  [string]$BuildDir = "$PSScriptRoot\..\build",
  [Parameter(ValueFromRemainingArguments = $true)][string[]]$IdfArgs
)
$ErrorActionPreference = 'Stop'
$env:XRDAP_LIBXR_ROOT = (Resolve-Path $LibxrRoot).Path
. "$IdfPath\export.ps1" | Out-Null
$app = (Resolve-Path "$PSScriptRoot\..\firmware").Path
# Wi-Fi + USB, SWD and the DAP Step() thread on core 1 (all defaults); the generated sdkconfig stays in the build dir.
idf.py -C $app -B $BuildDir -D SDKCONFIG="$BuildDir\sdkconfig" -D CMAKE_CXX_FLAGS="-fira-region=one" @IdfArgs
