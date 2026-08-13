# Exercises the context menu: hover a row, right-click it, photograph the menu,
# then report what the app logged about where the build time went. Pure ASCII.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

Add-Type @"
using System;
using System.Runtime.InteropServices;
public struct RECT3 { public int L, T, R, B; }
public class W3 {
    [DllImport("dwmapi.dll")]
    public static extern int DwmGetWindowAttribute(IntPtr h, int a, out RECT3 r, int s);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(
        IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll")] public static extern bool PostMessageW(
        IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)]
    public static extern int GetClassNameW(IntPtr h, System.Text.StringBuilder s, int n);
}
"@

$OrielRoot = $PSScriptRoot
$root = "$OrielRoot"
$log  = "$root\build\oriel.log"
if (Test-Path $log) { Remove-Item $log }

$p = Start-Process "$root\build\oriel.exe" -PassThru
$h = [IntPtr]::Zero
for ($i = 0; $i -lt 120; $i++) {
    $p.Refresh()
    if ($p.HasExited) { throw "exited early: $($p.ExitCode)" }
    if ($p.MainWindowHandle -ne [IntPtr]::Zero -and [W3]::IsWindowVisible($p.MainWindowHandle)) {
        $h = $p.MainWindowHandle; break
    }
    Start-Sleep -Milliseconds 50
}
if ($h -eq [IntPtr]::Zero) { $p.Kill(); throw "no window" }
$sb = New-Object System.Text.StringBuilder 256
[W3]::GetClassNameW($h, $sb, 256) | Out-Null
if ($sb.ToString() -ne 'OrielShell') { $p.Kill(); throw "wrong window: $sb" }

[W3]::SetWindowPos($h, [IntPtr](-1), 0, 0, 0, 0, 0x0043) | Out-Null
Start-Sleep -Milliseconds 1200      # let the first listing arrive

# Row 0 of column 0, in DIPs. PowerShell is DPI-unaware, so Windows virtualises
# the coordinates of messages it posts to our per-monitor-aware window - they
# arrive already scaled. Posting DIPs is therefore correct, not physical pixels.
# NB: do not name this LP - that is an alias for Out-Printer.
function MakeLParam($x, $y) { return [IntPtr](($y -shl 16) -bor ($x -band 0xFFFF)) }
$pt = MakeLParam 298 95

# hover first, exactly as a real pointer would
[W3]::PostMessageW($h, 0x0200, [IntPtr]0, $pt) | Out-Null      # WM_MOUSEMOVE
Start-Sleep -Milliseconds 700
[W3]::PostMessageW($h, 0x0204, [IntPtr]2, $pt) | Out-Null      # WM_RBUTTONDOWN
Start-Sleep -Milliseconds 1400

$r = New-Object RECT3
[W3]::DwmGetWindowAttribute($h, 9, [ref]$r, 16) | Out-Null
# grab a generous area: the popup extends past the window frame
$x = [Math]::Max(0, $r.L); $y = [Math]::Max(0, $r.T)
$w = $r.R - $x; $ht = $r.B - $y
$bmp = New-Object System.Drawing.Bitmap($w, $ht)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($x, $y, 0, 0, (New-Object System.Drawing.Size($w, $ht)))
$out = "$root\build\shot-menu.png"
$bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()

$p.Kill()          # a tracked popup owns a modal loop; nothing polite works here
Start-Sleep -Milliseconds 300

Write-Host "captured -> $out"
Write-Host "--- log ---"
if (Test-Path $log) { Get-Content $log } else { Write-Host "no log" }
