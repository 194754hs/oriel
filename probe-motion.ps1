# Captures a burst of real frames around one interaction and lays them out as a
# contact sheet, so motion can be looked at rather than asserted. Pure ASCII.
#   probe-motion.ps1 <what>   what = column | pill | hover | switch | tab | menu
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Text; using System.Runtime.InteropServices;
public struct RCT { public int L,T,R,B; }
public struct PT { public int X,Y; }
public class W {
  [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr c);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RCT r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref PT p);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h,IntPtr a,int x,int y,int cx,int cy,uint f);
  [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h,uint m,IntPtr w,IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
  [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h,StringBuilder s,int n);
  [DllImport("user32.dll")] public static extern IntPtr SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
  [DllImport("user32.dll")] public static extern bool GetCursorPos(out PT p);
}
"@
[W]::SetProcessDpiAwarenessContext([IntPtr](-4)) | Out-Null   # PER_MONITOR_AWARE_V2

$what = if ($args.Count -ge 1) { $args[0] } else { 'column' }
$OrielRoot = $PSScriptRoot
$OrielArtifacts = Join-Path $OrielRoot 'artifacts'
New-Item -ItemType Directory -Force $OrielArtifacts | Out-Null
$rel  = "$OrielRoot\build-rel"
$sp   = "$OrielArtifacts"

Get-Process oriel -ErrorAction SilentlyContinue | ForEach-Object { $_.Kill() }
Start-Sleep -Milliseconds 500
Remove-Item "$rel\oriel.log" -ErrorAction SilentlyContinue

$p = Start-Process "$rel\oriel.exe" -ArgumentList "`"$env:USERPROFILE\Documents`"" -PassThru
$h = [IntPtr]::Zero
for ($i=0; $i -lt 120; $i++) {
  Start-Sleep -Milliseconds 50; $p.Refresh()
  if ($p.HasExited) { throw "exited: $($p.ExitCode)" }
  if ($p.MainWindowHandle -ne [IntPtr]::Zero -and [W]::IsWindowVisible($p.MainWindowHandle)) { $h=$p.MainWindowHandle; break }
}
if ($h -eq [IntPtr]::Zero) { $p.Kill(); throw "no window" }
$sb = New-Object System.Text.StringBuilder 64
[W]::GetClassNameW($h,$sb,64) | Out-Null
if ($sb.ToString() -ne 'OrielShell') { $p.Kill(); throw "wrong window: $($sb.ToString())" }

[W]::SetWindowPos($h,[IntPtr](-1),120,80,2300,1360,0x0040) | Out-Null   # SWP_SHOWWINDOW
[W]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 1800

$dpi = [W]::GetDpiForWindow($h); $S = $dpi / 96.0
$cr = New-Object RCT; [W]::GetClientRect($h,[ref]$cr) | Out-Null
$o = New-Object PT; [W]::ClientToScreen($h,[ref]$o) | Out-Null
$cw = $cr.R - $cr.L; $ch = $cr.B - $cr.T
Write-Host ("dpi={0} scale={1} client={2}x{3} origin=({4},{5})" -f $dpi,$S,$cw,$ch,$o.X,$o.Y)

function Grab {
  $bmp = New-Object System.Drawing.Bitmap($cw,$ch)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($o.X,$o.Y,0,0,(New-Object System.Drawing.Size($cw,$ch)))
  $g.Dispose(); return $bmp
}
function MkLp($xd,$yd){ $x=[int]($xd*$S); $y=[int]($yd*$S); return [IntPtr](($y -shl 16) -bor ($x -band 0xFFFF)) }
function Click($xd,$yd){
  [W]::SetCursorPos([int]($o.X + $xd*$S), [int]($o.Y + $yd*$S)) | Out-Null
  [W]::PostMessageW($h,0x0201,[IntPtr]1,(MkLp $xd $yd)) | Out-Null
  [W]::PostMessageW($h,0x0202,[IntPtr]0,(MkLp $xd $yd)) | Out-Null
}
# Hover has to be driven with the real pointer: TrackMouseEvent watches where
# the cursor actually is, so a posted WM_MOUSEMOVE is undone by the
# WM_MOUSELEAVE that follows it one message later.
function PtrTo($xd,$yd){
  [W]::SetCursorPos([int]($o.X + $xd*$S), [int]($o.Y + $yd*$S)) | Out-Null
}

# region of interest in DIPs, an optional setup, and the interaction to measure
$setup = { PtrTo 620 700 }     # park somewhere inert unless a scenario says otherwise
switch ($what) {
  'column' { $roi = @(396, 78, 620, 260); $pre = { Click 300 95 }; $step = 16; $n = 14 }
  'pill'   { $roi = @(  0, 78, 200, 560); $pre = { Click  90 300 }; $step = 20; $n = 12 }
  # leave row 0 for row 2: the row being left has to fade out, not cut
  'hover'  { $roi = @(198, 78, 410, 170); $setup = { PtrTo 300 95 }
             $pre = { PtrTo 300 147 };   $step = 14; $n = 10 }
  'switch' { $roi = @(1010, 38, 1130, 74); $pre = { Click 1105 56 }; $step = 20; $n = 12 }
  # sort / share / more had no hover at all before this pass
  'chrome' { $roi = @(720,  38, 1130, 74); $pre = { PtrTo 748 56 }; $step = 16; $n = 12 }
  'sidebtn'{ $roi = @(  0,  36, 200,  76); $pre = { PtrTo  52 56 }; $step = 16; $n = 12 }
  'newtab' { $roi = @(  0,   0, 260,  36); $pre = { PtrTo 194 19 }; $step = 16; $n = 12 }
  'tab'    { $roi = @(  0,   0, 700,  38); $pre = { Click 194 19 }; $step = 26; $n = 14 }
  'insp'   { $roi = @(760,  30,1140, 420); $pre = { Click 1001 56 }; $step = 34; $n = 14 }
  default  { throw "unknown scenario $what" }
}
$saveCur = New-Object PT; [W]::GetCursorPos([ref]$saveCur) | Out-Null
& $setup
Start-Sleep -Milliseconds 600

# Grab only the region under test: capturing the whole client costs ~30ms a
# frame, which is coarser than the motions being measured.
$x0=[int]($roi[0]*$S); $y0=[int]($roi[1]*$S); $x1=[int]($roi[2]*$S); $y1=[int]($roi[3]*$S)
$rw = $x1-$x0; $rh = $y1-$y0
function GrabRoi {
  $bmp = New-Object System.Drawing.Bitmap($rw,$rh)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen(($o.X+$x0),($o.Y+$y0),0,0,(New-Object System.Drawing.Size($rw,$rh)))
  $g.Dispose(); return $bmp
}

$sw = [System.Diagnostics.Stopwatch]::StartNew()
$base = GrabRoi
$t = @(0); $frames = @($base)
& $pre
for ($i=0; $i -lt $n; $i++) {
  $target = ($i+1) * $step
  while ($sw.Elapsed.TotalMilliseconds -lt $target) { }   # spin: Start-Sleep cannot do 20ms reliably
  $t += [int]$sw.Elapsed.TotalMilliseconds
  $frames += (GrabRoi)
}
$p.Kill()
[W]::SetCursorPos($saveCur.X, $saveCur.Y) | Out-Null   # put the pointer back

# contact sheet: the frames tiled in a grid, each with its timestamp
$cols = [math]::Ceiling([math]::Sqrt($frames.Count * $rh / [double]$rw))
if ($cols -lt 1) { $cols = 1 }
$rows = [math]::Ceiling($frames.Count / [double]$cols)
$pad = 8; $lab = 30
$sheet = New-Object System.Drawing.Bitmap([int](($rw+$pad)*$cols), [int](($rh+$lab+$pad)*$rows))
$gs = [System.Drawing.Graphics]::FromImage($sheet)
$gs.Clear([System.Drawing.Color]::FromArgb(20,20,22))
$fnt = New-Object System.Drawing.Font('Consolas',16,[System.Drawing.FontStyle]::Bold)
$br  = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(120,220,160))
for ($i=0; $i -lt $frames.Count; $i++) {
  $cx = ($i % $cols) * ($rw+$pad); $cy = [math]::Floor($i / $cols) * ($rh+$lab+$pad)
  $gs.DrawString(("+" + $t[$i] + "ms"), $fnt, $br, $cx, $cy)
  $gs.DrawImage($frames[$i], [int]$cx, [int]($cy+$lab), [int]$rw, [int]$rh)
}
$gs.Dispose()
$out = "$sp\sheet-$what.png"
$sheet.Save($out,[System.Drawing.Imaging.ImageFormat]::Png)

# and a per-frame numeric signature of the ROI, so a pop can be told from a slide
Write-Host ""
Write-Host ("=== {0}: mean luminance of ROI per frame ===" -f $what)
for ($i=0; $i -lt $frames.Count; $i++) {
  $sum=0.0; $cnt=0
  for ($x=0; $x -lt $rw; $x+=4) { for ($y=0; $y -lt $rh; $y+=4) {
    $c=$frames[$i].GetPixel($x,$y); $sum += (0.299*$c.R + 0.587*$c.G + 0.114*$c.B); $cnt++ } }
  Write-Host ("  t=+{0,4}ms  mean={1}" -f $t[$i], [math]::Round($sum/$cnt,3))
  $frames[$i].Dispose()
}
Write-Host ""
Write-Host "sheet: $out  ($($rw+$lab) x $($rh*$frames.Count))"
Write-Host "--- oriel.log ---"
Get-Content "$rel\oriel.log" -ErrorAction SilentlyContinue | Select-Object -Last 25
