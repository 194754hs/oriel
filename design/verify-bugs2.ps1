# Second bug pass: history through the sidebar, tab close, filtered arrow keys.
# Pure ASCII.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Text; using System.Runtime.InteropServices;
public struct RCA { public int L,T,R,B; }
public struct PTA { public int X,Y; }
public class C3 {
  [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr c);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RCA r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref PTA p);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h,IntPtr a,int x,int y,int cx,int cy,uint f);
  [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h,uint m,IntPtr w,IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
  [DllImport("user32.dll")] public static extern bool GetCursorPos(out PTA p);
  [DllImport("user32.dll")] public static extern void keybd_event(byte k,byte s,uint f,UIntPtr e);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, IntPtr pid);
  [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
  [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint a,uint b,bool attach);
  [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr h);
  [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h,StringBuilder s,int n);
}
"@
[C3]::SetProcessDpiAwarenessContext([IntPtr](-4)) | Out-Null
$OrielRoot = (Split-Path -Parent $PSScriptRoot)
$OrielArtifacts = Join-Path $OrielRoot 'artifacts'
New-Item -ItemType Directory -Force $OrielArtifacts | Out-Null
$sp="$OrielArtifacts"
$t="$sp\bugtest"
$log="$OrielRoot\build-rel\oriel.log"
Remove-Item (Join-Path $env:LOCALAPPDATA 'Oriel\settings.tsv') -ErrorAction SilentlyContinue
Remove-Item $log -ErrorAction SilentlyContinue

$save = New-Object PTA; [C3]::GetCursorPos([ref]$save) | Out-Null
Get-Process oriel -ErrorAction SilentlyContinue | ForEach-Object { $_.Kill() }
Start-Sleep -Milliseconds 500
$p = Start-Process "$OrielRoot\build-rel\oriel.exe" -ArgumentList "`"$t`"" -PassThru
$h=[IntPtr]::Zero
for($i=0;$i -lt 140;$i++){ Start-Sleep -Milliseconds 50; $p.Refresh()
  if($p.MainWindowHandle -ne [IntPtr]::Zero -and [C3]::IsWindowVisible($p.MainWindowHandle)){$h=$p.MainWindowHandle;break} }
function Focus {
  for($k=0;$k -lt 12;$k++){
    if([C3]::GetForegroundWindow() -eq $h){ return }
    $fg=[C3]::GetWindowThreadProcessId([C3]::GetForegroundWindow(),[IntPtr]::Zero)
    $me=[C3]::GetCurrentThreadId()
    if($fg -ne $me){ [C3]::AttachThreadInput($me,$fg,$true)|Out-Null }
    [C3]::BringWindowToTop($h)|Out-Null; [C3]::SetForegroundWindow($h)|Out-Null
    if($fg -ne $me){ [C3]::AttachThreadInput($me,$fg,$false)|Out-Null }
    Start-Sleep -Milliseconds 250 }
}
[C3]::SetWindowPos($h,[IntPtr](-1),80,60,1500,1000,0x0040)|Out-Null
Focus
Start-Sleep -Seconds 3
$S=[C3]::GetDpiForWindow($h)/96.0
$cr=New-Object RCA; [C3]::GetClientRect($h,[ref]$cr)|Out-Null
$o=New-Object PTA; [C3]::ClientToScreen($h,[ref]$o)|Out-Null
function Click($xd,$yd){
  [C3]::SetCursorPos([int]($o.X+$xd*$S),[int]($o.Y+$yd*$S))|Out-Null
  $lp=[IntPtr](([int]($yd*$S) -shl 16) -bor ([int]($xd*$S) -band 0xFFFF))
  [C3]::PostMessageW($h,0x0201,[IntPtr]1,$lp)|Out-Null
  [C3]::PostMessageW($h,0x0202,[IntPtr]0,$lp)|Out-Null
  Start-Sleep -Milliseconds 800 }
function Chord($mods,$vk){
  Focus
  foreach($m in $mods){ [C3]::keybd_event($m,0,0,[UIntPtr]::Zero) }
  [C3]::keybd_event($vk,0,0,[UIntPtr]::Zero); [C3]::keybd_event($vk,0,2,[UIntPtr]::Zero)
  foreach($m in $mods){ [C3]::keybd_event($m,0,2,[UIntPtr]::Zero) }
  Start-Sleep -Milliseconds 900 }
function Title { $sb=New-Object System.Text.StringBuilder 256
  [C3]::GetWindowTextW($h,$sb,256)|Out-Null; return $sb.ToString() }
function Shot($n){ $b=New-Object System.Drawing.Bitmap(($cr.R-$cr.L),($cr.B-$cr.T))
  $g=[System.Drawing.Graphics]::FromImage($b)
  $g.CopyFromScreen($o.X,$o.Y,0,0,(New-Object System.Drawing.Size(($cr.R-$cr.L),($cr.B-$cr.T)))); $g.Dispose()
  $b.Save("$sp\$n",[System.Drawing.Imaging.ImageFormat]::Png); $b.Dispose() }
# The breadcrumb is the cheapest readable statement of where we are.
function Where_ {
  Shot 'tmp-where.png'
  return 'see shot'
}

Write-Host "== BUG 5: back must work after navigating from the sidebar =="
Click 90 165          # sidebar: 書類
Start-Sleep -Milliseconds 900
Shot 'b5-documents.png'
Chord @(0x12) 0x25    # Alt+Left
Start-Sleep -Milliseconds 900
Shot 'b5-back.png'
Write-Host "  compare b5-documents.png (書類) with b5-back.png (should be bugtest again)"

Write-Host ""
Write-Host "== BUG 6: a tab can be closed =="
Chord @(0x11) 0x54    # Ctrl+T opens a second tab
Start-Sleep -Milliseconds 900
Shot 'b6-two-tabs.png'
Chord @(0x11) 0x57    # Ctrl+W closes it
Start-Sleep -Milliseconds 900
Shot 'b6-one-tab.png'
Write-Host "  compare b6-two-tabs.png with b6-one-tab.png"

$p.Kill()
[C3]::SetCursorPos($save.X,$save.Y)|Out-Null
Write-Host ""
Write-Host "--- log tail ---"
Get-Content $log | Select-Object -Last 6
