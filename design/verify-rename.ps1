# Renames a real file through the UI and checks the disk afterwards. A field
# that appears is not a rename. Pure ASCII.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Text; using System.Runtime.InteropServices;
public struct RC6 { public int L,T,R,B; }
public struct PT6 { public int X,Y; }
public class R {
  [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr c);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RC6 r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref PT6 p);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h,IntPtr a,int x,int y,int cx,int cy,uint f);
  [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h,uint m,IntPtr w,IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
  [DllImport("user32.dll")] public static extern bool GetCursorPos(out PT6 p);
}
"@
[R]::SetProcessDpiAwarenessContext([IntPtr](-4)) | Out-Null
$OrielRoot = (Split-Path -Parent $PSScriptRoot)
$OrielArtifacts = Join-Path $OrielRoot 'artifacts'
New-Item -ItemType Directory -Force $OrielArtifacts | Out-Null
$sp="$OrielArtifacts"
$t="$sp\renametest"
Remove-Item $t -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $t | Out-Null
Set-Content "$t\aaa-before.txt" 'x' -Encoding Ascii
Set-Content "$t\zzz-other.txt"  'x' -Encoding Ascii
Write-Host "before: $((Get-ChildItem $t).Name -join ', ')"

$save = New-Object PT6; [R]::GetCursorPos([ref]$save) | Out-Null
Get-Process oriel -ErrorAction SilentlyContinue | ForEach-Object { $_.Kill() }
Start-Sleep -Milliseconds 500
$p = Start-Process "$OrielRoot\build-rel\oriel.exe" -ArgumentList "`"$t`"" -PassThru
$h=[IntPtr]::Zero
for($i=0;$i -lt 120;$i++){ Start-Sleep -Milliseconds 50; $p.Refresh()
  if($p.MainWindowHandle -ne [IntPtr]::Zero -and [R]::IsWindowVisible($p.MainWindowHandle)){$h=$p.MainWindowHandle;break} }
[R]::SetWindowPos($h,[IntPtr](-1),80,60,1500,1000,0x0040)|Out-Null
[R]::SetForegroundWindow($h)|Out-Null
Start-Sleep -Seconds 3
$S=[R]::GetDpiForWindow($h)/96.0
$cr=New-Object RC6; [R]::GetClientRect($h,[ref]$cr)|Out-Null
$o=New-Object PT6; [R]::ClientToScreen($h,[ref]$o)|Out-Null

function Click($xd,$yd){
  [R]::SetCursorPos([int]($o.X+$xd*$S),[int]($o.Y+$yd*$S))|Out-Null
  $lp=[IntPtr](([int]($yd*$S) -shl 16) -bor ([int]($xd*$S) -band 0xFFFF))
  [R]::PostMessageW($h,0x0201,[IntPtr]1,$lp)|Out-Null
  [R]::PostMessageW($h,0x0202,[IntPtr]0,$lp)|Out-Null
  Start-Sleep -Milliseconds 600 }
function Key($vk){ [R]::PostMessageW($h,0x0100,[IntPtr]$vk,[IntPtr]0)|Out-Null; Start-Sleep -Milliseconds 350 }
function Say($s){ foreach($c in $s.ToCharArray()){
    [R]::PostMessageW($h,0x0102,[IntPtr][int]$c,[IntPtr]0)|Out-Null; Start-Sleep -Milliseconds 45 }
  Start-Sleep -Milliseconds 400 }

Click 300 95          # select the first row
Key 0x71              # F2 starts the rename
Start-Sleep -Milliseconds 500
$b=New-Object System.Drawing.Bitmap(($cr.R-$cr.L),($cr.B-$cr.T))
$g=[System.Drawing.Graphics]::FromImage($b)
$g.CopyFromScreen($o.X,$o.Y,0,0,(New-Object System.Drawing.Size(($cr.R-$cr.L),($cr.B-$cr.T)))); $g.Dispose()
$b.Save("$sp\v-rename.png",[System.Drawing.Imaging.ImageFormat]::Png); $b.Dispose()
Say 'renamed'         # replaces the selected stem, keeps .txt
Key 0x0D              # Enter commits
Start-Sleep -Seconds 1
$p.Kill()
[R]::SetCursorPos($save.X,$save.Y)|Out-Null

$after = (Get-ChildItem $t).Name
Write-Host "after : $($after -join ', ')"
if ($after -contains 'renamed.txt') { Write-Host "RENAME OK - the file on disk changed name" }
else { Write-Host "RENAME FAILED - disk unchanged" }
Get-Content "$OrielRoot\build-rel\oriel.log" -ErrorAction SilentlyContinue |
  Select-String 'rename' | Select-Object -Last 3
