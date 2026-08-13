# Changes settings through the UI, closes the window, reopens it, and checks
# that what comes back is what was set. Restarting is the only test that means
# anything here. Pure ASCII.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Text; using System.Runtime.InteropServices;
public struct RC7 { public int L,T,R,B; }
public struct PT7 { public int X,Y; }
public class S2 {
  [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr c);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RC7 r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref PT7 p);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h,IntPtr a,int x,int y,int cx,int cy,uint f);
  [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h,uint m,IntPtr w,IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
  [DllImport("user32.dll")] public static extern bool GetCursorPos(out PT7 p);
}
"@
[S2]::SetProcessDpiAwarenessContext([IntPtr](-4)) | Out-Null
$OrielRoot = (Split-Path -Parent $PSScriptRoot)
$OrielArtifacts = Join-Path $OrielRoot 'artifacts'
New-Item -ItemType Directory -Force $OrielArtifacts | Out-Null
$sp="$OrielArtifacts"
$t="$sp\icontest"
$store="$env:LOCALAPPDATA\Oriel\settings.tsv"
Remove-Item $store -ErrorAction SilentlyContinue
$save = New-Object PT7; [S2]::GetCursorPos([ref]$save) | Out-Null

function Launch($argPath) {
  Get-Process oriel -ErrorAction SilentlyContinue | ForEach-Object { $_.Kill() }
  Start-Sleep -Milliseconds 600
  # Start-Process rejects an empty ArgumentList, so the no-argument launch has
  # to be a different call rather than an empty array.
  $exe = "$OrielRoot\build-rel\oriel.exe"
  if ($argPath) { $script:proc = Start-Process $exe -ArgumentList "`"$argPath`"" -PassThru }
  else          { $script:proc = Start-Process $exe -PassThru }
  $script:hw = [IntPtr]::Zero
  for($i=0;$i -lt 140;$i++){ Start-Sleep -Milliseconds 50; $script:proc.Refresh()
    if($script:proc.MainWindowHandle -ne [IntPtr]::Zero -and [S2]::IsWindowVisible($script:proc.MainWindowHandle)){
      $script:hw=$script:proc.MainWindowHandle; break } }
  Start-Sleep -Seconds 3
  $script:sc = [S2]::GetDpiForWindow($script:hw)/96.0
}
function Geom { $cr=New-Object RC7; [S2]::GetClientRect($script:hw,[ref]$cr)|Out-Null
                $o=New-Object PT7; [S2]::ClientToScreen($script:hw,[ref]$o)|Out-Null
                return @{ w=($cr.R-$cr.L); h=($cr.B-$cr.T); x=$o.X; y=$o.Y } }
function Click($xd,$yd){ $g=Geom
  [S2]::SetCursorPos([int]($g.x+$xd*$script:sc),[int]($g.y+$yd*$script:sc))|Out-Null
  $lp=[IntPtr](([int]($yd*$script:sc) -shl 16) -bor ([int]($xd*$script:sc) -band 0xFFFF))
  [S2]::PostMessageW($script:hw,0x0201,[IntPtr]1,$lp)|Out-Null
  [S2]::PostMessageW($script:hw,0x0202,[IntPtr]0,$lp)|Out-Null
  Start-Sleep -Milliseconds 600 }
function Shot($n){ $g=Geom
  $b=New-Object System.Drawing.Bitmap($g.w,$g.h)
  $gg=[System.Drawing.Graphics]::FromImage($b)
  $gg.CopyFromScreen($g.x,$g.y,0,0,(New-Object System.Drawing.Size($g.w,$g.h))); $gg.Dispose()
  $b.Save("$sp\$n",[System.Drawing.Imaging.ImageFormat]::Png); $b.Dispose() }

Write-Host "=== run 1: change things ==="
Launch $t
[S2]::SetWindowPos($script:hw,[IntPtr](-1),140,90,1600,1100,0x0040)|Out-Null
Start-Sleep -Milliseconds 800
$g=Geom; $wd=$g.w/$script:sc
Click ($wd-12-96-4-24) 56                 # gear: open the panel
Start-Sleep -Milliseconds 1000
$g=Geom; $wd=$g.w/$script:sc; $x0=$wd-376
Click ($x0+14+1*87+43) 92                 # appearance tab
Click ($x0+150+92+8) 141                  # dark
Click ($x0+158+3*30) 175                  # the fourth accent swatch
Click ($x0+14+2*87+43) 92                 # view tab
Click ($x0+200) 249                       # turn application marks off
Shot 'set-before.png'
Start-Sleep -Milliseconds 1600            # let the debounce fire
[S2]::PostMessageW($script:hw,0x0010,[IntPtr]0,[IntPtr]0)|Out-Null   # WM_CLOSE
Start-Sleep -Seconds 2
Get-Process oriel -ErrorAction SilentlyContinue | ForEach-Object { $_.Kill() }

Write-Host "--- settings.tsv ---"
if (Test-Path $store) { Get-Content $store } else { Write-Host "MISSING" }

Write-Host ""
Write-Host "=== run 2: no arguments at all - everything must come back ==="
Launch $null
Start-Sleep -Seconds 2
Shot 'set-after.png'
$g=Geom
Write-Host ("window client: {0}x{1} at ({2},{3})" -f $g.w,$g.h,$g.x,$g.y)
$script:proc.Kill()
[S2]::SetCursorPos($save.X,$save.Y)|Out-Null
