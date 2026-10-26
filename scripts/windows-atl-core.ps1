param(
  [Parameter(Mandatory=$true)][string]$LLVMRoot,
  [string]$OutDir='build-wtl10'
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$atl=(Resolve-Path 'include').Path
if(!(Test-Path (Join-Path $atl 'atlfile.h')) -or
   !(Test-Path (Join-Path $atl 'atlsync.h'))) {
  throw 'Missing repository-owned ATL file/sync implementations'
}
$source=(Resolve-Path 'tests/atl_conformance/atl_core.cpp').Path
New-Item -ItemType Directory -Force $OutDir | Out-Null
foreach($cpu in @('x86_64','aarch64')) {
  $cxx=Join-Path $LLVMRoot "$cpu-w64-mingw32-clang++.exe"
  if(!(Test-Path $cxx)) {throw "Missing pinned llvm-mingw compiler $cpu"}
  $suffix=if($cpu -eq 'x86_64'){'x64'}else{'arm64'}
  $exe="$OutDir/atl_core_$suffix.exe"
  & $cxx @('-std=c++17','-O0','-DNDEBUG','-DUNICODE','-D_UNICODE',
    '-D_WIN32_WINNT=0x0601',"-I$atl",$source,'-o',$exe,'-static',
    '-lole32','-loleaut32','-luuid','-lrpcrt4','-lshlwapi',
    '-ladvapi32','-lgdi32','-luser32','-lkernel32')
  if($LASTEXITCODE -ne 0) {throw "OpenATL core $cpu compile failed"}
  python tests/wtl10/verify_pe_machine.py $exe $cpu
  if($LASTEXITCODE -ne 0) {throw "OpenATL core $cpu PE machine mismatch"}
}
$manifestPath=Join-Path $OutDir 'manifest.json'
if(!(Test-Path $manifestPath)){throw 'No ARM64 provenance manifest'}
$manifest=Get-Content $manifestPath -Raw | ConvertFrom-Json -AsHashtable
$manifest['atl_core_arm64.exe']=
  (Get-FileHash (Join-Path $OutDir 'atl_core_arm64.exe') -Algorithm SHA256).Hash.ToLowerInvariant()
$manifest | ConvertTo-Json -Depth 4 | Set-Content $manifestPath -Encoding utf8
$native=(Resolve-Path (Join-Path $OutDir 'atl_core_x64.exe')).Path
$expected='ATL_CORE_OK file=1 string=1 sync=1 no_com=1'
for($i=1;$i -le 5;$i++) {
  $out=@(& $native 2>&1)
  $rc=$LASTEXITCODE
  $actual=($out | ForEach-Object{"$_".Trim()}) -join ''
  if($rc -ne 0 -or $actual -cne $expected) {
    throw "OpenATL core native x64 contract failed iteration $i, exit=$($rc): $actual"
  }
}
Write-Host 'OpenATL owned CAtlFile/CString/Win32 sync x64 native 5/5 PASS; ARM64 SHA256 ready'
