param([Parameter(Mandatory=$true)][string]$LLVMRoot)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
function Checked([string]$tool,[string[]]$argv) {
  & $tool @argv
  if ($LASTEXITCODE -ne 0) { throw "$tool failed exit $LASTEXITCODE" }
}
$demo=(Resolve-Path 'tools/wtl10-test').Path
$atl=(Resolve-Path 'tools/wtl10-openatl/include').Path
$wtlHeader=Get-ChildItem 'tools/wtl10' -File -Recurse -Filter atlapp.h |
  Where-Object { $_.Directory.Name -ieq 'Include' } | Select-Object -First 1
if (!$wtlHeader) { throw 'WTL10 official header missing' }
$wtl=$wtlHeader.Directory.FullName
if (!(Test-Path (Join-Path $demo 'wtltest.rc'))) { throw 'WTL10 demo RC missing' }
foreach ($arch in @('x86_64','aarch64')) {
  $cxx=Join-Path $LLVMRoot "$arch-w64-mingw32-clang++.exe"
  $windres=Join-Path $LLVMRoot "$arch-w64-mingw32-windres.exe"
  if (!(Test-Path $cxx) -or !(Test-Path $windres)) {
    throw "Pinned llvm-mingw tools missing for $arch"
  }
  $res="build-wtl10/wtltest_$arch.res.o"
  $exe="build-wtl10/wtltest_$arch.exe"
  Checked $windres @('-i',(Join-Path $demo 'wtltest.rc'),'-I',$demo,
                     '-O','coff','-o',$res)
  Checked $cxx @('-std=c++17','-DUNICODE','-D_UNICODE',
    '-D_WIN32_WINNT=0x0601','-DWINVER=0x0601','-D_WIN32_IE=0x0601',
    '-municode','-mwindows',
    "-I$atl","-I$wtl","-I$demo",(Join-Path $demo 'main.cpp'),
    $res,'-o',$exe,'-static','-lole32','-loleaut32','-luuid',
    '-lcomctl32','-lmsimg32','-luxtheme','-lgdi32','-lcomdlg32',
    '-lshell32','-lshlwapi','-ldwmapi','-luser32')
  Checked python @('tests/wtl10/verify_pe_machine.py',$exe,$arch,'--require-resources')
}
Write-Host 'OpenATL WTL10 full upstream SDI + .rc PE x64/ARM64 linked; interactive GUI not qualified'
# Mandatory native Windows x64 and cross-built Windows ARM64 conformance.
& ./scripts/windows-wtl10-gui.ps1 -LLVMRoot $LLVMRoot
& ./scripts/windows-atl-com-conformance.ps1 -LLVMRoot $LLVMRoot -OpenATLRoot 'tools/wtl10-openatl'
& ./scripts/windows-atl-com-p1.ps1 -LLVMRoot $LLVMRoot -OpenATLRoot 'tools/wtl10-openatl'
& ./scripts/windows-atl-com-p1b.ps1 -LLVMRoot $LLVMRoot -OpenATLRoot 'tools/wtl10-openatl'
& ./scripts/windows-atl-registry-sandbox.ps1 -LLVMRoot $LLVMRoot -OpenATLRoot 'tools/wtl10-openatl'
& ./scripts/windows-atl-com-p1c.ps1 -LLVMRoot $LLVMRoot -OpenATLRoot 'tools/wtl10-openatl'
& ./scripts/windows-atl-com-ratl-p1.ps1 -LLVMRoot $LLVMRoot
& ./scripts/windows-atl-ratl-nested.ps1 -LLVMRoot $LLVMRoot
Write-Host 'OpenATL independent native Windows x64 / ARM64 cross-build suite PASS'
