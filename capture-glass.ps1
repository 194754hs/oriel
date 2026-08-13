# Decides whether the sidebar is genuinely transparent: parks a known colour
# behind the window and checks whether the sidebar picks it up while the
# content surface stays exactly the theme token. Pure ASCII.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing, System.Windows.Forms

Add-Type @"
using System;
using System.Runtime.InteropServices;
public struct RECT2 { public int L, T, R, B; }
public class W2 {
    [DllImport("dwmapi.dll")]
    public static extern int DwmGetWindowAttribute(IntPtr h, int a, out RECT2 r, int s);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(
        IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)]
    public static extern int GetClassNameW(IntPtr h, System.Text.StringBuilder s, int n);
}
"@

# a flat, saturated backdrop covering the whole work area
$back = New-Object System.Windows.Forms.Form
$back.FormBorderStyle = 'None'
$back.BackColor = [System.Drawing.Color]::FromArgb(0, 200, 60)   # #00C83C
$back.StartPosition = 'Manual'
$wa = [System.Windows.Forms.Screen]::PrimaryScreen.WorkingArea
$back.Bounds = $wa
$back.Show()
[System.Windows.Forms.Application]::DoEvents()

$OrielRoot = $PSScriptRoot
$p = Start-Process "$OrielRoot\build\oriel.exe" -PassThru
$h = [IntPtr]::Zero
for ($i = 0; $i -lt 120; $i++) {
    $p.Refresh()
    if ($p.HasExited) { $back.Close(); throw "exited early: $($p.ExitCode)" }
    if ($p.MainWindowHandle -ne [IntPtr]::Zero -and [W2]::IsWindowVisible($p.MainWindowHandle)) {
        $h = $p.MainWindowHandle; break
    }
    [System.Windows.Forms.Application]::DoEvents()
    Start-Sleep -Milliseconds 50
}
if ($h -eq [IntPtr]::Zero) { $p.Kill(); $back.Close(); throw "no window" }

$sb = New-Object System.Text.StringBuilder 256
[W2]::GetClassNameW($h, $sb, 256) | Out-Null
if ($sb.ToString() -ne 'OrielShell') { $p.Kill(); $back.Close(); throw "wrong window: $sb" }

[W2]::SetWindowPos($h, [IntPtr](-1), 0, 0, 0, 0, 0x0043) | Out-Null
1..20 | ForEach-Object { [System.Windows.Forms.Application]::DoEvents(); Start-Sleep -Milliseconds 60 }

$r = New-Object RECT2
[W2]::DwmGetWindowAttribute($h, 9, [ref]$r, 16) | Out-Null
$w = $r.R - $r.L; $ht = $r.B - $r.T
$bmp = New-Object System.Drawing.Bitmap($w, $ht)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.L, $r.T, 0, 0, (New-Object System.Drawing.Size($w, $ht)))
$out = "$OrielRoot\build\shot-glass.png"
$bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)

function Px($n, $x, $y) {
    $c = $bmp.GetPixel($x, $y)
    "{0,-26} #{1:X2}{2:X2}{3:X2}" -f $n, $c.R, $c.G, $c.B
}
Px "sidebar upper"  120 500
Px "sidebar mid"    200 900
Px "sidebar lower"   80 1300
Px "content"       1200 900
Px "content right" 2100 1200
Px "title bar"     1200 20
Px "path bar"      1200 1470

$g.Dispose(); $bmp.Dispose()
$p.CloseMainWindow() | Out-Null
Start-Sleep -Milliseconds 300
if (-not $p.HasExited) { $p.Kill() }
$back.Close(); $back.Dispose()
Write-Host "saved $out"
