# Does it stay responsive on a directory big enough to hurt, and does it hold
# its memory over a long browse? Pure ASCII.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Text; using System.Runtime.InteropServices;
public struct RCC { public int L,T,R,B; }
public struct PTC { public int X,Y; }
public class E3 {
  [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr c);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RCC r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref PTC p);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h,IntPtr a,int x,int y,int cx,int cy,uint f);
  [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h,uint m,IntPtr w,IntPtr l);
  [DllImport("user32.dll")] public static extern IntPtr SendMessageTimeoutW(IntPtr h,uint m,IntPtr w,IntPtr l,uint fl,uint ms,out IntPtr res);
  [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
  [DllImport("user32.dll")] public static extern bool GetCursorPos(out PTC p);
}
"@
[E3]::SetProcessDpiAwarenessContext([IntPtr](-4)) | Out-Null
$OrielRoot = (Split-Path -Parent $PSScriptRoot)
$OrielArtifacts = Join-Path $OrielRoot 'artifacts'
New-Item -ItemType Directory -Force $OrielArtifacts | Out-Null
$sp="$OrielArtifacts"
$save = New-Object PTC; [E3]::GetCursorPos([ref]$save) | Out-Null

Get-Process oriel -ErrorAction SilentlyContinue | ForEach-Object { $_.Kill() }
Start-Sleep -Milliseconds 500
$sw=[System.Diagnostics.Stopwatch]::StartNew()
$p = Start-Process "$OrielRoot\build-rel\oriel.exe" -ArgumentList '"C:\Windows\System32"' -PassThru
$h=[IntPtr]::Zero
for($i=0;$i -lt 200;$i++){ Start-Sleep -Milliseconds 50; $p.Refresh()
  if($p.MainWindowHandle -ne [IntPtr]::Zero -and [E3]::IsWindowVisible($p.MainWindowHandle)){$h=$p.MainWindowHandle;break} }
Write-Host ("window up after {0} ms" -f [int]$sw.Elapsed.TotalMilliseconds)
[E3]::SetWindowPos($h,[IntPtr](-1),80,60,1600,1100,0x0040)|Out-Null
[E3]::SetForegroundWindow($h)|Out-Null
Start-Sleep -Seconds 4

$S=[E3]::GetDpiForWindow($h)/96.0
$cr=New-Object RCC; [E3]::GetClientRect($h,[ref]$cr)|Out-Null
$o=New-Object PTC; [E3]::ClientToScreen($h,[ref]$o)|Out-Null
function Shot($n){ $b=New-Object System.Drawing.Bitmap(($cr.R-$cr.L),($cr.B-$cr.T))
  $g=[System.Drawing.Graphics]::FromImage($b)
  $g.CopyFromScreen($o.X,$o.Y,0,0,(New-Object System.Drawing.Size(($cr.R-$cr.L),($cr.B-$cr.T)))); $g.Dispose()
  $b.Save("$sp\$n",[System.Drawing.Imaging.ImageFormat]::Png); $b.Dispose() }
# Round trip through the UI thread: how long the window takes to answer at all.
function Latency {
  $t=[System.Diagnostics.Stopwatch]::StartNew()
  $res=[IntPtr]::Zero
  [E3]::SendMessageTimeoutW($h,0x0000,[IntPtr]0,[IntPtr]0,0x0002,4000,[ref]$res)|Out-Null
  return [int]$t.Elapsed.TotalMilliseconds }

Shot 'stress-system32.png'
$p.Refresh()
Write-Host ("after loading 4674 entries: {0:N1} MB, UI answers in {1} ms" -f ($p.WorkingSet64/1MB), (Latency))

Write-Host ""
Write-Host "scrolling and selecting for 60 s..."
$rnd = 0
for ($i=0; $i -lt 560; $i++) {
  $rnd = ($rnd * 1103515245 + 12345) -band 0x7FFFFFFF
  $y = 90 + ($rnd % 700)
  [E3]::SetCursorPos([int]($o.X+300*$S),[int]($o.Y+$y*$S))|Out-Null
  $lp=[IntPtr](([int]($y*$S) -shl 16) -bor ([int](300*$S) -band 0xFFFF))
  [E3]::PostMessageW($h,0x0201,[IntPtr]1,$lp)|Out-Null
  [E3]::PostMessageW($h,0x0202,[IntPtr]0,$lp)|Out-Null
  # wheel over the column
  $wlp=[IntPtr](([int](($o.Y+400*$S)) -shl 16) -bor ([int]($o.X+300*$S) -band 0xFFFF))
  [E3]::PostMessageW($h,0x020A,[IntPtr](-120 -shl 16),$wlp)|Out-Null
  Start-Sleep -Milliseconds 450
  if (($i % 50) -eq 49) {
    $p.Refresh()
    Write-Host ("  t={0,3}s  {1,6:N1} MB  handles={2,5}  threads={3,4}  UI {4} ms" -f `
      [int]((($i+1)*0.45)), ($p.WorkingSet64/1MB), $p.HandleCount, $p.Threads.Count, (Latency))
  }
}
Shot 'stress-after.png'
$p.Refresh()
Write-Host ""
Write-Host ("final: {0:N1} MB, handles {1}, threads {2}, UI {3} ms" -f `
  ($p.WorkingSet64/1MB), $p.HandleCount, $p.Threads.Count, (Latency))
$p.Kill()
[E3]::SetCursorPos($save.X,$save.Y)|Out-Null
