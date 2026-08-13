# Drives the commands and checks the world afterwards: the clipboard, the disk,
# the window. A menu item that opens is not a working command. Pure ASCII.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
Add-Type @"
using System; using System.Text; using System.Runtime.InteropServices;
public struct RC8 { public int L,T,R,B; }
public struct PT8 { public int X,Y; }
public class A3 {
  [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr c);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RC8 r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref PT8 p);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h,IntPtr a,int x,int y,int cx,int cy,uint f);
  [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h,uint m,IntPtr w,IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
  [DllImport("user32.dll")] public static extern bool GetCursorPos(out PT8 p);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern void keybd_event(byte k,byte s,uint f,UIntPtr e);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, IntPtr pid);
  [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
  [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint a,uint b,bool attach);
  [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr h);
}
"@
[A3]::SetProcessDpiAwarenessContext([IntPtr](-4)) | Out-Null

# SetForegroundWindow from a background process is refused under the foreground
# lock. Attaching to the current foreground thread's input queue first is the
# documented way round it, and without it every chord lands in another window.
# Defined before anything calls it: PowerShell scripts are read top to bottom.
function Focus {
  for($k=0;$k -lt 12;$k++){
    if([A3]::GetForegroundWindow() -eq $h){ return }
    $fg = [A3]::GetWindowThreadProcessId([A3]::GetForegroundWindow(), [IntPtr]::Zero)
    $me = [A3]::GetCurrentThreadId()
    if ($fg -ne $me) { [A3]::AttachThreadInput($me,$fg,$true) | Out-Null }
    [A3]::BringWindowToTop($h) | Out-Null
    [A3]::SetForegroundWindow($h) | Out-Null
    if ($fg -ne $me) { [A3]::AttachThreadInput($me,$fg,$false) | Out-Null }
    Start-Sleep -Milliseconds 250 }
}
$OrielRoot = (Split-Path -Parent $PSScriptRoot)
$OrielArtifacts = Join-Path $OrielRoot 'artifacts'
New-Item -ItemType Directory -Force $OrielArtifacts | Out-Null
$sp="$OrielArtifacts"
$t="$sp\acttest"
Remove-Item $t -Recurse -Force -ErrorAction SilentlyContinue
# Settings persist now, so a previous run collapsing the sidebar would move every
# coordinate in this test. Start from defaults.
Remove-Item (Join-Path $env:LOCALAPPDATA 'Oriel\settings.tsv') -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $t | Out-Null
Set-Content "$t\alpha.txt" 'x' -Encoding Ascii
Set-Content "$t\beta.txt"  'x' -Encoding Ascii
Write-Host "before: $((Get-ChildItem $t).Name -join ', ')"

$save = New-Object PT8; [A3]::GetCursorPos([ref]$save) | Out-Null
Get-Process oriel -ErrorAction SilentlyContinue | ForEach-Object { $_.Kill() }
Start-Sleep -Milliseconds 500
$p = Start-Process "$OrielRoot\build-rel\oriel.exe" -ArgumentList "`"$t`"" -PassThru
$h=[IntPtr]::Zero
for($i=0;$i -lt 140;$i++){ Start-Sleep -Milliseconds 50; $p.Refresh()
  if($p.MainWindowHandle -ne [IntPtr]::Zero -and [A3]::IsWindowVisible($p.MainWindowHandle)){$h=$p.MainWindowHandle;break} }
[A3]::SetWindowPos($h,[IntPtr](-1),80,60,1500,1000,0x0040)|Out-Null
Focus
Start-Sleep -Seconds 3
$S=[A3]::GetDpiForWindow($h)/96.0
$cr=New-Object RC8; [A3]::GetClientRect($h,[ref]$cr)|Out-Null
$o=New-Object PT8; [A3]::ClientToScreen($h,[ref]$o)|Out-Null

function Click($xd,$yd){
  [A3]::SetCursorPos([int]($o.X+$xd*$S),[int]($o.Y+$yd*$S))|Out-Null
  $lp=[IntPtr](([int]($yd*$S) -shl 16) -bor ([int]($xd*$S) -band 0xFFFF))
  [A3]::PostMessageW($h,0x0201,[IntPtr]1,$lp)|Out-Null
  [A3]::PostMessageW($h,0x0202,[IntPtr]0,$lp)|Out-Null
  Start-Sleep -Milliseconds 700 }
# A real chord: the window reads GetKeyState, which posted messages do not set.
function Chord($mods,$vk){
  Focus
  if([A3]::GetForegroundWindow() -ne $h){ throw "cannot focus the window; the chord would land elsewhere" }
  foreach($m in $mods){ [A3]::keybd_event($m,0,0,[UIntPtr]::Zero) }
  [A3]::keybd_event($vk,0,0,[UIntPtr]::Zero)
  [A3]::keybd_event($vk,0,2,[UIntPtr]::Zero)
  foreach($m in $mods){ [A3]::keybd_event($m,0,2,[UIntPtr]::Zero) }
  Start-Sleep -Milliseconds 800 }
function Shot($n){ $b=New-Object System.Drawing.Bitmap(($cr.R-$cr.L),($cr.B-$cr.T))
  $g=[System.Drawing.Graphics]::FromImage($b)
  $g.CopyFromScreen($o.X,$o.Y,0,0,(New-Object System.Drawing.Size(($cr.R-$cr.L),($cr.B-$cr.T)))); $g.Dispose()
  $b.Save("$sp\$n",[System.Drawing.Imaging.ImageFormat]::Png); $b.Dispose() }

Focus
Click 300 95                      # select alpha.txt
Start-Sleep -Milliseconds 400

Write-Host ""
Write-Host "== Ctrl+Shift+C : copy path =="
[System.Windows.Forms.Clipboard]::Clear()   # a stale value must not read as a pass
Chord @(0x11,0x10) 0x43
$clip = [System.Windows.Forms.Clipboard]::GetText()
Write-Host "  clipboard: $clip"
if ($clip -like "*acttest\alpha.txt") { Write-Host "  PASS" } else { Write-Host "  FAIL" }

Write-Host ""
Write-Host "== Ctrl+D : duplicate =="
Focus
Chord @(0x11) 0x44
Start-Sleep -Seconds 1
$names = (Get-ChildItem $t).Name
Write-Host "  now: $($names -join ', ')"
if ($names -contains 'alpha のコピー.txt') { Write-Host "  PASS" } else { Write-Host "  FAIL" }

Write-Host ""
Write-Host "== Ctrl+Shift+N : new folder, ready to name =="
Focus
Chord @(0x11,0x10) 0x4E
Start-Sleep -Seconds 1
Shot 'act-newfolder.png'
$names = (Get-ChildItem $t).Name
if ($names -contains '新規フォルダ') { Write-Host "  PASS (created)" } else { Write-Host "  FAIL" }

Write-Host ""
Write-Host "== sidebar collapse =="
[A3]::PostMessageW($h,0x001B,[IntPtr]0,[IntPtr]0)|Out-Null   # kill focus, close any editor
Click 22 56
Start-Sleep -Milliseconds 700
Shot 'act-sidebar.png'

$p.Kill()
[A3]::SetCursorPos($save.X,$save.Y)|Out-Null
Write-Host ""
Write-Host "final: $((Get-ChildItem $t).Name -join ', ')"
