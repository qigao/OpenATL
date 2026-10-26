# Fresh per-CI-job dependency bootstrap; no vendored ATL/WTL headers.
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
function Run([string]$file,[string[]]$args_) {
  & $file @args_
  if ($LASTEXITCODE -ne 0) { throw "$file failed exit=$LASTEXITCODE" }
}
$openPin='7b587f3e10f3f8d93a01522033de80007368bad5'
$demoPin='63ba6a992cb3d79d56be66cd283f6615c6646b3d'
$wtlGitPin='065ef5597383c1231bd4892cf8e94331b29bdb97'
$wtlGitBlob='a9f9d84495aac2155b0fdb00fec0e421b0e75c90'
$wtlSha='13de5cd7ed41454b8f287e9bcc4525234c6ac8c85905e4d4fe1a0776709fd78f'
if (Test-Path tools/wtl10-openatl) { throw 'Refuse reused OpenATL test checkout' }
if (Test-Path tools/wtl10-test) { throw 'Refuse reused WTL10 demo checkout' }
New-Item -ItemType Directory -Force 'tools/wtl10' | Out-Null
New-Item -ItemType Directory -Force 'build-wtl10' | Out-Null
Run git @('clone','--quiet','https://github.com/kkhaike/openatl.git','tools/wtl10-openatl')
Run git @('-C','tools/wtl10-openatl','checkout','--quiet','--detach',$openPin)
if ((git -C tools/wtl10-openatl rev-parse HEAD).Trim() -ne $openPin) {
  throw 'OpenATL immutable source mismatch'
}
# Patches are for conformance only; not an installable OpenTL ATL distribution.
foreach ($patch in @(
    'scripts/openatl-wx-hardening.py',
    'scripts/openatl-com-p1-hardening.py',
    'scripts/openatl-com-p1b-map.py',
    'scripts/openatl-com-p1c-registrar.py'
)) {
  Run python @($patch,'tools/wtl10-openatl')
}
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
Write-Host 'OpenTL external WTL10/OpenATL sources exact-pinned; acceptance-only fixes applied'
