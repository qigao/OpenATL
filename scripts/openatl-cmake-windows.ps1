param([Parameter(Mandatory=$true)][string]$LLVMRoot)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$atl=(Resolve-Path include).Path
$wtlFile=Get-ChildItem 'tools/wtl10' -Recurse -File -Filter atlapp.h |
  Where-Object { $_.Directory.Name -ieq 'Include' } | Select-Object -First 1
if (!$wtlFile) { throw 'Official WTL10 header missing' }
$wtl=$wtlFile.Directory.FullName
$toolchain=(Resolve-Path 'cmake/toolchains/llvm-mingw.cmake').Path
foreach($arch in @('x86_64','aarch64')) {
  $build="build-openatl-cmake-$arch"
  $args_=@('-S','.','-B',$build,'-G','Ninja',
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain",
    "-DOPENATL_LLVM_MINGW_BIN=$LLVMRoot",
    "-DOPENATL_TARGET_ARCH=$arch",
    "-DOPENATL_WTL_INCLUDE_DIR=$wtl",
    '-DOPENATL_BUILD_QUALIFICATION=ON')
  & cmake @args_
  if ($LASTEXITCODE -ne 0) { throw "CMake configure Windows $arch failed" }
  & cmake --build $build --parallel 2
  if ($LASTEXITCODE -ne 0) { throw "CMake build Windows $arch failed" }
  python tests/wtl10/verify_pe_machine.py "$build/openatl_gui_runtime.exe" $arch --require-resources
  if ($LASTEXITCODE -ne 0) { throw "CMake GUI PE architecture mismatch: $arch" }
  foreach($target in @('openatl_com_client','openatl_thunk_fault','openatl_atl_core','openatl_atl_path')) {
    python tests/wtl10/verify_pe_machine.py "$build/$target.exe" $arch
    if ($LASTEXITCODE -ne 0) { throw "CMake COM target PE machine mismatch: $target/$arch" }
  }
  if($arch -eq 'aarch64') {
    # Carry the CMake-built repository-owned path test to the real ARM64
    # machine, preserving the same manifest/PE validation as the GUI.
    $out='build-wtl10/atl_path_arm64.exe'
    Copy-Item "$build/openatl_atl_path.exe" $out -Force
    $manifestPath='build-wtl10/manifest.json'
    $manifest=Get-Content $manifestPath -Raw | ConvertFrom-Json -AsHashtable
    $manifest['atl_path_arm64.exe']=(Get-FileHash $out -Algorithm SHA256).Hash.ToLowerInvariant()
    $manifest | ConvertTo-Json -Depth 4 | Set-Content $manifestPath -Encoding utf8
  }
  if($arch -eq 'x86_64') {
    & ctest --test-dir $build --output-on-failure --repeat until-fail:5
    if ($LASTEXITCODE -ne 0) { throw 'CMake native Windows x64 conformance failed' }
  }
}
Write-Host 'OpenATL UI no-COM + COM client + W^X fault admission built x64/ARM64; x64 CTest 10/10 x5 PASS'
