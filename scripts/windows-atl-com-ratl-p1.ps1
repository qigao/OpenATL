param(
  [Parameter(Mandatory=$true)][string]$LLVMRoot,
  [string]$RATLRoot = 'tools/ratl-conformance',
  [string]$OutDir = 'build-wtl10'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$pin = '2e2efa182d7c6ee9037454d23780991fb8543084'
$expected = 'RATL_COM_P1_REF_OK create=1 rollback=1 factory=1 nonagg=1 locks=1 running=1 revoke=1'
if (!(Test-Path (Join-Path $RATLRoot '.git'))) { throw 'RATL isolated checkout missing' }
$sha = (git -C $RATLRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $sha -ne $pin) { throw "RATL x64 P1 pin mismatch: $sha" }
$cxx = Join-Path $LLVMRoot 'x86_64-w64-mingw32-clang++.exe'
if (!(Test-Path $cxx)) { throw 'Pinned llvm-mingw x64 compiler unavailable' }
$ratl = (Resolve-Path (Join-Path $RATLRoot 'include/atl')).Path
$compat = (Resolve-Path (Join-Path $RATLRoot 'compat')).Path
$source = (Resolve-Path 'tests/atl_conformance/ratl_p1_reference.cpp').Path
New-Item -ItemType Directory -Force $OutDir | Out-Null
$exe = "$OutDir/atl_com_p1_ratl_x64.exe"
& $cxx @('-std=c++17','-O0','-DNDEBUG','-DUNICODE','-D_UNICODE',
    '-D_ATL_APARTMENT_THREADED','-D_WIN32_WINNT=0x0601',
    '-D__RATL__','-DINITGUID','-D_USE_PSEH3',
    '-D_CRT_NON_CONFORMING_SWPRINTFS',"-I$ratl","-I$compat",
    $source,'-o',$exe,'-static','-lole32','-loleaut32',
    '-luuid','-lrpcrt4','-lshlwapi','-ladvapi32',
    '-lgdi32','-luser32','-lkernel32')
if ($LASTEXITCODE -ne 0) { throw 'RATL P1 x64 native reference compiler failed' }
python tests/wtl10/verify_pe_machine.py $exe x86_64
if ($LASTEXITCODE -ne 0) { throw 'RATL P1 x64 PE machine mismatch' }

$native=(Resolve-Path $exe).Path
for ($i=1; $i -le 5; ++$i) {
  $output = @(& $native 2>&1)
  $rc = $LASTEXITCODE
  $line = ($output | ForEach-Object { "$_".Trim() }) -join ''
  if ($rc -ne 0 -or $line -cne $expected) {
    throw "Isolated RATL COM P1 reference native x64 failed run $i, exit=${rc}: $line"
  }
}
Write-Host 'Independent RATL COM P1 x64 factory/lifecycle/temporary class-object 5/5 PASS; ARM64 NOT qualified'

# Separate RATL-ONLY statreg.h RGS test (x64; never uses OpenATL objects).
$regSource=(Resolve-Path 'tests/atl_conformance/ratl_registry_reference.cpp').Path
$regExe="$OutDir/atl_ratl_registry_ref_x64.exe"
& $cxx @('-std=c++17','-O0','-DNDEBUG','-DUNICODE','-D_UNICODE',
    '-D_ATL_APARTMENT_THREADED','-D_WIN32_WINNT=0x0601',
    '-D__RATL__','-DINITGUID','-D_USE_PSEH3',
    '-D_CRT_NON_CONFORMING_SWPRINTFS',"-I$ratl","-I$compat",
    $regSource,'-o',$regExe,'-static','-lole32','-loleaut32',
    '-luuid','-lrpcrt4','-lshlwapi','-ladvapi32',
    '-lgdi32','-luser32','-lkernel32')
if ($LASTEXITCODE -ne 0) { throw 'RATL statreg RGS reference x64 compile failed' }
python tests/wtl10/verify_pe_machine.py $regExe x86_64
if ($LASTEXITCODE -ne 0) { throw 'RATL RGS reference machine mismatch' }
$regNative=(Resolve-Path $regExe).Path
$expectedReg='RATL_REG_REF_OK rgs=1 string=1 dword=1 unregister=1 invalid=1 clean=1'
for ($i=1; $i -le 5; ++$i) {
  $lines=@(& $regNative 2>&1)
  $resultCode=$LASTEXITCODE
  $resultText=($lines | ForEach-Object { "$_".Trim() }) -join ''
  if ($resultCode -ne 0 -or $resultText -cne $expectedReg) {
    throw "RATL RGS native x64 failed run $i exit=$($resultCode): $resultText"
  }
}
Write-Host 'Independent RATL statreg RGS registry sandbox x64 native 5/5 PASS; ARM64 NOT qualified'
