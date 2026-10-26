param([string]$ArtifactRoot='wtl10-arm64')
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
if([Runtime.InteropServices.RuntimeInformation]::OSArchitecture -ne
   [Runtime.InteropServices.Architecture]::Arm64) {
  throw 'Real native Windows ARM64 runner is mandatory'
}
$manifest=Get-Content (Join-Path $ArtifactRoot 'manifest.json') -Raw |
  ConvertFrom-Json -AsHashtable
$expected=@{
  'wtl10_gui_runtime_aarch64.exe'='WTL10 GUI runtime PASS: no COM apartment, W^X thunk x64/ARM64, message maps, resource dialog, NCDESTROY, OnFinalMessage'
  'atl_com_client_arm64.exe'='ATL_COM_CLIENT_OK activate=1 qi=1 comptr=1 bstr=1 variant=1 unregister=0'
}
foreach($filename in $expected.Keys) {
  $path=(Resolve-Path (Join-Path $ArtifactRoot $filename)).Path
  $data=[IO.File]::ReadAllBytes($path)
  if($data.Length -lt 128 -or [Text.Encoding]::ASCII.GetString($data,0,2) -ne 'MZ') {
    throw "Invalid Windows PE $filename"
  }
  $offset=[BitConverter]::ToInt32($data,0x3c)
  if($offset -lt 64 -or $offset+8 -gt $data.Length -or
     [Text.Encoding]::ASCII.GetString($data,$offset,4) -ne "PE$([char]0)$([char]0)" -or
     [BitConverter]::ToUInt16($data,$offset+4) -ne 0xAA64) {
    throw "Expected ARM64 PE: $filename"
  }
  $sha=(Get-FileHash $path -Algorithm SHA256).Hash.ToLowerInvariant()
  if(!$manifest.ContainsKey($filename) -or $sha -ne $manifest[$filename]) {
    throw "ARM64 SHA256 mismatch: $filename"
  }
}
for($repeat=1;$repeat -le 5;$repeat++) {
  foreach($file in $expected.Keys) {
    $program=(Resolve-Path (Join-Path $ArtifactRoot $file)).Path
    $out=@(& $program 2>&1)
    $status=$LASTEXITCODE
    $actual=($out | ForEach-Object {"$_".Trim()}) -join ''
    if($status -ne 0 -or $actual -cne $expected[$file]) {
      throw "Native ARM64 $file repeat $repeat code=$($status): $actual"
    }
  }
}
Write-Host 'llvm-mingw native Windows ARM64 WTL UI no-COM + COM CLIENT 5/5 PASS'
