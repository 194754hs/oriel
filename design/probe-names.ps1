# Lists what each set actually calls the marks Oriel needs. Guessing names and
# reporting a miss as "the set lacks it" would be wrong: usually the name just
# differs. Pure ASCII.
$OrielRoot = (Split-Path -Parent $PSScriptRoot)
$root = "$OrielRoot\design\icon-sets"
$dirs = @{
  lucide   = "$root\lucide\package\icons"
  tabler   = "$root\tabler\package\icons\outline"
  iconoir  = "$root\iconoir\package\icons\regular"
  phosphor = "$root\phosphor\package\assets\light"
}
$index = @{}
foreach ($k in $dirs.Keys) {
  $index[$k] = Get-ChildItem $dirs[$k] -Filter *.svg | ForEach-Object { $_.BaseName }
}
$patterns = @(
  'folder','^file-','photo|image','hard|disk|drive|ssd','cloud','clock|time',
  'monitor|desktop|computer|display','download','star','server|database',
  'chevron|caret|nav-arrow','layout-col|columns|view-col','layout-grid|grid|squares',
  '^list','search|magnif','settings|gear','share','sort|arrows-up-down|arrow-up-down',
  'dots|ellipsis|more','^plus|^add','sidebar|panel','archive|zip','music|audio',
  '^code|terminal','network|wifi','app-window|window'
)
foreach ($p in $patterns) {
  Write-Host ("### " + $p)
  foreach ($k in @('lucide','tabler','iconoir','phosphor')) {
    $m = $index[$k] | Where-Object { $_ -match $p } | Select-Object -First 7
    Write-Host ("  {0,-9} {1}" -f $k, ($m -join ', '))
  }
}
