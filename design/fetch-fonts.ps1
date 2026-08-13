# Subsets and embeds the OFL candidate faces for the typography bench.
# Google Fonts' css2 `text=` parameter returns a font containing only the
# glyphs we ask for, which keeps the self-contained artifact small enough.
$ErrorActionPreference = 'Stop'
$ProgressPreference    = 'SilentlyContinue'
$ua = 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 Chrome/126 Safari/537.36'
$OrielRoot = (Split-Path -Parent $PSScriptRoot)
$dir = "$OrielRoot\design"

# ── the glyph set: everything the bench renders, plus full kana so the
#    user can judge Japanese text that is not already in the mock ──────
$chars = [System.Collections.Generic.HashSet[char]]::new()
foreach ($c in [char[]](Get-Content "$dir\bench.html" -Raw -Encoding UTF8)) { [void]$chars.Add($c) }
# printable ASCII
0x20..0x7E | ForEach-Object { [void]$chars.Add([char]$_) }
# hiragana + katakana + prolonged sound mark + common punctuation
0x3041..0x30FF | ForEach-Object { [void]$chars.Add([char]$_) }
0xFF01..0xFF5E | ForEach-Object { [void]$chars.Add([char]$_) }   # fullwidth
foreach ($c in [char[]]'‹›·—–…「」『』【】〜・、。％＆℃±×÷≠≦≧→←↑↓') { [void]$chars.Add($c) }
# a few extra kanji so language switching has something to show
foreach ($c in [char[]]'設定言語日本語英語中文한국어表示形式並替共有検索新規作成削除名前変更複製情報開閉戻進上下左右') { [void]$chars.Add($c) }

$text = -join ($chars | Where-Object { $_ -ne "`r" -and $_ -ne "`n" -and $_ -ne "`t" } | Sort-Object)
$enc  = [uri]::EscapeDataString($text)
Write-Host ("glyphs: {0}  url-len: {1}" -f $chars.Count, $enc.Length)

$families = @(
  @{ css='Inter';                key='inter';    w='400;500;600;700' },
  @{ css='Noto Sans JP';         key='notojp';   w='400;500;600;700' },
  @{ css='IBM Plex Sans';        key='plex';     w='400;500;600;700' },
  @{ css='IBM Plex Sans JP';     key='plexjp';   w='400;500;600;700' },
  @{ css='Manrope';              key='manrope';  w='400;500;600;700' },
  @{ css='Zen Kaku Gothic New';  key='zenkaku';  w='400;500;700;900' }
)

$out = New-Object System.Text.StringBuilder
[void]$out.AppendLine("/* Candidate UI faces, subset to the glyphs this bench uses.")
[void]$out.AppendLine("   All SIL Open Font License 1.1 — see ATTRIBUTION.md. */")

foreach ($f in $families) {
  $fam = $f.css -replace ' ', '+'
  $url = "https://fonts.googleapis.com/css2?family=$fam" + ":wght@" + $f.w + "&text=$enc"
  try {
    $css = (Invoke-WebRequest -Uri $url -UserAgent $ua -UseBasicParsing -TimeoutSec 60).Content
  } catch {
    Write-Host ("  SKIP {0}: {1}" -f $f.css, $_.Exception.Message); continue
  }
  # With `text=`, Google serves /l/font?kit=... (no .woff2 suffix) and every
  # requested weight points at the SAME variable file. Group by src so we emit
  # one @font-face carrying the whole weight range instead of N duplicates.
  $blocks = [regex]::Matches($css, '(?s)@font-face\s*\{(.*?)\}')
  $bySrc = @{}
  foreach ($b in $blocks) {
    $body = $b.Groups[1].Value
    $wm = [regex]::Match($body, 'font-weight:\s*(\d+)')
    $um = [regex]::Match($body, 'src:\s*url\((https://[^)]+)\)')
    if (-not $wm.Success -or -not $um.Success) { continue }
    $u = $um.Groups[1].Value
    if (-not $bySrc.ContainsKey($u)) { $bySrc[$u] = New-Object System.Collections.ArrayList }
    [void]$bySrc[$u].Add([int]$wm.Groups[1].Value)
  }
  $n = 0; $kb = 0
  foreach ($u in $bySrc.Keys) {
    $ws = $bySrc[$u] | Sort-Object
    $range = if ($ws.Count -gt 1) { "$($ws[0]) $($ws[-1])" } else { "$($ws[0])" }
    $bytes = (Invoke-WebRequest -Uri $u -UserAgent $ua -UseBasicParsing -TimeoutSec 60).Content
    $kb += $bytes.Length
    $b64 = [Convert]::ToBase64String($bytes)
    [void]$out.AppendLine("@font-face{font-family:'$($f.key)';font-style:normal;font-weight:$range;font-display:block;src:url(data:font/woff2;base64,$b64) format('woff2')}")
    $n++
  }
  Write-Host ("  {0,-20} {1} face(s)  weights {2}  {3} KB raw" -f $f.css, $n, $f.w, [math]::Round($kb/1024))
}

Set-Content "$dir\fonts.css" $out.ToString() -Encoding UTF8 -NoNewline
Write-Host ("`nfonts.css: {0} KB" -f [math]::Round((Get-Item "$dir\fonts.css").Length/1024))
