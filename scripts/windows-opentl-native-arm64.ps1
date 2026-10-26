param([string]$ArtifactRoot='wtl10-arm64')
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
if ([System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString() -ne 'Arm64') {
  throw 'Genuine Windows ARM64 runner required'
}
$manifest=Get-Content (Join-Path $ArtifactRoot 'manifest.json') -Raw | ConvertFrom-Json -AsHashtable
$files=@(
  'wtl10_gui_runtime_aarch64.exe',
  'atl_com_openatl_arm64.exe',
  'atl_com_p1_openatl_arm64.exe',
  'atl_com_p1b_openatl_arm64.exe',
  'atl_registry_sandbox_arm64.exe',
  'atl_p1c_registrar_arm64.exe'
)
foreach($filename in $files) {
  $path=(Resolve-Path (Join-Path $ArtifactRoot $filename)).Path
  $data=[IO.File]::ReadAllBytes($path)
  if ($data.Length -lt 128 -or [Text.Encoding]::ASCII.GetString($data,0,2) -ne 'MZ') {
    throw "Invalid PE image $filename"
  }
  $offset=[BitConverter]::ToInt32($data,0x3c)
  if ($offset -lt 64 -or $offset+8 -gt $data.Length -or
      [Text.Encoding]::ASCII.GetString($data,$offset,4) -ne "PE$([char]0)$([char]0)" -or
      [BitConverter]::ToUInt16($data,$offset+4) -ne 0xAA64) {
    throw "Not Windows ARM64 PE: $filename"
  }
  $sha=(Get-FileHash $path -Algorithm SHA256).Hash.ToLowerInvariant()
  if (!$manifest.ContainsKey($filename) -or $sha -ne $manifest[$filename]) {
    throw "ARM64 PE SHA mismatch/missing provenance: $filename"
  }
}
$expect=@{
  'atl_com_openatl_arm64.exe'='ATL_COM_P0_OK identity=1 map=1 qi_errors=1 refs=0 comptr=1 bstr=1 variant=1'
  'atl_com_p1_openatl_arm64.exe'='ATL_COM_P1_OK create=1 rollback=1 factory=1 nonagg=1 locks=1 running=1 revoke=1 registry_failclosed=1'
  'atl_com_p1b_openatl_arm64.exe'='ATL_COM_P1B_OK map=2 rollback=1 activation=2 revoke=1 wrongthread=1 registry=0'
  'atl_registry_sandbox_arm64.exe'='ATL_REG_SANDBOX_OK isolated=1 redirect=1 rollback=1 restored=1 clean=1 stub_failclosed=1'
  'atl_p1c_registrar_arm64.exe'='ATL_P1C_REAL_OK guarded=1 writes=2 rollback=1 duplicate=1 targeted=1 clean=1 recovered=1'
}
for($iteration=1;$iteration -le 5;$iteration++) {
  $gui=(Resolve-Path (Join-Path $ArtifactRoot 'wtl10_gui_runtime_aarch64.exe')).Path
  & $gui
  if($LASTEXITCODE -ne 0) { throw "ARM64 native GUI runtime failed repetition $iteration" }
  foreach($name in $expect.Keys) {
    $program=(Resolve-Path (Join-Path $ArtifactRoot $name)).Path
    $out=@(& $program 2>&1)
    $rc=$LASTEXITCODE
    $actual=($out | ForEach-Object { "$_".Trim() }) -join ''
    if($rc -ne 0 -or $actual -cne $expect[$name]) {
      throw "ARM64 native $name failed repeat $iteration exit=$($rc): $actual"
    }
  }
  foreach($mode in @('invalid','duplicate','capacity')) {
    $p1b=(Resolve-Path (Join-Path $ArtifactRoot 'atl_com_p1b_openatl_arm64.exe')).Path
    $out=@(& $p1b $mode 2>&1)
    $actual=($out | ForEach-Object { "$_".Trim() }) -join ''
    if($LASTEXITCODE -ne 0 -or $actual -cne "ATL_COM_P1B_NEG_OK mode=$mode") {
      throw "ARM64 P1-B fail-closed $mode failed: $actual"
    }
  }
}
Write-Host 'OpenTL true Windows ARM64 GUI/COM P0-P1C native run 5/5 PASS'
