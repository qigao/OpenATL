param([string]$ArtifactRoot='msvc-arm64')
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
if([Runtime.InteropServices.RuntimeInformation]::OSArchitecture -ne
   [Runtime.InteropServices.Architecture]::Arm64) {
  throw 'Native MSVC ARM64 requires real Windows ARM64'
}
$manifest=Get-Content (Join-Path $ArtifactRoot 'manifest-msvc-arm64.json') -Raw |
  ConvertFrom-Json -AsHashtable
$files=@('openatl_gui_runtime.exe','openatl_com_client.exe',
  'openatl_thunk_fault.exe','openatl_atl_core.exe','openatl_wtl10_sdi.exe')
foreach($file in $files) {
  $path=(Resolve-Path (Join-Path $ArtifactRoot $file)).Path
  $data=[IO.File]::ReadAllBytes($path)
  if($data.Length -lt 128 -or [Text.Encoding]::ASCII.GetString($data,0,2) -ne 'MZ') {
    throw "Invalid MSVC ARM64 PE $file"
  }
  $offset=[BitConverter]::ToInt32($data,0x3c)
  if($offset -lt 64 -or $offset+8 -gt $data.Length -or
     [Text.Encoding]::ASCII.GetString($data,$offset,4) -ne "PE$([char]0)$([char]0)" -or
     [BitConverter]::ToUInt16($data,$offset+4) -ne 0xAA64) {
    throw "MSVC binary is not ARM64: $file"
  }
  $sha=(Get-FileHash $path -Algorithm SHA256).Hash.ToLowerInvariant()
  if(!$manifest.ContainsKey($file) -or $manifest[$file] -ne $sha) {
    throw "MSVC ARM64 SHA256 mismatch: $file"
  }
}
$expected=@{
  'openatl_gui_runtime.exe'='WTL10 GUI runtime PASS: no COM apartment, W^X thunk x64/ARM64, message maps, resource dialog, NCDESTROY, OnFinalMessage'
  'openatl_com_client.exe'='ATL_COM_CLIENT_OK activate=1 qi=1 comptr=1 bstr=1 variant=1 unregister=0'
  'openatl_atl_core.exe'='ATL_CORE_OK file=1 string=1 sync=1 no_com=1'
  'openatl_thunk_fault.exe'='THUNK_FAULT_OK alloc=1 rx=1 flush=1 reinit_rw=1 free=1 recovery=1 no_rwx=1 window_denied=1 dialog_denied=1'
}
function Run-Exact([string]$file,[string]$value,[string]$arguments='') {
  $psi=New-Object System.Diagnostics.ProcessStartInfo
  $psi.FileName=(Resolve-Path (Join-Path $ArtifactRoot $file)).Path
  $psi.Arguments=$arguments
  $psi.UseShellExecute=$false
  $psi.CreateNoWindow=$true
  $psi.RedirectStandardOutput=$true
  $psi.RedirectStandardError=$true
  $proc=New-Object System.Diagnostics.Process
  $proc.StartInfo=$psi
  if(!$proc.Start()) {throw "Cannot start MSVC ARM64 $file"}
  if(!$proc.WaitForExit(15000)) {
    $proc.Kill($true)
    $proc.WaitForExit()
    throw "MSVC ARM64 $file timeout"
  }
  $actual=$proc.StandardOutput.ReadToEnd().Trim()
  $errorText=$proc.StandardError.ReadToEnd().Trim()
  if($proc.ExitCode -ne 0 -or $errorText -or $actual -cne $value) {
    throw "MSVC ARM64 $file code=$($proc.ExitCode) expected=$value actual=$actual stderr=$errorText"
  }
}
for($i=1;$i -le 5;$i++) {
  foreach($key in $expected.Keys) {Run-Exact $key $expected[$key]}
}
$clientModes=@{
  '--sta'='ATL_COM_CLIENT_MODE_OK mode=sta apartment=1 activation=1 balance=1 changed=0'
  '--mta'='ATL_COM_CLIENT_MODE_OK mode=mta apartment=1 activation=1 balance=1 changed=0'
  '--changed-sta'='ATL_COM_CLIENT_MODE_OK mode=changed-sta apartment=1 activation=1 balance=1 changed=1'
  '--changed-mta'='ATL_COM_CLIENT_MODE_OK mode=changed-mta apartment=1 activation=1 balance=1 changed=1'
  '--uninitialized'='ATL_COM_CLIENT_MODE_OK mode=uninitialized apartment=0 activation_denied=1'
}
for($i=1;$i -le 5;++$i) {
  foreach($mode in $clientModes.Keys) {
    Run-Exact 'openatl_com_client.exe' $clientModes[$mode] $mode
  }
}
Write-Host 'MSVC native ARM64 UI/COM client and owned ATL core utilities 5/5 PASS; SDI link only'
