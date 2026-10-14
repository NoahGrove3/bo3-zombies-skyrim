# Undying Nights: install the party's new mods (plan.json from modplan.py) at full disk speed.
#  - moves the Party Pack from Downloads to D:\Undying Nights - Party Pack (room for the bigger pack)
#  - each archive is unpacked with Windows' own tar.exe, its planned files copied into the Party Pack
#    (files\Mods\Data for what every player needs, files\Graphics\Data for looks) and into the game's Data
# Progress: D:\un_tmp\un_mods.log
$ErrorActionPreference = 'Continue'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$game = Split-Path -Parent $here                       # ...\Skyrim Special Edition
$gameData = Join-Path $game 'Data'
$dl = Join-Path $env:USERPROFILE 'Downloads'
$tmp = 'D:\un_tmp'
$pack = 'D:\Undying Nights - Party Pack'
New-Item -ItemType Directory -Force $tmp | Out-Null
$log = Join-Path $tmp 'un_mods.log'
function Log($m) { $line = "$(Get-Date -Format HH:mm:ss) $m"; Add-Content -Path $log -Value $line; Write-Host $line }
Log "=== start"

# the Party Pack moves to D: (its zip is rebuilt there afterwards)
$oldPack = Join-Path $dl 'Undying Nights - Party Pack'
if ((Test-Path $oldPack) -and -not (Test-Path (Join-Path $pack 'files'))) {
    Log "moving the Party Pack to $pack"
    robocopy $oldPack $pack /E /MOVE /R:1 /W:1 /NFL /NDL /NJH /NJS /NP | Out-Null
    Log "moved"
}
$targets = @{ 'mods' = (Join-Path $pack 'files\Mods\Data'); 'graphics' = (Join-Path $pack 'files\Graphics\Data') }
foreach ($t in $targets.Values) { New-Item -ItemType Directory -Force $t | Out-Null }

$plan = Get-Content (Join-Path $here 'plan.json') -Raw | ConvertFrom-Json
$only = $args
foreach ($m in $plan.mods) {
    if ($only.Count -gt 0 -and -not ($only -contains $m.name)) { continue }
    $done = Join-Path $tmp ("done_" + ($m.name -replace '[^A-Za-z0-9]', '_'))
    if (Test-Path $done) { Log "skip (done) $($m.name)"; continue }
    $arch = Join-Path $dl $m.archive
    if (-not (Test-Path -LiteralPath $arch)) { Log "MISSING archive $($m.archive)"; continue }
    $x = Join-Path $tmp 'x'
    $stage = Join-Path $tmp 'stage'
    foreach ($d in @($x, $stage)) { if (Test-Path $d) { Remove-Item -LiteralPath $d -Recurse -Force }; New-Item -ItemType Directory -Force $d | Out-Null }
    Log "unpack $($m.name)"
    & tar.exe -xf $arch -C $x 2>&1 | Select-Object -First 3 | ForEach-Object { Log "  tar: $_" }
    foreach ($op in $m.ops) {
        $src = Join-Path $x ($op[0] -replace '/', '\')
        $dst = if ($op[1]) { Join-Path $stage ($op[1] -replace '/', '\') } else { $stage }
        if ($op[2]) {
            if (Test-Path -LiteralPath $src) { robocopy $src $dst /E /XD fomod /XF *.jpg *.jpeg readme* *.pdb meta.ini *.url *.html /R:1 /W:1 /NFL /NDL /NJH /NJS /NP | Out-Null }
            else { Log "  !! missing $($op[0])" }
        } else {
            if (Test-Path -LiteralPath $src) { New-Item -ItemType Directory -Force (Split-Path -Parent $dst) | Out-Null; Copy-Item -LiteralPath $src $dst -Force }
            else { Log "  !! missing $($op[0])" }
        }
    }
    $n = (Get-ChildItem -LiteralPath $stage -Recurse -File | Measure-Object).Count
    robocopy $stage $targets[$m.target] /E /R:1 /W:1 /NFL /NDL /NJH /NJS /NP | Out-Null
    robocopy $stage $gameData /E /R:1 /W:1 /NFL /NDL /NJH /NJS /NP | Out-Null
    Remove-Item -LiteralPath $x -Recurse -Force
    Remove-Item -LiteralPath $stage -Recurse -Force
    Set-Content -Path $done -Value $n
    Log "done $($m.name): $n files -> $($m.target) + game"
}
Log "=== all done"
