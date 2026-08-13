# Builds a side-by-side comparison page from the real SVGs, plus crops of what
# Oriel draws today. Pure ASCII (the HTML it writes is UTF-8).
$ErrorActionPreference = 'Stop'
$OrielRoot = (Split-Path -Parent $PSScriptRoot)
$root = "$OrielRoot\design"
$sets = "$OrielRoot\design\icon-sets"
$cur  = "$OrielRoot\design\icon-candidates"
$dirs = @{
  lucide   = "$sets\lucide\package\icons"
  tabler   = "$sets\tabler\package\icons\outline"
  iconoir  = "$sets\iconoir\package\icons\regular"
  phosphor = "$sets\phosphor\package\assets\light"
}

# role, label, then candidate names per set (first that exists wins), then the
# basename of the crop of what Oriel draws today.
$roles = @(
 @{r='folder';  ja='フォルダ';   cur='folder';  lucide='folder';       tabler='folder';        iconoir='folder';         phosphor='folder-light'}
,@{r='doc';     ja='書類';       cur='doc';     lucide='file-text';    tabler='file-text';     iconoir='page';           phosphor='file-text-light'}
,@{r='image';   ja='画像';       cur='';        lucide='file-image';   tabler='photo';         iconoir='media-image';    phosphor='file-image-light'}
,@{r='audio';   ja='音声';       cur='';        lucide='file-audio';   tabler='file-music';    iconoir='music-double-note'; phosphor='file-audio-light'}
,@{r='archive'; ja='圧縮';       cur='';        lucide='file-archive'; tabler='file-zip';      iconoir='archive';        phosphor='file-archive-light'}
,@{r='code';    ja='コード';     cur='';        lucide='file-code';    tabler='file-code';     iconoir='code';           phosphor='file-code-light'}
,@{r='drive';   ja='ドライブ';   cur='drive';   lucide='hard-drive';   tabler='server';        iconoir='floppy-disk';    phosphor='hard-drive-light'}
,@{r='cloud';   ja='クラウド';   cur='cloud';   lucide='cloud';        tabler='cloud';         iconoir='cloud';          phosphor='cloud-light'}
,@{r='pc';      ja='この PC';    cur='pc';      lucide='monitor';      tabler='device-desktop';iconoir='computer';       phosphor='monitor-light'}
,@{r='clock';   ja='最近';       cur='clock';   lucide='clock';        tabler='clock';         iconoir='clock';          phosphor='clock-light'}
,@{r='desktop'; ja='デスクトップ';cur='desktop';lucide='monitor-dot';  tabler='device-imac';   iconoir='computer';       phosphor='desktop-light'}
,@{r='download';ja='ダウンロード';cur='download';lucide='download';    tabler='download';      iconoir='download';       phosphor='download-simple-light'}
,@{r='star';    ja='お気に入り'; cur='star';    lucide='star';         tabler='star';          iconoir='star';           phosphor='star-light'}
,@{r='search';  ja='検索';       cur='search';  lucide='search';       tabler='search';        iconoir='search';         phosphor='magnifying-glass-light'}
,@{r='gear';    ja='設定';       cur='gear';    lucide='settings';     tabler='settings';      iconoir='settings';       phosphor='gear-light'}
,@{r='share';   ja='共有';       cur='share';   lucide='share-2';      tabler='share';         iconoir='share-android';  phosphor='share-network-light'}
,@{r='sort';    ja='並び替え';   cur='sort';    lucide='arrow-up-down';tabler='arrows-sort';   iconoir='sort';           phosphor='sort-ascending-light'}
,@{r='more';    ja='その他';     cur='more';    lucide='ellipsis';     tabler='dots';          iconoir='more-horiz';     phosphor='dots-three-light'}
,@{r='colview'; ja='カラム表示'; cur='column';  lucide='columns-3';    tabler='layout-columns';iconoir='view-columns-3'; phosphor='columns-light'}
,@{r='listview';ja='リスト表示'; cur='list';    lucide='list';         tabler='list';          iconoir='list';           phosphor='list-light'}
,@{r='gridview';ja='アイコン表示';cur='grid';   lucide='grid-2x2';     tabler='layout-grid';   iconoir='view-grid';      phosphor='squares-four-light'}
,@{r='chevron'; ja='開く記号';   cur='';        lucide='chevron-right';tabler='chevron-right'; iconoir='nav-arrow-right';phosphor='caret-right-light'}
,@{r='plus';    ja='新規タブ';   cur='';        lucide='plus';         tabler='plus';          iconoir='plus';           phosphor='plus-light'}
,@{r='sidebar'; ja='サイドバー'; cur='';        lucide='panel-left';   tabler='layout-sidebar-left-collapse'; iconoir='sidebar-collapse'; phosphor='sidebar-simple-light'}
)

$missing = @()
function Inner($set, $name) {
  if (-not $name) { return $null }
  $f = Join-Path $dirs[$set] "$name.svg"
  if (-not (Test-Path $f)) { $script:missing += "$set/$name"; return $null }
  $s = [IO.File]::ReadAllText($f)
  $vb = if ($s -match 'viewBox="([^"]+)"') { $Matches[1] } else { '0 0 24 24' }
  # Tabler ships a transparent hit-box path first; our CSS would give it a
  # stroke and draw a square around every icon.
  $s = $s -replace '<path\s+stroke="none"[^>]*fill="none"\s*/>', ''
  $body = if ($s -match '(?s)<svg[^>]*>(.*)</svg>') { $Matches[1] } else { '' }
  return @{ vb=$vb; body=$body }
}
function DataUri($base) {
  if (-not $base) { return $null }
  $f = Join-Path $cur "current-$base.png"
  if (-not (Test-Path $f)) { return $null }
  return 'data:image/png;base64,' + [Convert]::ToBase64String([IO.File]::ReadAllBytes($f))
}

$sb = New-Object System.Text.StringBuilder
[void]$sb.AppendLine(@'
<title>Oriel アイコン候補の比較</title>
<style>
 :root{
   --bg:#faf9f7; --panel:#fff; --ink:#1c1c1e; --ink2:#6e6e73; --hair:rgba(0,0,0,.09);
   --accent:#2f6f4f; --warn:#a8442a;
 }
 @media (prefers-color-scheme:dark){
   :root{--bg:#141416;--panel:#1c1c1f;--ink:#f0f0f2;--ink2:#95959c;--hair:rgba(255,255,255,.1);
         --accent:#7fbf9a;--warn:#e0866a}
 }
 :root[data-theme="dark"]{--bg:#141416;--panel:#1c1c1f;--ink:#f0f0f2;--ink2:#95959c;
   --hair:rgba(255,255,255,.1);--accent:#7fbf9a;--warn:#e0866a}
 :root[data-theme="light"]{--bg:#faf9f7;--panel:#fff;--ink:#1c1c1e;--ink2:#6e6e73;
   --hair:rgba(0,0,0,.09);--accent:#2f6f4f;--warn:#a8442a}
 body{background:var(--bg);color:var(--ink);margin:0;padding:32px 28px 64px;
   font-family:"Segoe UI Variable Text","Segoe UI",system-ui,sans-serif;
   font-size:14px;line-height:1.5;-webkit-text-size-adjust:100%}
 h1{font-size:22px;letter-spacing:-.01em;margin:0 0 6px;font-weight:650}
 .sub{color:var(--ink2);font-size:13px;margin:0 0 28px;max-width:74ch}
 h2{font-size:15px;letter-spacing:.01em;margin:36px 0 12px;font-weight:650}
 .wrap{overflow-x:auto;border:1px solid var(--hair);border-radius:12px;background:var(--panel)}
 table{border-collapse:collapse;width:max-content;min-width:100%}
 th,td{padding:0;text-align:center;vertical-align:middle}
 th.rowh,td.rowh{position:sticky;left:0;background:var(--panel);text-align:left;
   padding:0 16px;white-space:nowrap;border-right:1px solid var(--hair);z-index:1}
 thead th{font-size:11px;letter-spacing:.03em;color:var(--ink2);font-weight:600;
   padding:12px 4px;border-bottom:1px solid var(--hair);white-space:nowrap}
 tbody tr+tr td{border-top:1px solid var(--hair)}
 td{height:72px;width:72px}
 .name{font-weight:600;font-size:13px}
 .lic{font-size:11px;color:var(--ink2);font-weight:400}
 svg.ic{width:44px;height:44px;display:block;margin:0 auto;color:var(--ink)}
 svg.ic.stroked *{fill:none;stroke:currentColor;stroke-width:1.65;
   stroke-linecap:round;stroke-linejoin:round;vector-effect:non-scaling-stroke}
 svg.ic.filled *{fill:currentColor;stroke:none}
 img.ic{width:44px;height:44px;display:block;margin:0 auto;image-rendering:auto}
 .gap{color:var(--warn);font-size:11px;font-weight:600}
 .note{margin-top:10px;color:var(--ink2);font-size:12.5px;max-width:82ch}
 .note b{color:var(--ink)}
 ul{margin:8px 0 0;padding-left:20px;color:var(--ink2);font-size:12.5px;max-width:82ch}
 li{margin:5px 0}
 li b{color:var(--ink)}
 .real{display:flex;gap:34px;flex-wrap:wrap;margin-top:6px}
 .realcard{border:1px solid var(--hair);border-radius:12px;background:var(--panel);
   padding:16px 20px 14px}
 .realrow{display:flex;align-items:center;gap:14px;height:34px}
 .realrow svg.ic,.realrow img.ic{width:22px;height:22px}
 .realcard h3{font-size:12px;letter-spacing:.03em;color:var(--ink2);margin:0 0 10px;
   font-weight:600;text-transform:none}
</style>
<h1>アイコン候補の比較</h1>
<p class="sub">実物です。各セットの SVG をダウンロードし、線幅を 1.65（24 グリッド換算）に統一して描いています。形だけを比べられるよう、太さの違いは意図的に消してあります。「現行」は動いている Oriel の画面から切り出した実際の描画です。</p>
<h2>拡大（形を見る）</h2>
<div class="wrap"><table><thead><tr><th class="rowh">セット</th>
'@)
foreach ($x in $roles) { [void]$sb.AppendLine("<th>$($x.ja)</th>") }
[void]$sb.AppendLine('</tr></thead><tbody>')

$rows = @(
  @{k='current'; n='現行（自作）'; l='—'},
  @{k='lucide';  n='Lucide';       l='ISC'},
  @{k='tabler';  n='Tabler';       l='MIT'},
  @{k='iconoir'; n='Iconoir';      l='MIT'},
  @{k='phosphor';n='Phosphor Light';l='MIT'}
)
foreach ($row in $rows) {
  [void]$sb.AppendLine("<tr><td class=""rowh""><span class=""name"">$($row.n)</span><br><span class=""lic"">$($row.l)</span></td>")
  foreach ($x in $roles) {
    if ($row.k -eq 'current') {
      $u = DataUri $x.cur
      if ($u) { [void]$sb.AppendLine("<td><img class=""ic"" src=""$u"" alt=""""></td>") }
      else    { [void]$sb.AppendLine('<td><span class="gap">—</span></td>') }
    } else {
      $ic = Inner $row.k $x[$row.k]
      if ($ic) {
        $cls = if ($row.k -eq 'phosphor') { 'filled' } else { 'stroked' }
        [void]$sb.AppendLine("<td><svg class=""ic $cls"" viewBox=""$($ic.vb)"">$($ic.body)</svg></td>")
      } else {
        [void]$sb.AppendLine('<td><span class="gap">なし</span></td>')
      }
    }
  }
  [void]$sb.AppendLine('</tr>')
}
[void]$sb.AppendLine('</tbody></table></div>')

# real size, on a mock sidebar, which is how these are actually met
[void]$sb.AppendLine('<h2>実寸（サイドバー 16px 相当）</h2><div class="real">')
$realRoles = $roles | Where-Object { $_.r -in @('clock','desktop','doc','download','star','pc','drive','cloud') }
foreach ($row in $rows) {
  [void]$sb.AppendLine("<div class=""realcard""><h3>$($row.n)</h3>")
  foreach ($x in $realRoles) {
    [void]$sb.Append('<div class="realrow">')
    if ($row.k -eq 'current') {
      $u = DataUri $x.cur
      if ($u) { [void]$sb.Append("<img class=""ic"" src=""$u"" alt="""">") } else { [void]$sb.Append('<span style="width:22px"></span>') }
    } else {
      $ic = Inner $row.k $x[$row.k]
      $cls = if ($row.k -eq 'phosphor') { 'filled' } else { 'stroked' }
      if ($ic) { [void]$sb.Append("<svg class=""ic $cls"" viewBox=""$($ic.vb)"">$($ic.body)</svg>") } else { [void]$sb.Append('<span style="width:22px"></span>') }
    }
    [void]$sb.AppendLine("<span>$($x.ja)</span></div>")
  }
  [void]$sb.AppendLine('</div>')
}
[void]$sb.AppendLine('</div>')

[void]$sb.AppendLine(@'
<h2>選ぶうえで効いた事実</h2>
<ul>
<li><b>Iconoir には file-* の系統がない。</b>収録されているのは <code>file-not-found</code> だけで、書類・画像・音声・圧縮・コードは <code>page</code> / <code>media-image</code> / <code>music-double-note</code> / <code>archive</code> / <code>code</code> と、別々の考えで描かれた記号を寄せ集めることになる。ファイル一覧の同じ列に並ぶ以上、これは統一感を直接損なう。</li>
<li><b>Iconoir の共有は <code>share-android</code> と <code>share-ios</code> しかない。</b>他社プラットフォーム名を冠した記号を Windows 製品に入れることになる。</li>
<li><b>Tabler に汎用のハードディスクがない。</b><code>brand-google-drive</code> と <code>brand-onedrive</code> はあるが、「ローカル (C:)」に使える中立な記号がなく、<code>server</code> か <code>database</code> で代用することになる。</li>
<li><b>Phosphor は塗りで形が決まっている。</b>線幅を後から詰められないので、Thin / Light / Regular のどれかを選び切る必要がある。他は 1.65 に合わせ込める。</li>
<li><b>Lucide は今日必要なものが全部あり、file-* が一系統でそろう。</b>ライセンスも挙げた中で最も緩い ISC。</li>
</ul>
<p class="note">なお <b>Untitled UI</b> は npm で自由に取得できないため、この比較に実物を並べられていません。独自ライセンスで、MIT / ISC のような取り消し不能の許諾ではなく条項が変わりうる点も、ここまで素性の明確な素材だけで組んできた本製品では引っかかります。見た目を優先して採る判断はありえますが、その場合は唯一の要注意依存になります。</p>
'@)

$outFile = Join-Path $root 'icon-compare.html'
[IO.File]::WriteAllText($outFile, $sb.ToString(), (New-Object Text.UTF8Encoding $false))
Write-Host "wrote $outFile"
if ($missing.Count) { Write-Host "NOT FOUND (cell shows なし):"; $missing | Sort-Object -Unique | ForEach-Object { Write-Host "  $_" } }
