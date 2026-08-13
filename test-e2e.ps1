# End to end: a plain COM application asks for a save dialog, Oriel's picker
# answers, and the path comes back through the shim. Pure ASCII.
$ErrorActionPreference = 'Stop'
$OrielRoot = $PSScriptRoot
$root = "$OrielRoot"
$rel  = "$root\build-rel"

Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class E {
  public delegate bool EnumProc(IntPtr h, IntPtr p);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr p);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern int GetWindowThreadProcessId(IntPtr h, out int pid);
}
"@

function Get-OrielWindows {
    $found = New-Object System.Collections.ArrayList
    $cb = [E+EnumProc]{
        param($h, $p)
        $sb = New-Object System.Text.StringBuilder 64
        [E]::GetClassNameW($h, $sb, 64) | Out-Null
        if ($sb.ToString() -eq 'OrielShell' -and [E]::IsWindowVisible($h)) { $null = $found.Add($h) }
        return $true
    }
    [E]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
    return $found
}

function Send-Click([IntPtr]$h, [int]$x, [int]$y) {
    # DIPs: PowerShell is DPI-unaware so Windows scales what it posts for us
    $lp = [IntPtr](($y -shl 16) -bor ($x -band 0xFFFF))
    [E]::PostMessageW($h, 0x0200, [IntPtr]0, $lp) | Out-Null   # WM_MOUSEMOVE
    Start-Sleep -Milliseconds 120
    [E]::PostMessageW($h, 0x0201, [IntPtr]1, $lp) | Out-Null   # WM_LBUTTONDOWN
    Start-Sleep -Milliseconds 250
}

Get-Process oriel -ErrorAction SilentlyContinue | ForEach-Object { $_.Kill() }
Start-Sleep -Milliseconds 400
if (Test-Path "$rel\oriel-shim.log") { Remove-Item "$rel\oriel-shim.log" }
if (Test-Path "$rel\oriel.log")      { Remove-Item "$rel\oriel.log" }

Write-Host "starting resident Oriel..."
$oriel = Start-Process "$rel\oriel.exe" -ArgumentList "`"$rel`"" -PassThru
for ($i = 0; $i -lt 60; $i++) {
    Start-Sleep -Milliseconds 100
    if ((Get-OrielWindows).Count -ge 1) { break }
}
$before = Get-OrielWindows
Write-Host "resident windows: $($before.Count)"

Write-Host "a plain COM app asks for a save dialog..."
$out = "$env:TEMP\oriel-e2e.txt"
$app = Start-Process "$rel\shim_test.exe" -ArgumentList "pick" -PassThru `
        -RedirectStandardOutput $out -NoNewWindow

# wait for the picker to appear as a second window of our class
$picker = [IntPtr]::Zero
for ($i = 0; $i -lt 100; $i++) {
    Start-Sleep -Milliseconds 100
    $now = Get-OrielWindows
    if ($now.Count -gt $before.Count) {
        foreach ($h in $now) { if ($before -notcontains $h) { $picker = $h; break } }
        if ($picker -ne [IntPtr]::Zero) { break }
    }
}

if ($picker -eq [IntPtr]::Zero) {
    Write-Host "NO PICKER APPEARED - the shim should have fallen back"
} else {
    Write-Host "picker window found; driving it"
    Send-Click $picker 298 95        # first row of the first column
    Send-Click $picker 298 121       # second row, likely a file
    Send-Click $picker 1116 731      # the confirm button
}

$app.WaitForExit(15000) | Out-Null
if (-not $app.HasExited) { $app.Kill(); Write-Host "app did not finish; killed" }

Write-Host "--- app output ---"
if (Test-Path $out) { Get-Content $out } else { Write-Host "(none)" }
Write-Host "--- shim log ---"
if (Test-Path "$rel\oriel-shim.log") { Get-Content "$rel\oriel-shim.log" } else { Write-Host "(none)" }
Write-Host "--- oriel log ---"
if (Test-Path "$rel\oriel.log") { Get-Content "$rel\oriel.log" | Select-Object -Last 6 } else { Write-Host "(none)" }

if (-not $oriel.HasExited) { $oriel.Kill() }
