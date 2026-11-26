# Fetches the RT64 renderer into thirdparty/rt64.
#
# RT64 is ~380 MB including its shader-compiler (DXC) and bundled SDL2 for
# Windows, so it is not committed. Two ways to provide it:
#
#   powershell -ExecutionPolicy Bypass -File tools/fetch_rt64.ps1
#       Clones https://github.com/rt64/rt64 into thirdparty/rt64.
#
#   powershell -ExecutionPolicy Bypass -File tools/fetch_rt64.ps1 -From "D:\path\to\rt64"
#       Copies an existing checkout instead (no network).
#
# or configure with -DSCARS_RT64_DIR=<path> and skip this script entirely.

param(
    [string]$From = "",
    [string]$Destination = "",
    [string]$Repository = "https://github.com/rt64/rt64.git"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($Destination)) {
    $Destination = Join-Path $root "thirdparty\rt64"
}

if (Test-Path (Join-Path $Destination "CMakeLists.txt")) {
    Write-Host "RT64 already present at $Destination"
    exit 0
}

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Destination) | Out-Null

if (-not [string]::IsNullOrWhiteSpace($From)) {
    if (-not (Test-Path (Join-Path $From "CMakeLists.txt"))) {
        throw "$From is not an RT64 checkout (no CMakeLists.txt)"
    }
    Write-Host "Copying RT64 from $From"
    # /MIR keeps the contrib tree (DXC, SDL2, plume) intact; .git is skipped so
    # this project's git never treats the copy as a nested repository.
    robocopy $From $Destination /MIR /XD .git /NFL /NDL /NJH /NJS /NP /R:1 /W:1 | Out-Null
    if ($LASTEXITCODE -ge 8) {
        throw "robocopy failed with exit code $LASTEXITCODE"
    }
} else {
    Write-Host "Cloning $Repository into $Destination"
    git clone --recursive $Repository $Destination
    if ($LASTEXITCODE -ne 0) {
        throw "git clone failed with exit code $LASTEXITCODE"
    }
}

if (-not (Test-Path (Join-Path $Destination "CMakeLists.txt"))) {
    throw "RT64 was not fetched correctly into $Destination"
}
Write-Host "RT64 ready at $Destination"
