# Splices the subset OFL faces into the bench, producing the self-contained
# file that gets published. bench.html stays editable; bench.dist.html is generated.
#
# NOTE: keep this script pure ASCII. PowerShell 5.1 reads BOM-less .ps1 files as
# ANSI, so any non-ASCII literal here arrives mojibake'd and silently fails to match.
$ErrorActionPreference = 'Stop'
$OrielRoot = (Split-Path -Parent $PSScriptRoot)
$dir = "$OrielRoot\design"

$html  = Get-Content "$dir\bench.html" -Raw -Encoding UTF8
$fonts = Get-Content "$dir\fonts.css"  -Raw -Encoding UTF8

# match the whole marker comment line by its ASCII token only
$rx = [regex]'/\* @FONTS@[^\r\n]*?\*/'
if (-not $rx.IsMatch($html)) { throw "font marker not found in bench.html" }

$out = $rx.Replace($html, { param($m) $fonts }, 1)
Set-Content "$dir\bench.dist.html" $out -Encoding UTF8 -NoNewline

$kb    = [math]::Round((Get-Item "$dir\bench.dist.html").Length/1KB)
$faces = ([regex]::Matches($out, '@font-face')).Count
Write-Host "bench.dist.html: $kb KB, $faces faces embedded"
