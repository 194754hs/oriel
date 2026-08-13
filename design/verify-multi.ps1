# Multi-selection: does it select, and do the commands act on all of it?
# Deleting three files with one keystroke is the test that matters. Pure ASCII.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
Add-Type @"
using System; using System.Text; using System.Runtime.InteropServices;
public struct RCB { public int L,T,R,B; }
public struct PTB { public int X,Y; }
public class D3 {
  [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr c);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RCB r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref PTB p);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h,IntPtr a,int x,int y,int cx,int cy,uint f);
  [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h,uint m,IntPtr w,IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
  [DllImport("user32.dll")] public static extern bool GetCursorPos(out PTB p);
  [DllImport("user32.dll")] public static extern void keybd_event(byte k,byte s,uint f,UIntPtr e);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, IntPtr pid);
  [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
  [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint a,uint b,bool attach);
  [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr h);
}
"@
[D3]::SetProcessDpiAwarenessContext([IntPtr](-4)) | Out-Null
$OrielRoot = (Split-Path -Parent $PSScriptRoot)
$OrielArtifacts = Join-Path $OrielRoot 'artifacts'
New-Item -ItemType Directory -Force $OrielArtifacts | Out-Null
$sp="$OrielArtifacts"
$t="$sp\multitest"
$log="$OrielRoot\build-rel\oriel.log"
Remove-Item $t -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item (Join-Path $env:LOCALAPPDATA 'Oriel\settings.tsv') -ErrorAction SilentlyContinue
Remove-Item $log -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $t | Out-Null
foreach($n in 'a.txt','b.txt','c.txt','d.txt','e.pdf'){ Set-Content "$t\$n" 'x' -Encoding Ascii }
Write-Host "before: $((Get-ChildItem $t).Name -join ', ')"

$save = New-Object PTB; [D3]::GetCursorPos([ref]$save) | Out-Null
Get-Process oriel -ErrorAction SilentlyContinue | ForEach-Object { $_.Kill() }
Start-Sleep -Milliseconds 500
$p = Start-Process "$OrielRoot\build-rel\oriel.exe" -ArgumentList "`"$t`"" -PassThru
$h=[IntPtr]::Zero
for($i=0;$i -lt 140;$i++){ Start-Sleep -Milliseconds 50; $p.Refresh()
  if($p.MainWindowHandle -ne [IntPtr]::Zero -and [D3]::IsWindowVisible($p.MainWindowHandle)){$h=$p.MainWindowHandle;break} }
function Focus {
  for($k=0;$k -lt 12;$k++){
    if([D3]::GetForegroundWindow() -eq $h){ return }
    $fg=[D3]::GetWindowThreadProcessId([D3]::GetForegroundWindow(),[IntPtr]::Zero)
    $me=[D3]::GetCurrentThreadId()
    if($fg -ne $me){ [D3]::AttachThreadInput($me,$fg,$true)|Out-Null }
    [D3]::BringWindowToTop($h)|Out-Null; [D3]::SetForegroundWindow($h)|Out-Null
    if($fg -ne $me){ [D3]::AttachThreadInput($me,$fg,$false)|Out-Null }
    Start-Sleep -Milliseconds 250 }
}
[D3]::SetWindowPos($h,[IntPtr](-1),80,60,1500,1000,0x0040)|Out-Null
Focus
Start-Sleep -Seconds 3
$S=[D3]::GetDpiForWindow($h)/96.0
$cr=New-Object RCB; [D3]::GetClientRect($h,[ref]$cr)|Out-Null
$o=New-Object PTB; [D3]::ClientToScreen($h,[ref]$o)|Out-Null
function ClickMod($xd,$yd,$wparam){
  [D3]::SetCursorPos([int]($o.X+$xd*$S),[int]($o.Y+$yd*$S))|Out-Null
  $lp=[IntPtr](([int]($yd*$S) -shl 16) -bor ([int]($xd*$S) -band 0xFFFF))
  [D3]::PostMessageW($h,0x0201,[IntPtr]$wparam,$lp)|Out-Null
  [D3]::PostMessageW($h,0x0202,[IntPtr]0,$lp)|Out-Null
  Start-Sleep -Milliseconds 600 }
function Chord($mods,$vk){
  Focus
  foreach($m in $mods){ [D3]::keybd_event($m,0,0,[UIntPtr]::Zero) }
  [D3]::keybd_event($vk,0,0,[UIntPtr]::Zero); [D3]::keybd_event($vk,0,2,[UIntPtr]::Zero)
  foreach($m in $mods){ [D3]::keybd_event($m,0,2,[UIntPtr]::Zero) }
  Start-Sleep -Milliseconds 900 }
function Shot($n){ $b=New-Object System.Drawing.Bitmap(($cr.R-$cr.L),($cr.B-$cr.T))
  $g=[System.Drawing.Graphics]::FromImage($b)
  $g.CopyFromScreen($o.X,$o.Y,0,0,(New-Object System.Drawing.Size(($cr.R-$cr.L),($cr.B-$cr.T)))); $g.Dispose()
  $b.Save("$sp\$n",[System.Drawing.Imaging.ImageFormat]::Png); $b.Dispose() }

# Rows: 82 + 26*n, centres at 95, 121, 147, 173, 199
Write-Host ""
Write-Host "== shift-click takes the run a..c =="
ClickMod 300 95 1          # plain click on a.txt
ClickMod 300 147 5         # MK_SHIFT|MK_LBUTTON on c.txt
Shot 'm-shift.png'

Write-Host "== ctrl-click adds e.pdf, skipping d =="
ClickMod 300 199 9         # MK_CONTROL|MK_LBUTTON
Shot 'm-ctrl.png'

Write-Host "== Ctrl+Shift+C copies all four paths =="
[System.Windows.Forms.Clipboard]::Clear()
Chord @(0x11,0x10) 0x43
$clip = [System.Windows.Forms.Clipboard]::GetText()
$lines = @($clip -split "`r`n" | Where-Object { $_ })
Write-Host "  $($lines.Count) path(s):"
$lines | ForEach-Object { Write-Host "    $(Split-Path $_ -Leaf)" }
if ($lines.Count -eq 4) { Write-Host "  PASS" } else { Write-Host "  FAIL (expected 4)" }

Write-Host ""
Write-Host "== Delete removes all four at once =="
Chord @() 0x2E
Start-Sleep -Seconds 2
$left = (Get-ChildItem $t).Name
Write-Host "  left: $($left -join ', ')"
if ($left.Count -eq 1 -and $left -contains 'd.txt') { Write-Host "  PASS" } else { Write-Host "  FAIL" }

$p.Kill()
[D3]::SetCursorPos($save.X,$save.Y)|Out-Null
