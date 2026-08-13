# Proves motion rather than asserting it: fires an interaction, grabs a burst of
# frames, and reports how much each differs from the one before. A still frame
# can never show whether an animation ran. Pure ASCII.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public struct RC { public int L,T,R,B; }
public class A {
  [DllImport("dwmapi.dll")] public static extern int DwmGetWindowAttribute(IntPtr h,int a,out RC r,int s);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h,IntPtr a,int x,int y,int cx,int cy,uint f);
  [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h,uint m,IntPtr w,IntPtr l);
  [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h,StringBuilder s,int n);
}
"@

$OrielRoot = $PSScriptRoot
$OrielArtifacts = Join-Path $OrielRoot 'artifacts'
New-Item -ItemType Directory -Force $OrielArtifacts | Out-Null
$root = "$OrielRoot"
$rel  = "$root\build-rel"
$sp   = "$OrielArtifacts"

Get-Process oriel -ErrorAction SilentlyContinue | ForEach-Object { $_.Kill() }
Start-Sleep -Milliseconds 400

$p = Start-Process "$rel\oriel.exe" -ArgumentList "`"$env:USERPROFILE\Documents`"" -PassThru
$h = [IntPtr]::Zero
for ($i=0; $i -lt 120; $i++) {
  Start-Sleep -Milliseconds 50; $p.Refresh()
  if ($p.HasExited) { throw "exited: $($p.ExitCode)" }
  if ($p.MainWindowHandle -ne [IntPtr]::Zero -and [A]::IsWindowVisible($p.MainWindowHandle)) { $h=$p.MainWindowHandle; break }
}
if ($h -eq [IntPtr]::Zero) { $p.Kill(); throw "no window" }
$sb = New-Object System.Text.StringBuilder 64
[A]::GetClassNameW($h,$sb,64) | Out-Null
if ($sb.ToString() -ne 'OrielShell') { $p.Kill(); throw "wrong window" }
[A]::SetWindowPos($h,[IntPtr](-1),0,0,0,0,0x0043) | Out-Null
Start-Sleep -Milliseconds 1500

$r = New-Object RC
[A]::DwmGetWindowAttribute($h,9,[ref]$r,16) | Out-Null
$w = $r.R-$r.L; $ht = $r.B-$r.T

function Grab($name) {
  $bmp = New-Object System.Drawing.Bitmap($w,$ht)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($r.L,$r.T,0,0,(New-Object System.Drawing.Size($w,$ht)))
  $g.Dispose()
  if ($name) { $bmp.Save("$sp\$name",[System.Drawing.Imaging.ImageFormat]::Png) }
  return $bmp
}

# Compares only the content region, sampled on a grid - enough to detect motion
# without the cost of a full per-pixel diff.
function DiffPct($a,$b) {
  $n=0; $d=0
  for ($x=460; $x -lt [Math]::Min($w,2200); $x+=7) {
    for ($y=120; $y -lt [Math]::Min($ht,1300); $y+=7) {
      $n++
      $pa=$a.GetPixel($x,$y); $pb=$b.GetPixel($x,$y)
      if ([Math]::Abs($pa.R-$pb.R)+[Math]::Abs($pa.G-$pb.G)+[Math]::Abs($pa.B-$pb.B) -gt 12) { $d++ }
    }
  }
  if ($n -eq 0) { return 0 }
  return [math]::Round(100.0*$d/$n,2)
}

function MakeLParam($x,$y){ return [IntPtr](($y -shl 16) -bor ($x -band 0xFFFF)) }

Write-Host "=== column arrival (design: 260ms, staggered 45ms) ==="
$base = Grab 'a0.png'
# click a folder in the first column to push a new one in
[A]::PostMessageW($h,0x0201,[IntPtr]1,(MakeLParam 298 95)) | Out-Null
$frames=@()
for ($i=0; $i -lt 6; $i++) { Start-Sleep -Milliseconds 55; $frames += (Grab "a$($i+1).png") }
$prev=$base
for ($i=0; $i -lt $frames.Count; $i++) {
  Write-Host ("  t=+{0,3}ms  change vs previous frame: {1}%" -f (($i+1)*55), (DiffPct $prev $frames[$i]))
  $prev.Dispose() | Out-Null
  $prev=$frames[$i]
}
$prev.Dispose()

Start-Sleep -Milliseconds 600
Write-Host ""
Write-Host "=== sidebar pill (design: 180ms) ==="
$b0 = Grab
[A]::PostMessageW($h,0x0201,[IntPtr]1,(MakeLParam 90 320)) | Out-Null   # a different sidebar row
$sf=@()
for ($i=0; $i -lt 4; $i++) { Start-Sleep -Milliseconds 50; $sf += (Grab) }
# sample the pill band on the sidebar only
function SideDiff($a,$b) {
  $n=0;$d=0
  for ($x=20; $x -lt 380; $x+=5) { for ($y=140; $y -lt 900; $y+=5) {
    $n++; $pa=$a.GetPixel($x,$y); $pb=$b.GetPixel($x,$y)
    if ([Math]::Abs($pa.R-$pb.R)+[Math]::Abs($pa.G-$pb.G)+[Math]::Abs($pa.B-$pb.B) -gt 12) { $d++ } } }
  return [math]::Round(100.0*$d/$n,2)
}
$prev=$b0
for ($i=0; $i -lt $sf.Count; $i++) {
  Write-Host ("  t=+{0,3}ms  sidebar change: {1}%" -f (($i+1)*50), (SideDiff $prev $sf[$i]))
  $prev.Dispose(); $prev=$sf[$i]
}
$prev.Dispose()

Start-Sleep -Milliseconds 500
$final = Grab 'anim-final.png'
$final.Dispose()
$p.Kill()
Write-Host ""
Write-Host "frames written to $sp"
