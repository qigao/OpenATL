param([string]$ArtifactRoot='msvc-arm64')
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
if([Runtime.InteropServices.RuntimeInformation]::OSArchitecture -ne
   [Runtime.InteropServices.Architecture]::Arm64) {
  throw 'Native MSVC ARM64 requires real Windows ARM64'
}
$manifest=Get-Content (Join-Path $ArtifactRoot 'manifest-msvc-arm64.json') -Raw |
  ConvertFrom-Json -AsHashtable
$files=@('openatl_gui_runtime.exe','openatl_com_client.exe','openatl_wtl10_sdi.exe')
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
}
function Run-Exact([string]$file,[string]$value) {
  $psi=New-Object System.Diagnostics.ProcessStartInfo
  $psi.FileName=(Resolve-Path (Join-Path $ArtifactRoot $file)).Path
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
Write-Host 'MSVC native ARM64 UI no-COM + COM client 5/5 PASS; full SDI built but not run'
