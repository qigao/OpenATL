param(
  [Parameter(Mandatory=$true)][string]$LLVMRoot,
  [string]$RATLRoot='tools/ratl-conformance',
  [string]$PatchedRoot='tools/ratl-nested-patched',
  [string]$OutDir='build-wtl10'
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$pin='2e2efa182d7c6ee9037454d23780991fb8543084'
$sha=(git -C $RATLRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $sha -ne $pin) { throw "Original RATL HEAD mismatch $sha" }
# Never mistake a previously modified working tree for genuine upstream.
git -C $RATLRoot diff --quiet HEAD -- include/atl/statreg.h
if ($LASTEXITCODE -ne 0) { throw 'Original RATL statreg.h checkout is modified' }

$cpp=Join-Path $LLVMRoot 'x86_64-w64-mingw32-clang++.exe'
if (!(Test-Path $cpp)) { throw 'Pinned x64 compiler missing' }
$source=(Resolve-Path 'tests/atl_conformance/ratl_rgs_nested.cpp').Path
$original=(Resolve-Path (Join-Path $RATLRoot 'include/atl')).Path
$compat=(Resolve-Path (Join-Path $RATLRoot 'compat')).Path

function Compile-RATL([string]$inc,[string]$compatInc,[string]$exe) {
  & $cpp @('-std=c++17','-O0','-DNDEBUG','-DUNICODE','-D_UNICODE',
    '-D_ATL_APARTMENT_THREADED','-D_WIN32_WINNT=0x0601',
    '-D__RATL__','-DINITGUID','-D_USE_PSEH3',
    '-D_CRT_NON_CONFORMING_SWPRINTFS',"-I$inc","-I$compatInc",
    $source,'-o',$exe,'-static','-lole32','-loleaut32',
    '-luuid','-lrpcrt4','-lshlwapi','-ladvapi32',
    '-lgdi32','-luser32','-lkernel32')
  if ($LASTEXITCODE -ne 0) { throw "RATL RGS nested build failed: $exe" }
  python tests/wtl10/verify_pe_machine.py $exe x86_64
  if ($LASTEXITCODE -ne 0) { throw "RATL nested test PE machine rejected: $exe" }
}

New-Item -ItemType Directory -Force $OutDir | Out-Null
$baseline="$OutDir/ratl_nested_original_x64.exe"
Compile-RATL $original $compat $baseline

if (Test-Path $PatchedRoot) { throw 'Refuse reused patched checkout' }
git clone --quiet $RATLRoot $PatchedRoot
if ($LASTEXITCODE -ne 0) { throw 'RATL temporary clone failed' }
git -C $PatchedRoot checkout --quiet --detach $pin
if ($LASTEXITCODE -ne 0 -or (git -C $PatchedRoot rev-parse HEAD).Trim() -ne $pin) {
  throw 'RATL temporary checkout pin mismatch'
}
python scripts/ratl-rgs-nested-test-patch.py $PatchedRoot
if ($LASTEXITCODE -ne 0) { throw 'RATL test-only patch refused upstream drift' }
$fixInc=(Resolve-Path (Join-Path $PatchedRoot 'include/atl')).Path
$fixCompat=(Resolve-Path (Join-Path $PatchedRoot 'compat')).Path
$fixed="$OutDir/ratl_nested_patched_x64.exe"
Compile-RATL $fixInc $fixCompat $fixed

# Third PE enables a compile-time fault only in the isolated RATL
# test checkout. Never simulate a permission failure using real ACL/HKLM.
$denied="$OutDir/ratl_nested_injected_x64.exe"
& $cpp @('-std=c++17','-O0','-DNDEBUG','-DUNICODE','-D_UNICODE',
  '-D_ATL_APARTMENT_THREADED','-D_WIN32_WINNT=0x0601',
  '-DRATL_P1D_TEST_DENY_DELETE=1',
  '-D__RATL__','-DINITGUID','-D_USE_PSEH3',
  '-D_CRT_NON_CONFORMING_SWPRINTFS',"-I$fixInc","-I$fixCompat",
  $source,'-o',$denied,'-static','-lole32','-loleaut32',
  '-luuid','-lrpcrt4','-lshlwapi','-ladvapi32','-lgdi32',
  '-luser32','-lkernel32')
if ($LASTEXITCODE -ne 0) { throw 'RATL P1-D injected error executable compile failed' }
python tests/wtl10/verify_pe_machine.py $denied x86_64
if ($LASTEXITCODE -ne 0) { throw 'Injected RATL x64 machine mismatch' }

function Execute-Strict([string]$program,[string]$mode,[string]$expected) {
  $process=New-Object System.Diagnostics.Process
  $psi=New-Object System.Diagnostics.ProcessStartInfo
  $psi.FileName=(Resolve-Path $program).Path
  $psi.Arguments=$mode
  $psi.UseShellExecute=$false
  $psi.CreateNoWindow=$true
  $psi.RedirectStandardOutput=$true
  $psi.RedirectStandardError=$true
  $process.StartInfo=$psi
  if (!$process.Start()) { throw "Cannot launch RATL P1-D $mode" }
  if (!$process.WaitForExit(12000)) {
    $process.Kill($true)
    $process.WaitForExit()
    throw "RATL P1-D $mode timed out (possible recursion loop)"
  }
  $stdout=$process.StandardOutput.ReadToEnd().Trim()
  $stderr=$process.StandardError.ReadToEnd().Trim()
  if ($process.ExitCode -ne 0 -or $stdout -cne $expected -or $stderr) {
    throw "RATL P1-D mode=$mode code=$($process.ExitCode) expected=$expected actual=$stdout stderr=$stderr"
  }
  if ($mode -like '*force' -or $mode -like '*delete') {
    Write-Host "Verified original-vs-patched RATL: $stdout"
  }
}
for($i=1;$i -le 5;++$i) {
  foreach($mode in @('nested','types','noremove','baseline-force','baseline-delete')) {
    $extra = if ($mode -like '*force' -or $mode -like '*delete') { ' hr=0x00000000 stale=1' } else { '' }
    Execute-Strict $baseline $mode "RATL_P1D_OK mode=$mode$extra"
  }
  foreach($mode in @('nested','types','noremove','patched-force','patched-delete')) {
    $extra = if ($mode -like '*force' -or $mode -like '*delete') { ' hr=0x00000000 stale=0' } else { '' }
    Execute-Strict $fixed $mode "RATL_P1D_OK mode=$mode$extra"
  }
  foreach($mode in @('denied-force','denied-delete')) {
    Execute-Strict $denied $mode "RATL_P1D_OK mode=$mode hr=0x80070005 stale=1"
  }
}
Write-Host 'RATL P1-D original false-success reproduced; patched nested deletion and access-denied rollback 5/5 PASS'
