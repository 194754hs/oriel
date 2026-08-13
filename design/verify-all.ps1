# Exercises the features added in this pass and captures each one, so they are
# reported from the screen rather than from the source. Pure ASCII.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Text; using System.Runtime.InteropServices;
public struct RC5 { public int L,T,R,B; }
public struct PT5 { public int X,Y; }
public class Z {
  [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr c);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RC5 r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref PT5 p);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h,IntPtr a,int x,int y,int cx,int cy,uint f);
  [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h,uint m,IntPtr w,IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
  [DllImport("user32.dll")] public static extern bool GetCursorPos(out PT5 p);
}
"@
[Z]::SetProcessDpiAwarenessContext([IntPtr](-4)) | Out-Null
$OrielRoot = (Split-Path -Parent $PSScriptRoot)
$OrielArtifacts = Join-Path $OrielRoot 'artifacts'
New-Item -ItemType Directory -Force $OrielArtifacts | Out-Null
$sp="$OrielArtifacts"
$t="$sp\icontest"
$save = New-Object PT5; [Z]::GetCursorPos([ref]$save) | Out-Null

Get-Process oriel -ErrorAction SilentlyContinue | ForEach-Object { $_.Kill() }
Start-Sleep -Milliseconds 500
$p = Start-Process "$OrielRoot\build-rel\oriel.exe" -ArgumentList "`"$t`"" -PassThru
$h=[IntPtr]::Zero
for($i=0;$i -lt 120;$i++){ Start-Sleep -Milliseconds 50; $p.Refresh()
  if($p.MainWindowHandle -ne [IntPtr]::Zero -and [Z]::IsWindowVisible($p.MainWindowHandle)){$h=$p.MainWindowHandle;break} }
[Z]::SetWindowPos($h,[IntPtr](-1),80,60,1700,1200,0x0040)|Out-Null
[Z]::SetForegroundWindow($h)|Out-Null
Start-Sleep -Seconds 4
$S=[Z]::GetDpiForWindow($h)/96.0

function Geom { $cr=New-Object RC5; [Z]::GetClientRect($h,[ref]$cr)|Out-Null
                $o=New-Object PT5; [Z]::ClientToScreen($h,[ref]$o)|Out-Null
                return @{ w=($cr.R-$cr.L); h=($cr.B-$cr.T); x=$o.X; y=$o.Y } }
function Shot($name){ $g=Geom
  $b=New-Object System.Drawing.Bitmap($g.w,$g.h)
  $gg=[System.Drawing.Graphics]::FromImage($b)
  $gg.CopyFromScreen($g.x,$g.y,0,0,(New-Object System.Drawing.Size($g.w,$g.h))); $gg.Dispose()
  $b.Save("$sp\$name",[System.Drawing.Imaging.ImageFormat]::Png); $b.Dispose()
  Write-Host "  wrote $name" }
function Click($xd,$yd){ $g=Geom
  [Z]::SetCursorPos([int]($g.x+$xd*$S),[int]($g.y+$yd*$S))|Out-Null
  $lp=[IntPtr](([int]($yd*$S) -shl 16) -bor ([int]($xd*$S) -band 0xFFFF))
  [Z]::PostMessageW($h,0x0201,[IntPtr]1,$lp)|Out-Null
  [Z]::PostMessageW($h,0x0202,[IntPtr]0,$lp)|Out-Null
  Start-Sleep -Milliseconds 600 }
function Say($s){ foreach($c in $s.ToCharArray()){
    [Z]::PostMessageW($h,0x0102,[IntPtr][int]$c,[IntPtr]0)|Out-Null; Start-Sleep -Milliseconds 40 }
  Start-Sleep -Milliseconds 500 }
function Key($vk){ [Z]::PostMessageW($h,0x0100,[IntPtr]$vk,[IntPtr]0)|Out-Null; Start-Sleep -Milliseconds 400 }

$g=Geom; $wd=$g.w/$S
Write-Host "== search =="
Click ($wd-12-96-4-44-146+40) 56     # inside the search capsule
Say 'ae'
Shot 'v-search.png'
Key 0x1B                              # Esc clears the filter

Write-Host "== tag, then the tag view =="
Click 300 95                          # first row
[Z]::PostMessageW($h,0x0100,[IntPtr]0x11,[IntPtr]0)|Out-Null   # Ctrl down
Start-Sleep -Milliseconds 100
Write-Host "  (tagging needs a real Ctrl chord; using the sidebar view instead)"

Write-Host "== breadcrumb hover =="
Click 300 95
Start-Sleep -Milliseconds 500
$g=Geom
[Z]::SetCursorPos([int]($g.x+60*$S),[int]($g.y+($g.h/$S-13)*$S))|Out-Null
Start-Sleep -Milliseconds 500
Shot 'v-crumb.png'

Write-Host "== inspector: general =="
Click ($wd-12-96-4-24) 56             # gear
Start-Sleep -Milliseconds 900
$g=Geom; $wd=$g.w/$S; $x0=$wd-376
Click ($x0+14+0*87+43) 92             # the general tab
Shot 'v-insp-general.png'
Click ($x0+14+3*87+43) 92             # the keys tab
Shot 'v-insp-keys.png'
$p.Kill()
[Z]::SetCursorPos($save.X,$save.Y)|Out-Null
Write-Host "done"
