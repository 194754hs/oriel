# Pulls the candidate icon sets so the choice can be made by looking rather than
# by reading marketing pages. Pure ASCII.
#
# One tarball per set, not one request per icon: raw.githubusercontent is not
# reachable from here and unpkg answers single files far too slowly to fetch
# fifty of them.
$ErrorActionPreference = 'Continue'
$ProgressPreference = 'SilentlyContinue'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$OrielRoot = (Split-Path -Parent $PSScriptRoot)
$root = "$OrielRoot\design\icon-sets"
New-Item -ItemType Directory -Force $root | Out-Null

# name in the registry -> folder we unpack it into, and the licence it carries
$sets = @(
  @{ pkg='lucide-static';          dir='lucide';   lic='ISC' },
  @{ pkg='@tabler/icons';          dir='tabler';   lic='MIT' },
  @{ pkg='iconoir';                dir='iconoir';  lic='MIT' },
  @{ pkg='@phosphor-icons/core';   dir='phosphor'; lic='MIT' }
)

foreach ($s in $sets) {
  $dest = Join-Path $root $s.dir
  if (Test-Path (Join-Path $dest 'package')) { Write-Host "$($s.dir): already unpacked"; continue }
  New-Item -ItemType Directory -Force $dest | Out-Null
  $meta = 'https://registry.npmjs.org/' + ($s.pkg -replace '/','%2F') + '/latest'
  try {
    $j = (Invoke-WebRequest -Uri $meta -UseBasicParsing -TimeoutSec 120).Content | ConvertFrom-Json
  } catch { Write-Host "$($s.dir): metadata FAILED - $($_.Exception.Message)"; continue }
  $tgz = Join-Path $dest 'pkg.tgz'
  try {
    Invoke-WebRequest -Uri $j.dist.tarball -OutFile $tgz -UseBasicParsing -TimeoutSec 300
  } catch { Write-Host "$($s.dir): tarball FAILED - $($_.Exception.Message)"; continue }
  # tar.exe ships with Windows 10+ and handles .tgz directly
  & tar.exe -xzf $tgz -C $dest
  if ($LASTEXITCODE -ne 0) { Write-Host "$($s.dir): extract FAILED"; continue }
  Remove-Item $tgz -Force
  Write-Host ("{0}: v{1} ({2}) unpacked" -f $s.dir, $j.version, $s.lic)
}

Write-Host ""
Write-Host "--- icon counts ---"
foreach ($s in $sets) {
  $d = Join-Path $root $s.dir
  if (Test-Path $d) {
    $n = (Get-ChildItem $d -Recurse -Filter *.svg -ErrorAction SilentlyContinue).Count
    Write-Host ("  {0,-9} {1,6} svg  [{2}]" -f $s.dir, $n, $s.lic)
  }
}
