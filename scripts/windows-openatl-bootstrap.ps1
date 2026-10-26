param(
  [ValidateSet('LLVM','MSVC')][string]$Toolchain = 'LLVM',
  [ValidateSet('x64','arm64')][string]$TargetArchitecture = 'x64'
)
# WTL comes ONLY from the vcpkg manifest+overlay, never an ad-hoc ZIP.
# This bootstrap does not install/export OpenATL itself.
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
function Run([string]$file,[string[]]$args_) {
  & $file @args_
  if ($LASTEXITCODE -ne 0) { throw "$file failed exit=$LASTEXITCODE" }
}
$demoPin='63ba6a992cb3d79d56be66cd283f6615c6646b3d'
$vcpkgPin='3037cbe976a6872e56e8b07152e19a8344360491'
if (!(Test-Path 'include/atlbase.h') -or !(Test-Path 'include/atlwin.h')) {
  throw 'Missing repository-owned OpenATL headers; no remote fallback'
}
if (Test-Path 'tools/vcpkg' -or (Test-Path 'tools/wtl10')) {
  throw 'Refuse reused vcpkg or legacy manual WTL checkout'
}
New-Item -ItemType Directory -Force 'build-wtl10' | Out-Null
# Install the vcpkg tool, not an OpenATL Installed SDK.
Run git @('init','--quiet','tools/vcpkg')
Run git @('-C','tools/vcpkg','fetch','--depth','1','https://github.com/microsoft/vcpkg.git',$vcpkgPin)
Run git @('-C','tools/vcpkg','checkout','--quiet','--detach','FETCH_HEAD')
if ((git -C tools/vcpkg rev-parse HEAD).Trim() -ne $vcpkgPin) {
  throw 'vcpkg Git checkout SHA mismatch'
}
Run (Join-Path $PWD 'tools/vcpkg/bootstrap-vcpkg.bat') @('-disableMetrics')
$vcpkg=(Resolve-Path 'tools/vcpkg/vcpkg.exe').Path
$triplets=if ($Toolchain -eq 'LLVM') {
  if (!$env:OPENATL_LLVM_BIN) { throw 'LLVM bin directory not set' }
  $env:OPENATL_LLVM_MINGW_BIN=$env:OPENATL_LLVM_BIN
  @('x64-openatl-mingw','arm64-openatl-mingw')
} else {
  @("$TargetArchitecture-windows-static")
}
$overlay=(Resolve-Path 'cmake/vcpkg-triplets').Path
foreach ($triplet in $triplets) {
  Run $vcpkg @('install',"--triplet=$triplet","--overlay-triplets=$overlay")
  $directory="vcpkg_installed/$triplet"
  $header=Join-Path $directory 'include/atlapp.h'
  if (!(Test-Path $header)) { throw "Missing vcpkg WTL header at $header" }
  if (Test-Path (Join-Path $directory 'share/atl')) {
    throw "Unexpected Microsoft ATL stub in WTL dependency graph: $triplet"
  }
  $version=Get-Content $header -TotalCount 130
  if (!($version -match 'WTL version 10\.0') -or !($version -match '_WTL_VER')) {
    throw "Wrong vcpkg WTL version for $triplet"
  }
}
# The upstream sample is test data only, not WTL's implementation.
if (Test-Path 'tools/wtl10-test') { throw 'Refuse reused WTL demo checkout' }
Run git @('clone','--quiet','https://github.com/kkhaike/wtltest.git','tools/wtl10-test')
Run git @('-C','tools/wtl10-test','checkout','--quiet','--detach',$demoPin)
if ((git -C tools/wtl10-test rev-parse HEAD).Trim() -ne $demoPin) {
  throw 'WTL10 demo source SHA mismatch'
}
'{}' | Set-Content 'build-wtl10/manifest.json' -Encoding utf8
Write-Host "OpenATL repository ATL + manifest-installed WTL from vcpkg: $($triplets -join ', '); zero Microsoft ATL"
