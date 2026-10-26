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
$sha=(git -C $OpenATLRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $sha -ne $pin) { throw "OpenATL source pin mismatch $sha" }
$inc=(Resolve-Path (Join-Path $OpenATLRoot 'include')).Path
$base=Get-Content (Join-Path $inc 'atlbase.h') -Raw
if (!$base.Contains('return E_NOTIMPL;')) { throw 'OpenATL registry stubs must fail closed' }
$source=(Resolve-Path 'tests/atl_conformance/registry_sandbox.cpp').Path
$expected='ATL_REG_SANDBOX_OK isolated=1 redirect=1 rollback=1 restored=1 clean=1 stub_failclosed=1'
New-Item -ItemType Directory -Force $OutDir | Out-Null

foreach ($cpu in @('x86_64','aarch64')) {
  $cxx=Join-Path $LLVMRoot "$cpu-w64-mingw32-clang++.exe"
  if (!(Test-Path $cxx)) { throw "Missing pinned llvm-mingw $cpu compiler" }
  $suffix=if ($cpu -eq 'x86_64') { 'x64' } else { 'arm64' }
  $exe="$OutDir/atl_registry_sandbox_$suffix.exe"
  Invoke-Checked $cxx @('-std=c++17','-O0','-DNDEBUG',
    '-DUNICODE','-D_UNICODE','-D_ATL_APARTMENT_THREADED',
    '-D_WIN32_WINNT=0x0601',"-I$inc",
    $source,'-o',$exe,'-static',
    '-lole32','-loleaut32','-luuid','-lrpcrt4',
    '-lshlwapi','-ladvapi32','-lgdi32','-luser32','-lkernel32')
  Invoke-Checked python @('tests/wtl10/verify_pe_machine.py',$exe,$cpu)
}
$manifestPath=Join-Path $OutDir 'manifest.json'
if (!(Test-Path $manifestPath)) { throw 'Missing WTL10 provenance manifest' }
$manifest=Get-Content $manifestPath -Raw | ConvertFrom-Json -AsHashtable
$arm=Join-Path $OutDir 'atl_registry_sandbox_arm64.exe'
$manifest['atl_registry_sandbox_arm64.exe']=(Get-FileHash $arm -Algorithm SHA256).Hash.ToLowerInvariant()
$manifest | ConvertTo-Json -Depth 4 | Set-Content $manifestPath -Encoding utf8
$native=(Resolve-Path (Join-Path $OutDir 'atl_registry_sandbox_x64.exe')).Path
for ($i=1; $i -le 5; ++$i) {
  $lines=@(& $native 2>&1)
  $rc=$LASTEXITCODE
  $actual=($lines | ForEach-Object { "$_".Trim() }) -join ''
  if ($rc -ne 0 -or $actual -cne $expected) {
    throw "OpenATL registry sandbox x64 failed run $i exit=$($rc): $actual"
  }
}
Write-Host 'OpenATL Windows x64 process-only HKCR sandbox/isolation/rollback/cleanup 5/5 PASS'
