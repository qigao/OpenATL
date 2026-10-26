param([string]$ArtifactRoot='msvc-arm64')
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest

if([System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture -ne
   [System.Runtime.InteropServices.Architecture]::Arm64) {
  throw 'Native MSVC ARM64 tests MUST run on genuine Windows ARM64'
}
$manifestFile=(Resolve-Path (Join-Path $ArtifactRoot 'manifest-msvc-arm64.json')).Path
$manifest=Get-Content $manifestFile -Raw | ConvertFrom-Json -AsHashtable
$files=@('openatl_gui_runtime.exe','openatl_com_p0.exe','openatl_com_p1.exe',
  'openatl_com_p1b.exe','openatl_registry_sandbox.exe','openatl_registrar.exe',
  'openatl_wtl10_sdi.exe')
foreach($file in $files) {
  $path=(Resolve-Path (Join-Path $ArtifactRoot $file)).Path
  $data=[IO.File]::ReadAllBytes($path)
  if($data.Length -lt 128 -or [Text.Encoding]::ASCII.GetString($data,0,2) -ne 'MZ') {
    throw "Invalid MSVC ARM64 MZ file $file"
  }
  $offset=[BitConverter]::ToInt32($data,0x3c)
  if($offset -lt 64 -or $offset+8 -gt $data.Length -or
     [Text.Encoding]::ASCII.GetString($data,$offset,4) -ne "PE$([char]0)$([char]0)" -or
     [BitConverter]::ToUInt16($data,$offset+4) -ne 0xAA64) {
    throw "MSVC binary is not ARM64 PE: $file"
  }
  $sha=(Get-FileHash $path -Algorithm SHA256).Hash.ToLowerInvariant()
  if(!$manifest.ContainsKey($file) -or $manifest[$file] -ne $sha) {
    throw "MSVC ARM64 binary SHA256/provenance mismatch: $file"
  }
}
function Run-Exact([string]$name,[string]$args_,[string]$expected) {
  $start=New-Object System.Diagnostics.ProcessStartInfo
  $start.FileName=(Resolve-Path (Join-Path $ArtifactRoot "$name.exe")).Path
  $start.Arguments=$args_
  $start.UseShellExecute=$false
  $start.CreateNoWindow=$true
  $start.RedirectStandardOutput=$true
  $start.RedirectStandardError=$true
  $p=New-Object System.Diagnostics.Process
  $p.StartInfo=$start
  if(!$p.Start()) {throw "Cannot run MSVC ARM64 $name"}
  if(!$p.WaitForExit(15000)) {
    $p.Kill($true)
    $p.WaitForExit()
    throw "MSVC ARM64 $name $args_ timed out"
  }
  $out=$p.StandardOutput.ReadToEnd().Trim()
  $err=$p.StandardError.ReadToEnd().Trim()
  if($p.ExitCode -ne 0 -or $err -or $out -cne $expected) {
    throw "MSVC ARM64 $name $args_ mismatch exit=$($p.ExitCode) expected=$expected stdout=$out stderr=$err"
  }
}
$expected=@{
  'openatl_gui_runtime'='WTL10 GUI runtime PASS: W^X thunk x64/ARM64, true message maps, resource dialog, NCDESTROY, OnFinalMessage'
  'openatl_com_p0'='ATL_COM_P0_OK identity=1 map=1 qi_errors=1 refs=0 comptr=1 bstr=1 variant=1'
  'openatl_com_p1'='ATL_COM_P1_OK create=1 rollback=1 factory=1 nonagg=1 locks=1 running=1 revoke=1 registry_failclosed=1'
  'openatl_com_p1b'='ATL_COM_P1B_OK map=2 rollback=1 activation=2 revoke=1 wrongthread=1 registry=0'
  'openatl_registry_sandbox'='ATL_REG_SANDBOX_OK isolated=1 redirect=1 rollback=1 restored=1 clean=1 stub_failclosed=1'
  'openatl_registrar'='ATL_P1C_REAL_OK guarded=1 writes=2 rollback=1 duplicate=1 targeted=1 clean=1 recovered=1'
}
for($i=1;$i -le 5;++$i) {
  foreach($name in $expected.Keys) { Run-Exact $name '' $expected[$name] }
  foreach($mode in @('invalid','duplicate','capacity')) {
    Run-Exact 'openatl_com_p1b' $mode "ATL_COM_P1B_NEG_OK mode=$mode"
  }
}
Write-Host 'MSVC native Windows ARM64 OpenATL/WTL10 GUI/COM P0-P1C 5/5 PASS; full SDI compiled and linked only'
