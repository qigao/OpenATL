param(
  [Parameter(Mandatory=$true)][string]$LLVMRoot,
  [Parameter(Mandatory=$true)][string]$OpenATLRoot,
  [string]$OutDir = 'build-wtl10'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Invoke-Checked([string]$program, [string[]]$args_) {
  & $program @args_
  if ($LASTEXITCODE -ne 0) { throw "$program failed, exit=$LASTEXITCODE" }
}
$pin = '7b587f3e10f3f8d93a01522033de80007368bad5'
$actual = (git -C $OpenATLRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $actual -ne $pin) { throw "OpenATL P1 pin mismatch $actual" }
$include = (Resolve-Path (Join-Path $OpenATLRoot 'include')).Path
$header = Get-Content (Join-Path $include 'atlbase.h') -Raw
if (!$header.Contains('return E_NOTIMPL;')) { throw 'Unpatched OpenATL registry stubs' }
$source = (Resolve-Path 'tests/atl_conformance/com_p1.cpp').Path
$expected = 'ATL_COM_P1_OK create=1 rollback=1 factory=1 nonagg=1 locks=1 running=1 revoke=1 registry_failclosed=1'
New-Item -ItemType Directory -Force $OutDir | Out-Null

foreach ($cpu in @('x86_64','aarch64')) {
  $cxx = Join-Path $LLVMRoot "$cpu-w64-mingw32-clang++.exe"
  if (!(Test-Path $cxx)) { throw "Missing target driver: $cpu" }
  $suffix = if ($cpu -eq 'x86_64') { 'x64' } else { 'arm64' }
  $path = "$OutDir/atl_com_p1_openatl_$suffix.exe"
  Invoke-Checked $cxx @('-std=c++17','-O0','-DNDEBUG',
    '-DUNICODE','-D_UNICODE','-D_ATL_APARTMENT_THREADED','-D_WIN32_WINNT=0x0601',
    "-I$include",$source,'-o',$path,'-static',
    '-lole32','-loleaut32','-luuid','-lrpcrt4','-lshlwapi',
    '-ladvapi32','-lgdi32','-luser32','-lkernel32')
  Invoke-Checked python @('tests/wtl10/verify_pe_machine.py',$path,$cpu)
  Write-Host "Compiled separate OpenATL COM P1 Windows $cpu PE"
}
$manifestPath = Join-Path $OutDir 'manifest.json'
if (!(Test-Path $manifestPath)) { throw 'Missing WTL10 ARM64 manifest' }
$manifest = Get-Content $manifestPath -Raw | ConvertFrom-Json -AsHashtable
$arm64Path = Join-Path $OutDir 'atl_com_p1_openatl_arm64.exe'
$manifest['atl_com_p1_openatl_arm64.exe'] = (Get-FileHash $arm64Path -Algorithm SHA256).Hash.ToLowerInvariant()
$manifest | ConvertTo-Json -Depth 4 | Set-Content $manifestPath -Encoding utf8

$x64 = (Resolve-Path (Join-Path $OutDir 'atl_com_p1_openatl_x64.exe')).Path
for ($i=1; $i -le 5; ++$i) {
  $out = @(& $x64 2>&1)
  $code = $LASTEXITCODE
  $observed = ($out | ForEach-Object { "$_".Trim() }) -join ''
  if ($code -ne 0 -or $observed -cne $expected) {
    throw "Native OpenATL COM P1 x64 failed run $i code=$code, output=$observed"
  }
}
Write-Host 'Native OpenATL COM P1 x64 class factory/rollback/temporary activation 5/5 PASS'
