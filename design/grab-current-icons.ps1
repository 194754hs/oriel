# Crops the icons Oriel draws today straight out of a running window, so the
# comparison is against what actually ships rather than against the source.
# Pure ASCII.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Text; using System.Runtime.InteropServices;
public struct RCT2 { public int L,T,R,B; }
public struct PT2 { public int X,Y; }
public class G {
  [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr c);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RCT2 r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref PT2 p);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h,IntPtr a,int x,int y,int cx,int cy,uint f);
  [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
  [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h,StringBuilder s,int n);
  [DllImport("user32.dll")] public static extern IntPtr SetForegroundWindow(IntPtr h);
}
"@
[G]::SetProcessDpiAwarenessContext([IntPtr](-4)) | Out-Null

$OrielRoot = (Split-Path -Parent $PSScriptRoot)
$rel = "$OrielRoot\build-rel"
$out = "$OrielRoot\design\icon-candidates"
New-Item -ItemType Directory -Force $out | Out-Null

Get-Process oriel -ErrorAction SilentlyContinue | ForEach-Object { $_.Kill() }
Start-Sleep -Milliseconds 500
$p = Start-Process "$rel\oriel.exe" -ArgumentList "`"$env:USERPROFILE\Documents`"" -PassThru
$h = [IntPtr]::Zero
for ($i=0; $i -lt 120; $i++) {
  Start-Sleep -Milliseconds 50; $p.Refresh()
  if ($p.MainWindowHandle -ne [IntPtr]::Zero -and [G]::IsWindowVisible($p.MainWindowHandle)) { $h=$p.MainWindowHandle; break }
}
$sb = New-Object System.Text.StringBuilder 64; [G]::GetClassNameW($h,$sb,64) | Out-Null
if ($sb.ToString() -ne 'OrielShell') { $p.Kill(); throw "wrong window" }
[G]::SetWindowPos($h,[IntPtr](-1),120,80,2300,1360,0x0040) | Out-Null
[G]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 1800

$S = [G]::GetDpiForWindow($h) / 96.0
$cr = New-Object RCT2; [G]::GetClientRect($h,[ref]$cr) | Out-Null
$o  = New-Object PT2;  [G]::ClientToScreen($h,[ref]$o)  | Out-Null
$full = New-Object System.Drawing.Bitmap(($cr.R-$cr.L),($cr.B-$cr.T))
$g = [System.Drawing.Graphics]::FromImage($full)
$g.CopyFromScreen($o.X,$o.Y,0,0,(New-Object System.Drawing.Size(($cr.R-$cr.L),($cr.B-$cr.T))))
$g.Dispose()
$full.Save("$out\current-window.png",[System.Drawing.Imaging.ImageFormat]::Png)

# centre of each mark, in DIPs, derived from the same constants the renderer uses
$marks = @(
  @{n='folder'; x= 27; y= 19},   # tab
  @{n='sort';   x=748; y= 56},
  @{n='share';  x=778; y= 56},
  @{n='more';   x=808; y= 56},
  @{n='search'; x=850; y= 56},
  @{n='gear';   x=1001;y= 56},
  @{n='column'; x=1041;y= 56},   # selected: white on accent
  @{n='list';   x=1073;y= 56},
  @{n='grid';   x=1105;y= 56},
  @{n='clock';  x= 25; y=113},
  @{n='desktop';x= 25; y=139},
  @{n='doc';    x= 25; y=165},
  @{n='download';x=25; y=191},
  @{n='star';   x= 25; y=217},
  @{n='pc';     x= 25; y=276},
  @{n='drive';  x= 25; y=302},
  @{n='cloud';  x= 25; y=328}
)
# 24 DIP box around the centre: the mark itself is 16, so this keeps its margin
$box = [int](24 * $S)
foreach ($m in $marks) {
  $cx = [int]($m.x * $S); $cy = [int]($m.y * $S)
  $crop = New-Object System.Drawing.Bitmap($box,$box)
  $gc = [System.Drawing.Graphics]::FromImage($crop)
  $gc.DrawImage($full, (New-Object System.Drawing.Rectangle(0,0,$box,$box)),
                (New-Object System.Drawing.Rectangle(($cx-$box/2),($cy-$box/2),$box,$box)),
                [System.Drawing.GraphicsUnit]::Pixel)
  $gc.Dispose()
  $crop.Save("$out\current-$($m.n).png",[System.Drawing.Imaging.ImageFormat]::Png)
  $crop.Dispose()
}
$full.Dispose()
$p.Kill()
Write-Host ("cropped {0} marks at {1}x{1}px into {2}" -f $marks.Count, $box, $out)
