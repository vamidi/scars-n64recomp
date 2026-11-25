<#
.SYNOPSIS
Generates the AOT corpus for S.C.A.R.S from a local ROM dump.

.DESCRIPTION
1. Ghidra headless: analyse the ROM, discover functions, export symbols TOML.
2. n64sym: name libultra functions by code signature.
3. merge_symbols.py: merge those names into the symbols TOML.
4. N64Recomp: translate every function into C under build/generated.

The ROM and everything derived from it stay local.

.PARAMETER Rom
Path to your own .z64 dump. Defaults to data/scars.z64.

.PARAMETER Ghidra
Ghidra install directory (contains support/analyzeHeadless.bat).

.PARAMETER Recompiler
N64Recomp executable. Defaults to the built tool under build/.

.PARAMETER ProjectDir
Ghidra project parent directory. Must not contain '+' (Ghidra rejects it),
so it defaults to the temp directory rather than this C++ path.
#>
[CmdletBinding()]
param(
    [string]$Rom,
    [string]$Ghidra = "C:\ghidra_12.1.2_PUBLIC",
    [string]$Recompiler,
    [string]$ProjectDir
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if (-not $Rom) { $Rom = Join-Path $root "data\scars.z64" }
if (-not $Recompiler) { $Recompiler = Join-Path $root "build\windows-x64-release\bin\N64Recomp.exe" }
if (-not $ProjectDir) { $ProjectDir = Join-Path $env:TEMP "scars-ghidra" }
$rom = (Resolve-Path $Rom).Path
$symbolsDir = Join-Path $root "build\symbols"
$generatedDir = Join-Path $root "build\generated"
$ghidraScripts = Join-Path $root "tools\ghidra"

New-Item -ItemType Directory -Force -Path $symbolsDir, $generatedDir, $ProjectDir | Out-Null

if (-not (Test-Path $Recompiler)) {
    throw "N64Recomp was not found at $Recompiler. Build it first: cmake --build --preset windows-x64-release"
}
$analyzeHeadless = Join-Path $Ghidra "support\analyzeHeadless.bat"
if (-not (Test-Path $analyzeHeadless)) {
    throw "analyzeHeadless.bat was not found under $Ghidra"
}

$symbolsToml = Join-Path $symbolsDir "scars.symbols.toml"
$n64symOut = Join-Path $symbolsDir "n64sym.txt"

Write-Host "[1/4] Ghidra analysis + function export"
Get-ChildItem $ProjectDir -Force -ErrorAction SilentlyContinue | Where-Object { $_.Name -like "Scars*" } | Remove-Item -Recurse -Force
& $analyzeHeadless $ProjectDir Scars `
    -import $rom `
    -loader BinaryLoader -loader-baseAddr 0x801ff400 `
    -processor "MIPS:BE:32:default" `
    -scriptPath $ghidraScripts `
    -preScript ScarsSeed.java `
    -postScript ScarsExportSymbols.java $symbolsToml 0x80200400 0x802B6050 0x1000 `
    -analysisTimeoutPerFile 1800 | Out-Host
if ($LASTEXITCODE -ne 0) { throw "Ghidra analysis failed ($LASTEXITCODE)" }

Write-Host "[2/4] n64sym libultra symbol scan"
$n64symExe = Join-Path $root "tools\n64sym\n64sym.exe"
if (-not (Test-Path $n64symExe)) {
    Write-Host "      downloading n64sym v0.9a"
    $n64symDir = Split-Path $n64symExe
    New-Item -ItemType Directory -Force -Path $n64symDir | Out-Null
    $zip = Join-Path $n64symDir "n64sym.zip"
    Invoke-WebRequest -Uri "https://github.com/shygoo/n64sym/releases/download/v0.9a/n64sym-v0.9a.zip" -OutFile $zip
    Expand-Archive -Path $zip -DestinationPath $n64symDir -Force
    Remove-Item $zip
}
& $n64symExe $rom -s -f default -o $n64symOut | Out-Host
if ($LASTEXITCODE -ne 0) { throw "n64sym failed ($LASTEXITCODE)" }

Write-Host "[3/4] Merging names into the symbols TOML"
& python (Join-Path $root "tools\merge_symbols.py") $symbolsToml $n64symOut | Out-Host
if ($LASTEXITCODE -ne 0) { throw "merge_symbols.py failed ($LASTEXITCODE)" }

Write-Host "[4/4] N64Recomp translation"
Get-ChildItem $generatedDir -File -ErrorAction SilentlyContinue | Remove-Item -Force
& $Recompiler (Join-Path $root "recomp\scars.toml") | Out-Host
if ($LASTEXITCODE -ne 0) { throw "N64Recomp failed ($LASTEXITCODE)" }

$count = (Get-ChildItem $generatedDir -File).Count
Write-Host "Done: $count generated files in $generatedDir"
Write-Host "Next: cmake --preset windows-x64-release -DSCARS_LINK_GENERATED=ON"
Write-Host "      cmake --build --preset windows-x64-release"



