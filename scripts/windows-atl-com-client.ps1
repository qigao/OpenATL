param(
  [Parameter(Mandatory=$true)][string]$LLVMRoot,
  [Parameter(Mandatory=$true)][string]$OpenATLRoot,
  [string]$OutDir = 'build-wtl10'
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$pin='7b587f3e10f3f8d93a01522033de80007368bad5'
$actual=(git -C $OpenATLRoot rev-parse HEAD).Trim()
if($LASTEXITCODE -ne 0 -or $actual -ne $pin) {throw "OpenATL client pin mismatch $actual"}
$includes=(Resolve-Path (Join-Path $OpenATLRoot 'include')).Path
$source=(Resolve-Path 'tests/atl_conformance/com_client.cpp').Path
$expected='ATL_COM_CLIENT_OK activate=1 qi=1 comptr=1 bstr=1 variant=1 unregister=0'
New-Item -ItemType Directory -Force $OutDir | Out-Null
foreach($arch in @('x86_64','aarch64')) {
  $cxx=Join-Path $LLVMRoot "$arch-w64-mingw32-clang++.exe"
  if(!(Test-Path $cxx)){throw "Missing exact-pinned llvm-mingw $arch"}
  $suffix=if($arch -eq 'x86_64'){'x64'}else{'arm64'}
  $exe="$OutDir/atl_com_client_$suffix.exe"
  & $cxx @('-std=c++17','-O0','-DNDEBUG','-DUNICODE','-D_UNICODE',
    '-D_WIN32_WINNT=0x0601',"-I$includes",$source,'-o',$exe,
    '-static','-lole32','-loleaut32','-luuid','-lrpcrt4',
    '-lshlwapi','-ladvapi32','-lgdi32','-luser32','-lkernel32')
  if($LASTEXITCODE -ne 0) {throw "COM client Windows $arch compile failed"}
  python tests/wtl10/verify_pe_machine.py $exe $arch
  if($LASTEXITCODE -ne 0) {throw "COM client PE $arch rejected"}
}
$manifestPath=Join-Path $OutDir 'manifest.json'
if(!(Test-Path $manifestPath)){throw 'Missing ARM64 manifest from GUI qualification'}
$manifest=Get-Content $manifestPath -Raw | ConvertFrom-Json -AsHashtable
$arm64=Join-Path $OutDir 'atl_com_client_arm64.exe'
$manifest['atl_com_client_arm64.exe']=(Get-FileHash $arm64 -Algorithm SHA256).Hash.ToLowerInvariant()
$manifest | ConvertTo-Json -Depth 4 | Set-Content $manifestPath -Encoding utf8
$native=(Resolve-Path (Join-Path $OutDir 'atl_com_client_x64.exe')).Path
for($i=1;$i -le 5;++$i) {
  $out=@(& $native 2>&1)
  $status=$LASTEXITCODE
  $actual=($out|ForEach-Object{"$_".Trim()})-join ''
  if($status -ne 0 -or $actual -cne $expected) {
    throw "OpenATL COM Client native x64 run $i failed code=$($status): $actual"
  }
}
Write-Host 'OpenATL COM CLIENT Windows x64 5/5 PASS; Windows ARM64 PE+SHA256 ready'
