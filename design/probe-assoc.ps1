# Runs the same rule the renderer uses, so what shows up in the window can be
# predicted instead of guessed at. Pure ASCII.
$ErrorActionPreference = 'Continue'
Add-Type @"
using System; using System.Text; using System.Runtime.InteropServices;
public class A2 {
  [DllImport("shlwapi.dll", CharSet=CharSet.Unicode)]
  public static extern int AssocQueryStringW(int flags,int str,string assoc,string extra,
                                             StringBuilder outBuf,ref uint cch);
}
"@
$win = $env:SystemRoot
$exts = 'blend','psd','ai','aep','prproj','indd','xd','sketch','fig',
        'docx','xlsx','pptx','pdf','zip','7z','rar','txt','md','json','cs','cpp',
        'py','js','ts','html','css','png','jpg','svg','mp4','mov','mp3','wav',
        'exe','lnk','ico','msi','blend1','c4d','max','ma','mb','fbx','obj','stl',
        'unity','uproject','sln','csproj','xcodeproj','db','sqlite','iso','ttf'
$rows = @()
foreach ($e in $exts) {
  $sb = New-Object System.Text.StringBuilder 1024
  [uint32]$n = 1024
  $hr = [A2]::AssocQueryStringW(0x40, 15, ".$e", $null, $sb, [ref]$n)   # NOTRUNCATE, DEFAULTICON
  if ($hr -ne 0) { $rows += [pscustomobject]@{ ext=$e; verdict='glyph'; icon='(none registered)' }; continue }
  $raw = $sb.ToString()
  $mod = $raw
  $c = $mod.LastIndexOf(',')
  if ($c -ge 0) { $mod = $mod.Substring(0,$c) }
  $mod = $mod.Trim('"')
  if ($mod -eq '%1') { $rows += [pscustomobject]@{ ext=$e; verdict='FILE ICON'; icon=$raw }; continue }
  $mod = [Environment]::ExpandEnvironmentVariables($mod)
  $verdict = if ($mod.StartsWith($win, [StringComparison]::OrdinalIgnoreCase)) { 'glyph' } else { 'APP ICON' }
  $rows += [pscustomobject]@{ ext=$e; verdict=$verdict; icon=$raw }
}
$rows | Where-Object { $_.verdict -ne 'glyph' } | Format-Table -AutoSize
Write-Host ("app/file icons: {0} of {1}" -f ($rows | Where-Object { $_.verdict -ne 'glyph' }).Count, $rows.Count)
