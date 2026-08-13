# Opens the panel, switches to the view tab and toggles the application-mark
# setting, capturing before and after. Proves the switch does something rather
# than assuming the wiring is right. Pure ASCII.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Text; using System.Runtime.InteropServices;
public struct RC4 { public int L,T,R,B; }
public struct PT4 { public int X,Y; }
public class Q {
  [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr c);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RC4 r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref PT4 p);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h,IntPtr a,int x,int y,int cx,int cy,uint f);
  [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h,uint m,IntPtr w,IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
  [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h,StringBuilder s,int n);
  [DllImport("user32.dll")] public static extern IntPtr SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
}
"@
[Q]::SetProcessDpiAwarenessContext([IntPtr](-4)) | Out-Null
$OrielRoot = (Split-Path -Parent $PSScriptRoot)
$OrielArtifacts = Join-Path $OrielRoot 'artifacts'
New-Item -ItemType Directory -Force $OrielArtifacts | Out-Null
$sp="$OrielArtifacts"
$t="$sp\icontest"

Get-Process oriel -ErrorAction SilentlyContinue | ForEach-Object { $_.Kill() }
Start-Sleep -Milliseconds 500
$p = Start-Process "$OrielRoot\build-rel\oriel.exe" -ArgumentList "`"$t`"" -PassThru
$h=[IntPtr]::Zero
for($i=0;$i -lt 120;$i++){ Start-Sleep -Milliseconds 50; $p.Refresh()
  if($p.MainWindowHandle -ne [IntPtr]::Zero -and [Q]::IsWindowVisible($p.MainWindowHandle)){$h=$p.MainWindowHandle;break} }
[Q]::SetWindowPos($h,[IntPtr](-1),80,60,1500,1200,0x0040)|Out-Null
[Q]::SetForegroundWindow($h)|Out-Null
Start-Sleep -Seconds 4

$S=[Q]::GetDpiForWindow($h)/96.0
function Geom { $cr=New-Object RC4; [Q]::GetClientRect($h,[ref]$cr)|Out-Null
                $o=New-Object PT4; [Q]::ClientToScreen($h,[ref]$o)|Out-Null
                return @{ w=($cr.R-$cr.L); h=($cr.B-$cr.T); x=$o.X; y=$o.Y } }
function Shot($name){ $g0=Geom
  $b=New-Object System.Drawing.Bitmap($g0.w,$g0.h)
  $gg=[System.Drawing.Graphics]::FromImage($b)
  $gg.CopyFromScreen($g0.x,$g0.y,0,0,(New-Object System.Drawing.Size($g0.w,$g0.h))); $gg.Dispose()
  $b.Save("$sp\$name",[System.Drawing.Imaging.ImageFormat]::Png); $b.Dispose()
  Write-Host ("  $name  {0}x{1}" -f $g0.w,$g0.h) }
function Hit($xd,$yd){ $g0=Geom
  [Q]::SetCursorPos([int]($g0.x+$xd*$S),[int]($g0.y+$yd*$S))|Out-Null
  $lp=[IntPtr](([int]($yd*$S) -shl 16) -bor ([int]($xd*$S) -band 0xFFFF))
  [Q]::PostMessageW($h,0x0201,[IntPtr]1,$lp)|Out-Null
  [Q]::PostMessageW($h,0x0202,[IntPtr]0,$lp)|Out-Null
  Start-Sleep -Milliseconds 700 }

# gear sits 24 DIP left of the view switcher, which hugs the right edge
$g=Geom; $wd=$g.w/$S
Hit ($wd-12-96-4-24) 56       # open the panel
Start-Sleep -Milliseconds 800
$g=Geom; $wd=$g.w/$S
$x0=$wd-376
Hit ($x0+14+2*87+43) 92       # the view tab
Shot 'appicons-on.png'
Hit ($x0+200) 249             # second checkbox row: application marks
Shot 'appicons-off.png'
Hit ($x0+200) 249             # and back on
Shot 'appicons-back.png'
$p.Kill()
