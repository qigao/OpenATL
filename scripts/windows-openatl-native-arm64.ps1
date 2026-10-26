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
  'atl_core_arm64.exe'='ATL_CORE_OK file=1 string=1 sync=1 no_com=1'
  'wtl10_thunk_fault_aarch64.exe'='THUNK_FAULT_OK alloc=1 rx=1 flush=1 reinit_rw=1 free=1 recovery=1 no_rwx=1 window_denied=1 dialog_denied=1'
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
$clientModes=@{
  '--sta'='ATL_COM_CLIENT_MODE_OK mode=sta apartment=1 activation=1 balance=1 changed=0'
  '--mta'='ATL_COM_CLIENT_MODE_OK mode=mta apartment=1 activation=1 balance=1 changed=0'
  '--changed-sta'='ATL_COM_CLIENT_MODE_OK mode=changed-sta apartment=1 activation=1 balance=1 changed=1'
  '--changed-mta'='ATL_COM_CLIENT_MODE_OK mode=changed-mta apartment=1 activation=1 balance=1 changed=1'
  '--uninitialized'='ATL_COM_CLIENT_MODE_OK mode=uninitialized apartment=0 activation_denied=1'
}
$client=(Resolve-Path (Join-Path $ArtifactRoot 'atl_com_client_arm64.exe')).Path
for($i=1;$i -le 5;++$i) {
  foreach($mode in $clientModes.Keys) {
    $out=@(& $client $mode 2>&1)
    $status=$LASTEXITCODE
    $actual=($out|ForEach-Object {"$_".Trim()})-join ''
    if($status -ne 0 -or $actual -cne $clientModes[$mode]) {
      throw "llvm-mingw ARM64 COM client $mode failed repeat $i exit=$($status): $actual"
    }
  }
}
Write-Host 'llvm-mingw native ARM64 UI/COM client and owned ATL file/string/sync 5/5 PASS'
