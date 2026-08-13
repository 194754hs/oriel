# Measures the two remaining motions from the sheet: row hover (60ms) and the
# view switcher indicator (220ms). Pure ASCII.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Text; using System.Runtime.InteropServices;
public struct RC2 { public int L,T,R,B; }
public class B2 {
  [DllImport("dwmapi.dll")] public static extern int DwmGetWindowAttribute(IntPtr h,int a,out RC2 r,int s);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h,IntPtr a,int x,int y,int cx,int cy,uint f);
  [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h,uint m,IntPtr w,IntPtr l);
  [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h,StringBuilder s,int n);
}
"@
$OrielRoot = $PSScriptRoot
$rel="$OrielRoot\build-rel"
Get-Process oriel -ErrorAction SilentlyContinue | ForEach-Object { $_.Kill() }
Start-Sleep -Milliseconds 400
$p=Start-Process "$rel\oriel.exe" -ArgumentList "`"$env:USERPROFILE\Documents`"" -PassThru
$h=[IntPtr]::Zero
for($i=0;$i -lt 120;$i++){ Start-Sleep -Milliseconds 50; $p.Refresh()
  if($p.MainWindowHandle -ne [IntPtr]::Zero -and [B2]::IsWindowVisible($p.MainWindowHandle)){$h=$p.MainWindowHandle;break} }
$sb=New-Object System.Text.StringBuilder 64; [B2]::GetClassNameW($h,$sb,64)|Out-Null
if($sb.ToString() -ne 'OrielShell'){ $p.Kill(); throw "wrong window" }
[B2]::SetWindowPos($h,[IntPtr](-1),0,0,0,0,0x0043)|Out-Null
Start-Sleep -Milliseconds 1500
$r=New-Object RC2; [B2]::DwmGetWindowAttribute($h,9,[ref]$r,16)|Out-Null
$w=$r.R-$r.L; $ht=$r.B-$r.T
function Grab{ $bmp=New-Object System.Drawing.Bitmap($w,$ht)
  $g=[System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($r.L,$r.T,0,0,(New-Object System.Drawing.Size($w,$ht))); $g.Dispose(); return $bmp }
function PctChange($a,$b,$x0,$x1,$y0,$y1){ $n=0;$d=0
  for($x=$x0;$x -lt $x1;$x+=3){ for($y=$y0;$y -lt $y1;$y+=3){ $n++
    $pa=$a.GetPixel($x,$y);$pb=$b.GetPixel($x,$y)
    if([Math]::Abs($pa.R-$pb.R)+[Math]::Abs($pa.G-$pb.G)+[Math]::Abs($pa.B-$pb.B) -gt 10){$d++} } }
  if($n -eq 0){return 0}; return [math]::Round(100.0*$d/$n,2) }
# NB: not "LP" - that is an alias for Out-Printer.
function Pt($x,$y){ return [IntPtr](($y -shl 16) -bor ($x -band 0xFFFF)) }

Write-Host "=== row hover (design: 60ms) ==="
[B2]::PostMessageW($h,0x0200,[IntPtr]0,(Pt 700 900))|Out-Null   # park away from rows
Start-Sleep -Milliseconds 400
$b0=Grab
[B2]::PostMessageW($h,0x0200,[IntPtr]0,(Pt 298 147))|Out-Null   # onto a row
$f=@(); for($i=0;$i -lt 4;$i++){ Start-Sleep -Milliseconds 25; $f+=(Grab) }
$prev=$b0
for($i=0;$i -lt $f.Count;$i++){
  Write-Host ("  t=+{0,3}ms  row band change: {1}%" -f (($i+1)*25),(PctChange $prev $f[$i] 420 1300 270 300))
  $prev.Dispose(); $prev=$f[$i] }
$prev.Dispose()

Start-Sleep -Milliseconds 400
Write-Host ""
Write-Host "=== view switcher indicator (design: 220ms) ==="
$c0=Grab
[B2]::PostMessageW($h,0x0201,[IntPtr]1,(Pt 1148 56))|Out-Null   # icon view button
$g2=@(); for($i=0;$i -lt 5;$i++){ Start-Sleep -Milliseconds 50; $g2+=(Grab) }
$prev=$c0
for($i=0;$i -lt $g2.Count;$i++){
  Write-Host ("  t=+{0,3}ms  switcher change: {1}%" -f (($i+1)*50),(PctChange $prev $g2[$i] 2050 2320 80 140))
  $prev.Dispose(); $prev=$g2[$i] }
$prev.Dispose()
$p.Kill()



