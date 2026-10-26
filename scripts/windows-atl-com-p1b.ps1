param(
  [Parameter(Mandatory=$true)][string]$LLVMRoot,
  [Parameter(Mandatory=$true)][string]$OpenATLRoot,
  [string]$OutDir = 'build-wtl10'
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest

function Invoke-Checked([string]$program,[string[]]$argv) {
  & $program @argv
  if ($LASTEXITCODE -ne 0) { throw "$program failed with exit $LASTEXITCODE" }
}
$pin='7b587f3e10f3f8d93a01522033de80007368bad5'
$actual=(git -C $OpenATLRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $actual -ne $pin) { throw "Pinned OpenATL HEAD mismatch $actual" }
$inc=(Resolve-Path (Join-Path $OpenATLRoot 'include')).Path
$header=Get-Content (Join-Path $inc 'atlbase.h') -Raw
$com=Get-Content (Join-Path $inc 'atlcom.h') -Raw
if (!$header.Contains('AtlAutoObjectSlots') -or
    !$header.Contains('RegisterClassObjects') -or
    !$com.Contains('AtlRegisterAutoObjectEntry')) {
  throw 'P1-B map hardening patch is absent'
}
$one=(Resolve-Path 'tests/atl_conformance/com_p1b_map.cpp').Path
$two=(Resolve-Path 'tests/atl_conformance/com_p1b_entry.cpp').Path
$expected='ATL_COM_P1B_OK map=2 rollback=1 activation=2 revoke=1 wrongthread=1 registry=0'
New-Item -ItemType Directory -Force $OutDir | Out-Null

foreach ($cpu in @('x86_64','aarch64')) {
  $compiler=Join-Path $LLVMRoot "$cpu-w64-mingw32-clang++.exe"
  if (!(Test-Path $compiler)) { throw "Pinned llvm-mingw compiler not found: $cpu" }
  $suffix=if ($cpu -eq 'x86_64') { 'x64' } else { 'arm64' }
  $output="$OutDir/atl_com_p1b_openatl_$suffix.exe"
  Invoke-Checked $compiler @(
    '-std=c++17','-O0','-DNDEBUG','-DUNICODE','-D_UNICODE',
    '-D_ATL_APARTMENT_THREADED','-D_WIN32_WINNT=0x0601',
    "-I$inc",$one,$two,'-o',$output,'-static',
    '-lole32','-loleaut32','-luuid','-lrpcrt4','-lshlwapi',
    '-ladvapi32','-lgdi32','-luser32','-lkernel32')
  Invoke-Checked python @('tests/wtl10/verify_pe_machine.py',$output,$cpu)
  Write-Host "OpenATL COM P1-B two-TU map compiled for $cpu"
}
$manifestPath=Join-Path $OutDir 'manifest.json'
if (!(Test-Path $manifestPath)) { throw 'Missing ARM64 provenance manifest' }
$manifest=Get-Content $manifestPath -Raw | ConvertFrom-Json -AsHashtable
$armPath=Join-Path $OutDir 'atl_com_p1b_openatl_arm64.exe'
$manifest['atl_com_p1b_openatl_arm64.exe']=(Get-FileHash $armPath -Algorithm SHA256).Hash.ToLowerInvariant()
$manifest | ConvertTo-Json -Depth 4 | Set-Content $manifestPath -Encoding utf8

$x64=(Resolve-Path (Join-Path $OutDir 'atl_com_p1b_openatl_x64.exe')).Path
for ($i=1; $i -le 5; ++$i) {
  $out=@(& $x64 2>&1)
  $rc=$LASTEXITCODE
  $observed=($out | ForEach-Object { "$_".Trim() }) -join ''
  if ($rc -ne 0 -or $observed -cne $expected) {
    throw "OpenATL native x64 COM P1-B run $i failed exit=$($rc): $observed"
  }
  foreach ($mode in @('invalid','duplicate','capacity')) {
    $negative = @(& $x64 $mode 2>&1)
    $negativeRC = $LASTEXITCODE
    $negativeText = ($negative | ForEach-Object { "$_".Trim() }) -join ''
    $negativeExpected = "ATL_COM_P1B_NEG_OK mode=$mode"
    if ($negativeRC -ne 0 -or $negativeText -cne $negativeExpected) {
      throw "OpenATL native x64 P1-B $mode rejection failed repetition $i exit=$($negativeRC): $negativeText"
    }
  }
}
Write-Host 'OpenATL native x64 P1-B automatic map/partial rollback/revoke 5/5 PASS'
