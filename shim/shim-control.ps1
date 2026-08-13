# Turns the dialog interception on and off. Per-user only: nothing here needs
# elevation and nothing here touches HKLM, so the genuine registration always
# survives underneath. Keep pure ASCII.
#
#   shim-control.ps1 status
#   shim-control.ps1 on
#   shim-control.ps1 off
#   shim-control.ps1 bypass    (leaves registration in place, takes us out of the chain)
#   shim-control.ps1 unbypass

$ErrorActionPreference = 'Stop'

$CLSIDS = @{
    'FileOpenDialog' = '{DC1C5A9C-E88A-4dde-A5A1-60F82A20AEF7}'
    'FileSaveDialog' = '{C0B4E2F3-BA21-4773-8DBA-335EC946EB8B}'
}
# Prefer the release build: the debug one drags in the debug CRT, which has no
# business being loaded into other applications.
$OrielRoot = (Split-Path -Parent $PSScriptRoot)
$REL = "$OrielRoot\build-rel\oriel_dialog.dll"
$DBG = "$OrielRoot\build\oriel_dialog.dll"
$DLL = if (Test-Path $REL) { $REL } else { $DBG }
$OFF = (Split-Path $DLL) + '\oriel-shim.off'
$ROOT = 'HKCU:\Software\Classes\CLSID'
# 32-bit processes on 64-bit Windows read their per-user CLSID overrides from the
# Wow6432Node branch, and a 64-bit DLL cannot load into them, so the x86 shim is
# a separate build registered in a separate place.
$X86 = "$OrielRoot\build-x86\oriel_dialog.dll"
$ROOT32 = 'HKCU:\Software\Classes\Wow6432Node\CLSID'

function Show-Status {
    Write-Host "dll      : $DLL"
    Write-Host "          $(if (Test-Path $DLL) { 'present' } else { 'MISSING - build first' })"
    Write-Host "bypass   : $(if (Test-Path $OFF) { 'ON  (shim is out of the chain)' } else { 'off' })"
    foreach ($name in $CLSIDS.Keys) {
        $k = "$ROOT\$($CLSIDS[$name])\InprocServer32"
        if (Test-Path $k) {
            $v = (Get-ItemProperty $k).'(default)'
            Write-Host ("{0,-15}: intercepted -> {1}" -f $name, $v)
        } else {
            Write-Host ("{0,-15}: genuine (no per-user override)" -f $name)
        }
    }
    Write-Host ""
    Write-Host "x86 dll  : $X86"
    Write-Host "          $(if (Test-Path $X86) { 'present' } else { 'MISSING - run build-x86.cmd' })"
    foreach ($name in $CLSIDS.Keys) {
        $k = "$ROOT32\$($CLSIDS[$name])\InprocServer32"
        if (Test-Path $k) {
            $v = (Get-ItemProperty $k).'(default)'
            Write-Host ("{0,-15}: intercepted (32-bit) -> {1}" -f $name, $v)
        } else {
            Write-Host ("{0,-15}: genuine (32-bit apps)" -f $name)
        }
    }
    Write-Host ""
    Write-Host "note: elevated processes read the administrator's HKCU, and store"
    Write-Host "      apps go through a broker, so neither is intercepted."
}

switch ($args[0]) {
    'on' {
        if (-not (Test-Path $DLL)) { throw "build oriel_dialog.dll first" }
        foreach ($name in $CLSIDS.Keys) {
            $k = "$ROOT\$($CLSIDS[$name])\InprocServer32"
            New-Item -Path $k -Force | Out-Null
            New-ItemProperty -Path $k -Name '(default)'    -Value $DLL       -PropertyType String -Force | Out-Null
            New-ItemProperty -Path $k -Name 'ThreadingModel' -Value 'Apartment' -PropertyType String -Force | Out-Null
            Write-Host "intercepting $name (64-bit)"
            if (Test-Path $X86) {
                $k32 = "$ROOT32\$($CLSIDS[$name])\InprocServer32"
                New-Item -Path $k32 -Force | Out-Null
                New-ItemProperty -Path $k32 -Name '(default)'      -Value $X86         -PropertyType String -Force | Out-Null
                New-ItemProperty -Path $k32 -Name 'ThreadingModel' -Value 'Apartment'  -PropertyType String -Force | Out-Null
                Write-Host "intercepting $name (32-bit)"
            }
        }
        Write-Host ""
        Show-Status
    }
    'off' {
        foreach ($name in $CLSIDS.Keys) {
            $k = "$ROOT\$($CLSIDS[$name])"
            if (Test-Path $k) { Remove-Item $k -Recurse -Force; Write-Host "released $name (64-bit)" }
            else { Write-Host "$name was not intercepted" }
            $k32 = "$ROOT32\$($CLSIDS[$name])"
            if (Test-Path $k32) { Remove-Item $k32 -Recurse -Force; Write-Host "released $name (32-bit)" }
        }
        Write-Host ""
        Show-Status
    }
    'bypass' {
        Set-Content $OFF 'shim disabled' -Encoding ASCII
        Write-Host "bypass file written; the next dialog will use the genuine implementation"
    }
    'unbypass' {
        if (Test-Path $OFF) { Remove-Item $OFF }
        Write-Host "bypass cleared"
    }
    default { Show-Status }
}
