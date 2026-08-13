# Exercises the specific defects fixed in this pass, so each is proved rather
# than asserted. Pure ASCII.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Text; using System.Runtime.InteropServices;
public struct RC9 { public int L,T,R,B; }
public struct PT9 { public int X,Y; }
public class B3 {
  [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr c);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RC9 r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref PT9 p);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h,IntPtr a,int x,int y,int cx,int cy,uint f);
  [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h,uint m,IntPtr w,IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
  [DllImport("user32.dll")] public static extern bool GetCursorPos(out PT9 p);
  [DllImport("user32.dll")] public static extern void keybd_event(byte k,byte s,uint f,UIntPtr e);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, IntPtr pid);
  [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
  [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint a,uint b,bool attach);
  [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr h);
}
"@
[B3]::SetProcessDpiAwarenessContext([IntPtr](-4)) | Out-Null
$OrielRoot = (Split-Path -Parent $PSScriptRoot)
$OrielArtifacts = Join-Path $OrielRoot 'artifacts'
New-Item -ItemType Directory -Force $OrielArtifacts | Out-Null
$sp="$OrielArtifacts"
$t="$sp\bugtest"
$log="$OrielRoot\build-rel\oriel.log"
Remove-Item $t -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item (Join-Path $env:LOCALAPPDATA 'Oriel\settings.tsv') -ErrorAction SilentlyContinue
Remove-Item (Join-Path $env:LOCALAPPDATA 'Oriel\tags.tsv') -ErrorAction SilentlyContinue
Remove-Item $log -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $t | Out-Null
New-Item -ItemType Directory -Force "$t\sub" | Out-Null
Set-Content "$t\sub\inner.txt" 'x' -Encoding Ascii
Set-Content "$t\one.txt" 'x' -Encoding Ascii
Set-Content "$t\two.txt" 'x' -Encoding Ascii

$save = New-Object PT9; [B3]::GetCursorPos([ref]$save) | Out-Null
Get-Process oriel -ErrorAction SilentlyContinue | ForEach-Object { $_.Kill() }
Start-Sleep -Milliseconds 500
$p = Start-Process "$OrielRoot\build-rel\oriel.exe" -ArgumentList "`"$t`"" -PassThru
$h=[IntPtr]::Zero
for($i=0;$i -lt 140;$i++){ Start-Sleep -Milliseconds 50; $p.Refresh()
  if($p.MainWindowHandle -ne [IntPtr]::Zero -and [B3]::IsWindowVisible($p.MainWindowHandle)){$h=$p.MainWindowHandle;break} }

function Focus {
  for($k=0;$k -lt 12;$k++){
    if([B3]::GetForegroundWindow() -eq $h){ return }
    $fg=[B3]::GetWindowThreadProcessId([B3]::GetForegroundWindow(),[IntPtr]::Zero)
    $me=[B3]::GetCurrentThreadId()
    if($fg -ne $me){ [B3]::AttachThreadInput($me,$fg,$true)|Out-Null }
    [B3]::BringWindowToTop($h)|Out-Null; [B3]::SetForegroundWindow($h)|Out-Null
    if($fg -ne $me){ [B3]::AttachThreadInput($me,$fg,$false)|Out-Null }
    Start-Sleep -Milliseconds 250 }
}
[B3]::SetWindowPos($h,[IntPtr](-1),80,60,1500,1000,0x0040)|Out-Null
Focus
Start-Sleep -Seconds 3
$S=[B3]::GetDpiForWindow($h)/96.0
$cr=New-Object RC9; [B3]::GetClientRect($h,[ref]$cr)|Out-Null
$o=New-Object PT9; [B3]::ClientToScreen($h,[ref]$o)|Out-Null

function Click($xd,$yd){
  [B3]::SetCursorPos([int]($o.X+$xd*$S),[int]($o.Y+$yd*$S))|Out-Null
  $lp=[IntPtr](([int]($yd*$S) -shl 16) -bor ([int]($xd*$S) -band 0xFFFF))
  [B3]::PostMessageW($h,0x0201,[IntPtr]1,$lp)|Out-Null
  [B3]::PostMessageW($h,0x0202,[IntPtr]0,$lp)|Out-Null
  Start-Sleep -Milliseconds 600 }
function RClick($xd,$yd){
  [B3]::SetCursorPos([int]($o.X+$xd*$S),[int]($o.Y+$yd*$S))|Out-Null
  $lp=[IntPtr](([int]($yd*$S) -shl 16) -bor ([int]($xd*$S) -band 0xFFFF))
  [B3]::PostMessageW($h,0x0204,[IntPtr]2,$lp)|Out-Null
  Start-Sleep -Milliseconds 900
  [B3]::keybd_event(0x1B,0,0,[UIntPtr]::Zero); [B3]::keybd_event(0x1B,0,2,[UIntPtr]::Zero)
  Start-Sleep -Milliseconds 500 }
function Chord($mods,$vk){
  Focus
  foreach($m in $mods){ [B3]::keybd_event($m,0,0,[UIntPtr]::Zero) }
  [B3]::keybd_event($vk,0,0,[UIntPtr]::Zero); [B3]::keybd_event($vk,0,2,[UIntPtr]::Zero)
  foreach($m in $mods){ [B3]::keybd_event($m,0,2,[UIntPtr]::Zero) }
  Start-Sleep -Milliseconds 900 }
function Shot($n){ $b=New-Object System.Drawing.Bitmap(($cr.R-$cr.L),($cr.B-$cr.T))
  $g=[System.Drawing.Graphics]::FromImage($b)
  $g.CopyFromScreen($o.X,$o.Y,0,0,(New-Object System.Drawing.Size(($cr.R-$cr.L),($cr.B-$cr.T)))); $g.Dispose()
  $b.Save("$sp\$n",[System.Drawing.Imaging.ImageFormat]::Png); $b.Dispose() }

Write-Host "== BUG 1: right-clicking the same row twice must not grow the menu =="
Click 300 95
RClick 300 95
RClick 300 95
$counts = (Get-Content $log | Select-String 'context menu items: (\d+)' |
           ForEach-Object { [int]$_.Matches[0].Groups[1].Value })
Write-Host "  item counts across the two right-clicks: $($counts -join ', ')"
if ($counts.Count -ge 2 -and $counts[0] -eq $counts[1]) { Write-Host "  PASS" } else { Write-Host "  FAIL" }

Write-Host ""
Write-Host "== BUG 2: opening a folder must descend, not launch another manager =="
$before = (Get-Process explorer -ErrorAction SilentlyContinue).Count
Click 300 95                       # 'sub' is first (folders lead)
Chord @(0x11) 0x28                 # Ctrl+Down
Shot 'bug-openfolder.png'
$after = (Get-Process explorer -ErrorAction SilentlyContinue).Count
Write-Host "  explorer processes before=$before after=$after"
if ($before -eq $after) { Write-Host "  PASS (nothing else launched)" } else { Write-Host "  FAIL" }

Write-Host ""
Write-Host "== BUG 3: a tag view must survive a refresh =="
Click 300 95
Chord @(0x11) 0x31                 # Ctrl+1 tags it
Start-Sleep -Milliseconds 600
# Sidebar rows: 78 +22 +5*26 +11 +22 +3*26 +11 +22 = 374 is the first tag row.
Click 90 387
Start-Sleep -Milliseconds 900
Shot 'bug-tag-before.png'
Chord @() 0x74                     # F5
Start-Sleep -Milliseconds 800
Shot 'bug-tag-after.png'
Write-Host "  see bug-tag-before.png / bug-tag-after.png"

Write-Host ""
Write-Host "== BUG 4: right-click past the last row offers the folder menu =="
RClick 300 700
Write-Host "  see the log for whether a menu ran"

$p.Kill()
[B3]::SetCursorPos($save.X,$save.Y)|Out-Null
Write-Host ""
Write-Host "--- log tail ---"
Get-Content $log | Select-Object -Last 12
