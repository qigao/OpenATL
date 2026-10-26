param(
  [Parameter(Mandatory=$true)]
  [ValidateSet('x64','arm64')][string]$TargetArchitecture,
  [string]$OutRoot='build-msvc'
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest

if ($env:RUNNER_OS -and $env:RUNNER_OS -ne 'Windows') { throw 'MSVC requires real Windows runner' }
$compiler=Get-Command cl.exe -ErrorAction Stop
$resourceCompiler=Get-Command rc.exe -ErrorAction Stop
if (!$compiler.Source.EndsWith('cl.exe',[StringComparison]::OrdinalIgnoreCase)) {
  throw 'MSVC cl.exe is not on PATH'
}
$expectedTarget=if ($TargetArchitecture -eq 'x64') {'x64'} else {'arm64'}
$actualTarget=$env:VSCMD_ARG_TGT_ARCH
if ($actualTarget) {
  $ok=if ($expectedTarget -eq 'x64') {
    $actualTarget -in @('x64','amd64')
  } else {$actualTarget -eq 'arm64'}
  if (!$ok) { throw "MSVC Developer Command Prompt target $actualTarget != $expectedTarget" }
}
$atlPin='7b587f3e10f3f8d93a01522033de80007368bad5'
$atlCheckout='tools/wtl10-openatl'
if ((git -C $atlCheckout rev-parse HEAD).Trim() -ne $atlPin) {
  throw 'Refuse non-pinned OpenATL source in MSVC lane'
}
$atl=(Resolve-Path "$atlCheckout/include").Path
$marker=Get-Content (Join-Path $atl 'atlbase.h') -TotalCount 3
if (!(($marker -join ' ') -match 'OpenATL - Clean-room ATL subset')) {
  throw 'Refuse accidental use of Microsoft ATL instead of pinned OpenATL'
}
$wtlHeader=Get-ChildItem 'tools/wtl10' -Recurse -File -Filter atlapp.h |
  Where-Object { $_.Directory.Name -ieq 'Include' } | Select-Object -First 1
if (!$wtlHeader) { throw 'Official WTL10 atlapp.h missing' }
$wtl=$wtlHeader.Directory.FullName
$demo=(Resolve-Path 'tools/wtl10-test').Path
if (!(Test-Path (Join-Path $demo 'wtltest.rc'))) {throw 'Pinned wtltest.rc missing'}

$build="$OutRoot-$TargetArchitecture"
$settings=@('-S','.','-B',$build,'-G','Ninja',
  '-DCMAKE_BUILD_TYPE=Release',
  '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded',
  "-DOPENATL_ATL_INCLUDE_DIR=$atl",
  "-DOPENATL_WTL_INCLUDE_DIR=$wtl",
  "-DOPENATL_WTL_DEMO_DIR=$demo",
  '-DOPENATL_BUILD_QUALIFICATION=ON',
  '-DOPENATL_TEST_PATCHES_APPLIED=ON')
& cmake @settings
if($LASTEXITCODE -ne 0){throw "MSVC CMake configure failed for $TargetArchitecture"}
& cmake --build $build --config Release --parallel 2
if($LASTEXITCODE -ne 0){throw "MSVC CMake build failed for $TargetArchitecture"}
$ident=(Get-ChildItem "$build/CMakeFiles" -Recurse -Filter CMakeCXXCompiler.cmake |
        Select-Object -First 1)
if(!$ident -or !(Select-String -Path $ident.FullName -Pattern 'CMAKE_CXX_COMPILER_ID "MSVC"' -Quiet)) {
  throw 'Build was not compiled by MSVC cl.exe'
}

$machine=if($TargetArchitecture -eq 'x64'){'x86_64'}else{'aarch64'}
$names=@('openatl_gui_runtime','openatl_com_client','openatl_thunk_fault','openatl_wtl10_sdi')
foreach($name in $names) {
  $binary=Join-Path $build "$name.exe"
  $args_=@('tests/wtl10/verify_pe_machine.py',$binary,$machine)
  if($name -eq 'openatl_gui_runtime' -or $name -eq 'openatl_wtl10_sdi') {
    $args_+=@('--require-resources')
  }
  & python @args_
  if($LASTEXITCODE -ne 0) {throw "MSVC $TargetArchitecture PE rejected: $name"}
}

if ($TargetArchitecture -eq 'x64') {
  & ctest --test-dir $build --output-on-failure --repeat until-fail:5
  if($LASTEXITCODE -ne 0){throw 'MSVC x64 COM/WTL test failure'}
  Write-Host 'MSVC x64: WTL10 UI no-COM + COM client + W^X OS fault admission 8 CTests x5 PASS'
} else {
  # Handoff to genuine Windows ARM64 runner after verifying SHA256.
  $manifest=@{}
  foreach($name in $names) {
    $file="$name.exe"
    $manifest[$file]=(Get-FileHash (Join-Path $build $file) -Algorithm SHA256).Hash.ToLowerInvariant()
  }
  $manifest | ConvertTo-Json -Depth 3 |
    Set-Content (Join-Path $build 'manifest-msvc-arm64.json') -Encoding utf8
  Write-Host 'MSVC ARM64: exact PE SHA256 + .rsrc provenance ready; native execution required'
}
