param(
  [Parameter(Mandatory=$true)][string]$LLVMRoot,
  [string]$OutDir = 'build-wtl10'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Invoke-Checked([string]$exe, [string[]]$argv) {
  & $exe @argv
  if ($LASTEXITCODE -ne 0) {
    throw "$exe returned $LASTEXITCODE"
  }
}
$atl = (Resolve-Path 'tools/wtl10-openatl/include').Path
$header = Get-ChildItem 'tools/wtl10' -Recurse -File -Filter atlapp.h |
  Where-Object { $_.Directory.Name -ieq 'Include' } |
  Select-Object -First 1
if (-not $header) { throw 'WTL10 10.0 release not found' }
$wtl = $header.Directory.FullName
$root = (Resolve-Path 'tests/wtl10').Path
New-Item -ItemType Directory -Force $OutDir | Out-Null

foreach ($cpu in @('x86_64','aarch64')) {
  $cxx = Join-Path $LLVMRoot "$cpu-w64-mingw32-clang++.exe"
  $windres = Join-Path $LLVMRoot "$cpu-w64-mingw32-windres.exe"
  foreach ($exe in @($cxx,$windres)) {
    if (!(Test-Path $exe)) { throw "Missing pinned tool: $exe" }
  }
  $resource = "$OutDir/wtl10_gui_runtime_$cpu.res.o"
  $exePath = "$OutDir/wtl10_gui_runtime_$cpu.exe"
  Invoke-Checked $windres @('-i', 'tests/wtl10/gui_runtime.rc',
    '-I', 'tests/wtl10', '-O', 'coff', '-o', $resource)
  $args_ = @('-std=c++17', '-O0', '-DUNICODE', '-D_UNICODE',
    '-D_WIN32_WINNT=0x0601', '-DWINVER=0x0601', '-D_WIN32_IE=0x0601',
    "-I$atl", "-I$wtl", "-I$root",
    'tests/wtl10/gui_runtime.cpp', $resource, '-o', $exePath,
    '-static', '-loleaut32', '-lole32', '-luuid',
    '-lcomctl32', '-lgdi32', '-luser32', '-lshell32',
    '-lshlwapi', '-lcomdlg32', '-luxtheme', '-lmsimg32')
  Invoke-Checked $cxx $args_
  Invoke-Checked python @('tests/wtl10/verify_pe_machine.py', $exePath,
    $cpu, '--require-resources')
  Write-Host "Clang++ OpenATL/WTL10 native GUI runtime built: $cpu"
}
$rootDir = (Resolve-Path $OutDir).Path
Push-Location $rootDir
try {
  for ($i = 1; $i -le 5; $i++) {
    Invoke-Checked (Join-Path $rootDir 'wtl10_gui_runtime_x86_64.exe') @()
  }
} finally {
  Pop-Location
}

$manifestPath = "$OutDir/manifest.json"
if (!(Test-Path $manifestPath)) { throw 'Missing runtime manifest from Windows WTL10 bridge' }
$manifest = Get-Content $manifestPath -Raw | ConvertFrom-Json -AsHashtable
$arm64 = "$OutDir/wtl10_gui_runtime_aarch64.exe"
$manifest['wtl10_gui_runtime_aarch64.exe'] =
  (Get-FileHash $arm64 -Algorithm SHA256).Hash.ToLowerInvariant()
$manifest | ConvertTo-Json -Depth 3 | Set-Content $manifestPath -Encoding utf8
Write-Host 'Native WTL10 GUI runtime x64 5/5 PASS; ARM64 executable and SHA256 manifest ready'
