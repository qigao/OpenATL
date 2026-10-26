param(
  [Parameter(Mandatory=$true)][string]$LLVMRoot,
  [Parameter(Mandatory=$true)][string]$OpenATLRoot,
  [string]$RATLRoot = 'tools/ratl-conformance',
  [string]$OutDir = 'build-wtl10'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$openPin = '7b587f3e10f3f8d93a01522033de80007368bad5'
$ratlPin = '2e2efa182d7c6ee9037454d23780991fb8543084'
$expected = 'ATL_COM_P0_OK identity=1 map=1 qi_errors=1 refs=0 comptr=1 bstr=1 variant=1'

function Invoke-Checked([string]$exe, [string[]]$args_) {
  & $exe @args_
  if ($LASTEXITCODE -ne 0) { throw "$exe failed with exit $LASTEXITCODE" }
}
function Assert-Pin([string]$path, [string]$pin) {
  if (!(Test-Path (Join-Path $path '.git'))) { throw "Missing git checkout: $path" }
  $actual = (git -C $path rev-parse HEAD).Trim()
  if ($LASTEXITCODE -ne 0 -or $actual -ne $pin) {
    throw "ATL source pin mismatch: $path has $actual"
  }
}
if (!(Test-Path $RATLRoot)) {
  Invoke-Checked git @('clone', '--quiet', 'https://github.com/katahiromz/RATL.git', $RATLRoot)
}
Invoke-Checked git @('-C', $RATLRoot, 'checkout', '--detach', '--quiet', $ratlPin)
Assert-Pin $OpenATLRoot $openPin
Assert-Pin $RATLRoot $ratlPin

$openInc = (Resolve-Path (Join-Path $OpenATLRoot 'include')).Path
$ratlInc = (Resolve-Path (Join-Path $RATLRoot 'include/atl')).Path
$ratlCompat = (Resolve-Path (Join-Path $RATLRoot 'compat')).Path
$source = (Resolve-Path 'tests/atl_conformance/com_p0.cpp').Path
New-Item -ItemType Directory -Force $OutDir | Out-Null

foreach ($cpu in @('x86_64','aarch64')) {
  $compiler = Join-Path $LLVMRoot "$cpu-w64-mingw32-clang++.exe"
  if (!(Test-Path $compiler)) { throw "Missing llvm-mingw C++ driver: $cpu" }
  $suffix = if ($cpu -eq 'x86_64') { 'x64' } else { 'arm64' }

  # The pinned RATL PSEH3 compiler/inline-asm backend is x86-specific.
  # Do not fake ARM64 exception semantics with _USE_DUMMY_PSEH.
  $providers = if ($cpu -eq 'x86_64') { @('openatl','ratl') } else { @('openatl') }
  foreach ($provider in $providers) {
    $exe = "$OutDir/atl_com_$($provider)_$suffix.exe"
    $flags = @('-std=c++17', '-O0', '-DNDEBUG', '-DUNICODE', '-D_UNICODE',
      '-D_ATL_APARTMENT_THREADED', '-D_WIN32_WINNT=0x0601')
    if ($provider -eq 'openatl') {
      $flags += @('-DATL_CONFORMANCE_OPENATL=1', "-I$openInc")
    } else {
      $flags += @('-DATL_CONFORMANCE_RATL=1', '-D__RATL__', '-DINITGUID',
        '-D_USE_PSEH3', '-D_CRT_NON_CONFORMING_SWPRINTFS', "-I$ratlInc", "-I$ratlCompat")
    }
    $flags += @($source, '-o', $exe, '-static',
      '-lole32', '-loleaut32', '-luuid', '-lrpcrt4',
      '-lshlwapi', '-ladvapi32', '-lgdi32', '-luser32', '-lkernel32')
    Invoke-Checked $compiler $flags
    Invoke-Checked python @('tests/wtl10/verify_pe_machine.py', $exe, $cpu)
    Write-Host "ATL COM P0 compiled $provider/$cpu under its isolated include tree"
  }
}

# Linux/macOS cross-host tests stop after PE verification. This script is
# Windows only, with real execution of each x64 executable.
$manifestPath = Join-Path $OutDir 'manifest.json'
if (!(Test-Path $manifestPath)) { throw 'WTL10 SHA256 provenance manifest missing' }
$manifest = Get-Content $manifestPath -Raw | ConvertFrom-Json -AsHashtable
foreach ($provider in @('openatl')) {
  $file = "atl_com_$($provider)_arm64.exe"
  $manifest[$file] = (Get-FileHash (Join-Path $OutDir $file) -Algorithm SHA256).Hash.ToLowerInvariant()
}
$manifest | ConvertTo-Json -Depth 4 | Set-Content $manifestPath -Encoding utf8

for ($i = 1; $i -le 5; $i++) {
  $observations = @()
  foreach ($provider in @('openatl','ratl')) {
    $file = (Resolve-Path (Join-Path $OutDir "atl_com_$($provider)_x64.exe")).Path
    $lines = @(& $file 2>&1)
    if ($LASTEXITCODE -ne 0) { throw "$provider COM P0 failed native x64 run $i : $($lines -join ' / ')" }
    $actual = ($lines | ForEach-Object { "$_".Trim() }) -join ''
    if ($actual -cne $expected) {
      throw "$provider COM P0 semantic mismatch on x64 run $i : $actual"
    }
    $observations += $actual
  }
  if ($observations[0] -cne $observations[1]) { throw "OpenATL/RATL P0 result differs, run $i" }
}
Write-Host "ATL COM P0: OpenATL vs isolated RATL native Windows x64 5/5 PASS (RATL ARM64 PSEH3 NOT qualified)"
