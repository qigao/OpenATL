param(
  [Parameter(Mandatory=$true)][string]$LLVMRoot,
  [Parameter(Mandatory=$true)][string]$OpenATLRoot,
  [string]$OutDir = 'build-wtl10'
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
function Invoke-Checked([string]$program,[string[]]$argv) {
  & $program @argv
  if ($LASTEXITCODE -ne 0) { throw "$program failed exit=$LASTEXITCODE" }
}
$pin='7b587f3e10f3f8d93a01522033de80007368bad5'
$sha=(git -C $OpenATLRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $sha -ne $pin) { throw "OpenATL HEAD mismatch $sha" }
$inc=(Resolve-Path (Join-Path $OpenATLRoot 'include')).Path
$header=Get-Content (Join-Path $inc 'atlbase.h') -Raw
if (!$header.Contains('AtlSandboxRegistryArm') -or
    !$header.Contains('AtlSandboxRegisterServer')) {
  throw 'OpenATL P1-C registrar patch missing'
}
$one=(Resolve-Path 'tests/atl_conformance/registry_real.cpp').Path
$two=(Resolve-Path 'tests/atl_conformance/com_p1b_entry.cpp').Path
$expected='ATL_P1C_REAL_OK guarded=1 writes=2 rollback=1 duplicate=1 targeted=1 clean=1 recovered=1'
New-Item -ItemType Directory -Force $OutDir | Out-Null
foreach ($cpu in @('x86_64','aarch64')) {
  $compiler=Join-Path $LLVMRoot "$cpu-w64-mingw32-clang++.exe"
  if (!(Test-Path $compiler)) { throw "Missing pinned compiler $cpu" }
  $suffix=if ($cpu -eq 'x86_64') {'x64'} else {'arm64'}
  $exe="$OutDir/atl_p1c_registrar_$suffix.exe"
  Invoke-Checked $compiler @(
    '-std=c++17','-O0','-DNDEBUG','-DUNICODE','-D_UNICODE',
    '-D_ATL_APARTMENT_THREADED','-D_WIN32_WINNT=0x0601',
    "-I$inc",$one,$two,'-o',$exe,'-static',
    '-lole32','-loleaut32','-luuid','-lrpcrt4',
    '-lshlwapi','-ladvapi32','-lgdi32','-luser32','-lkernel32')
  Invoke-Checked python @('tests/wtl10/verify_pe_machine.py',$exe,$cpu)
}
$manifestPath=Join-Path $OutDir 'manifest.json'
if (!(Test-Path $manifestPath)) { throw 'Missing WTL10 manifest' }
$manifest=Get-Content $manifestPath -Raw | ConvertFrom-Json -AsHashtable
$arm=Join-Path $OutDir 'atl_p1c_registrar_arm64.exe'
$manifest['atl_p1c_registrar_arm64.exe']=(Get-FileHash $arm -Algorithm SHA256).Hash.ToLowerInvariant()
$manifest | ConvertTo-Json -Depth 4 | Set-Content $manifestPath -Encoding utf8
$native=(Resolve-Path (Join-Path $OutDir 'atl_p1c_registrar_x64.exe')).Path
for ($i=1; $i -le 5; ++$i) {
  $lines=@(& $native 2>&1)
  $rc=$LASTEXITCODE
  $observed=($lines | ForEach-Object { "$_".Trim() }) -join ''
  if ($rc -ne 0 -or $observed -cne $expected) {
    throw "OpenATL P1-C native x64 sandbox registrar failed run $i exit=$($rc): $observed"
  }
}
Write-Host 'OpenATL P1-C native Windows x64 sandbox register/rollback/abrupt-child/recovery 5/5 PASS'
