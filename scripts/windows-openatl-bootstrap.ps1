# Fresh dependency bootstrap: ATL headers are checked in under include/.
# Fetch only the pinned official WTL10 archive and WTL sample, never OpenATL.
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
function Run([string]$file,[string[]]$args_) {
  & $file @args_
  if ($LASTEXITCODE -ne 0) { throw "$file failed exit=$LASTEXITCODE" }
}
$demoPin='63ba6a992cb3d79d56be66cd283f6615c6646b3d'
$wtlGitPin='065ef5597383c1231bd4892cf8e94331b29bdb97'
$wtlGitBlob='a9f9d84495aac2155b0fdb00fec0e421b0e75c90'
$wtlSha='13de5cd7ed41454b8f287e9bcc4525234c6ac8c85905e4d4fe1a0776709fd78f'
if (!(Test-Path 'include/atlbase.h') -or !(Test-Path 'include/atlwin.h')) {
  throw 'Missing repository-owned OpenATL headers; no remote fallback'
}
if (Test-Path tools/wtl10-test) { throw 'Refuse reused WTL10 demo checkout' }
New-Item -ItemType Directory -Force 'tools/wtl10' | Out-Null
New-Item -ItemType Directory -Force 'build-wtl10' | Out-Null
# ATL implementation is checked into include/ with separate MIT license.
# No checkout mutation or test-time patching of our provider.
Run git @('clone','--quiet','https://github.com/kkhaike/wtltest.git','tools/wtl10-test')
Run git @('-C','tools/wtl10-test','checkout','--quiet','--detach',$demoPin)
if ((git -C tools/wtl10-test rev-parse HEAD).Trim() -ne $demoPin) {
  throw 'WTL10 demo immutable source mismatch'
}
$url="https://raw.githubusercontent.com/Win32-WTL/WTL/$wtlGitPin/Releases/WTL10_10320_Final.zip"
Invoke-WebRequest -Uri $url -OutFile 'build-wtl10/wtl10.zip'
$actualSha=(Get-FileHash 'build-wtl10/wtl10.zip' -Algorithm SHA256).Hash.ToLowerInvariant()
if ($actualSha -ne $wtlSha) { throw "WTL10 ZIP SHA256 mismatch: $actualSha" }
$actualBlob=(git hash-object 'build-wtl10/wtl10.zip').Trim()
if ($LASTEXITCODE -ne 0 -or $actualBlob -ne $wtlGitBlob) {
  throw "WTL10 pinned git blob mismatch: $actualBlob"
}
Expand-Archive 'build-wtl10/wtl10.zip' -DestinationPath tools/wtl10 -Force
$header=Get-ChildItem tools/wtl10 -File -Recurse -Filter atlapp.h |
  Where-Object { $_.Directory.Name -ieq 'Include' } | Select-Object -First 1
if (!$header) { throw 'WTL10 Include/atlapp.h missing' }
$version=Get-Content $header.FullName -TotalCount 130
if (!($version -match 'WTL version 10\.0') -or !($version -match '_WTL_VER')) {
  throw 'Refuse WTL version mismatch or legacy WTL8'
}
'{}' | Set-Content 'build-wtl10/manifest.json' -Encoding utf8
Write-Host 'Repository-owned MIT OpenATL headers; official WTL10 SHA256 pinned; no second ATL checkout'
