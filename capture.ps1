# Launches the shell, waits for it to paint, grabs its frame, then closes it.
# Keep pure ASCII (PS 5.1 reads BOM-less .ps1 as ANSI).
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

Add-Type @"
using System;
using System.Runtime.InteropServices;
public struct RECT { public int L, T, R, B; }
public class Win {
    [DllImport("dwmapi.dll")]
    public static extern int DwmGetWindowAttribute(IntPtr h, int a, out RECT r, int s);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(
        IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll", CharSet=CharSet.Unicode)]
    public static extern int GetClassNameW(IntPtr h, System.Text.StringBuilder s, int n);
    [DllImport("user32.dll")]
    public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
}
"@

$OrielRoot = $PSScriptRoot
$exe = "$OrielRoot\build\oriel.exe"
$out = $args[0]
if (-not $out) { $out = "$OrielRoot\build\shot.png" }

if ($args[2]) { $p = Start-Process $exe -ArgumentList "`"$($args[2])`"" -PassThru }
else          { $p = Start-Process $exe -PassThru }
$h = [IntPtr]::Zero
for ($i = 0; $i -lt 120; $i++) {
    $p.Refresh()
    if ($p.HasExited) { throw "process exited early with code $($p.ExitCode)" }
    if ($p.MainWindowHandle -ne [IntPtr]::Zero -and [Win]::IsWindowVisible($p.MainWindowHandle)) {
        $h = $p.MainWindowHandle; break
    }
    Start-Sleep -Milliseconds 50
}
if ($h -eq [IntPtr]::Zero) { $p.Kill(); throw "no window appeared" }

# SetForegroundWindow is refused to background processes, which is how an
# unrelated window ended up in an earlier capture. Topmost is not restricted.
$HWND_TOPMOST = [IntPtr](-1)
[Win]::SetWindowPos($h, $HWND_TOPMOST, 0, 0, 0, 0, 0x0043) | Out-Null  # NOMOVE|NOSIZE|SHOWWINDOW
[Win]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 900     # let DWM settle the acrylic and the first frame land

# prove we are about to photograph the right window
$sb = New-Object System.Text.StringBuilder 256
[Win]::GetClassNameW($h, $sb, 256) | Out-Null
if ($sb.ToString() -ne 'OrielShell') { $p.Kill(); throw "unexpected window class: $($sb.ToString())" }

# optional: click a row first. Coordinates are DIPs - PowerShell is DPI-unaware,
# so Windows already scales what it posts to our per-monitor-aware window.
if ($args[3]) {
    $xy = $args[3] -split ','
    $lp = [IntPtr]((([int]$xy[1]) -shl 16) -bor (([int]$xy[0]) -band 0xFFFF))
    [Win]::PostMessageW($h, 0x0200, [IntPtr]0, $lp) | Out-Null   # WM_MOUSEMOVE
    Start-Sleep -Milliseconds 150
    [Win]::PostMessageW($h, 0x0201, [IntPtr]1, $lp) | Out-Null   # WM_LBUTTONDOWN
    Start-Sleep -Milliseconds 1200
}

# optional: drive the view before shooting, e.g. -keys 40,40,39
if ($args[1]) {
    foreach ($vk in ($args[1] -split ',')) {
        [Win]::PostMessageW($h, 0x0100, [IntPtr][int]$vk, [IntPtr]0) | Out-Null  # WM_KEYDOWN
        Start-Sleep -Milliseconds 220
    }
    Start-Sleep -Milliseconds 500
}

# 9 = DWMWA_EXTENDED_FRAME_BOUNDS: the real visible frame, shadow excluded
$r = New-Object RECT
[Win]::DwmGetWindowAttribute($h, 9, [ref]$r, 16) | Out-Null
$w = $r.R - $r.L; $ht = $r.B - $r.T
if ($w -le 0 -or $ht -le 0) { $p.Kill(); throw "bad window bounds" }

$bmp = New-Object System.Drawing.Bitmap($w, $ht)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.L, $r.T, 0, 0, (New-Object System.Drawing.Size($w, $ht)))
$bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()

$p.CloseMainWindow() | Out-Null
Start-Sleep -Milliseconds 300
if (-not $p.HasExited) { $p.Kill() }

Write-Host ("captured {0}x{1} -> {2}" -f $w, $ht, $out)
