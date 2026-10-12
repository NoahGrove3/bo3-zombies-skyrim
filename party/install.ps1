# Undying Nights - Party Pack installer. Installs Undying Nights and Skyrim Together Reborn into Skyrim Special Edition,
# turns the plugins on, sets your co-op role, and puts a "Undying Nights Co-op" shortcut on the desktop.
param([ValidateSet('client', 'host')][string]$Role = 'client')
$ErrorActionPreference = 'Stop'
$pack = Split-Path -Parent $PSScriptRoot
function Say($t, $c = 'Gray') { Write-Host $t -ForegroundColor $c }

Say "`n  UNDYING NIGHTS - PARTY PACK ($Role)`n" Yellow

# ---- find Skyrim Special Edition
$game = $null
foreach ($k in 'HKLM:\SOFTWARE\WOW6432Node\Bethesda Softworks\Skyrim Special Edition', 'HKLM:\SOFTWARE\Bethesda Softworks\Skyrim Special Edition') {
    try { $p = (Get-ItemProperty $k -ErrorAction Stop).'installed path'; if ($p -and (Test-Path (Join-Path $p 'SkyrimSE.exe'))) { $game = $p; break } } catch {}
}
if (-not $game) {
    $steam = $null
    try { $steam = (Get-ItemProperty 'HKCU:\Software\Valve\Steam' -ErrorAction Stop).SteamPath } catch {}
    $libs = @()
    if ($steam) {
        $libs += $steam
        $vdf = Join-Path $steam 'steamapps\libraryfolders.vdf'
        if (Test-Path $vdf) { $libs += (Select-String -Path $vdf -Pattern '"path"\s+"([^"]+)"' | ForEach-Object { $_.Matches[0].Groups[1].Value -replace '\\\\', '\' }) }
    }
    foreach ($l in $libs) { $c = Join-Path $l 'steamapps\common\Skyrim Special Edition'; if (Test-Path (Join-Path $c 'SkyrimSE.exe')) { $game = $c; break } }
}
if (-not $game) {
    Add-Type -AssemblyName System.Windows.Forms
    $d = New-Object System.Windows.Forms.FolderBrowserDialog
    $d.Description = 'Pick your Skyrim Special Edition folder (the one with SkyrimSE.exe)'
    if ($d.ShowDialog() -eq 'OK' -and (Test-Path (Join-Path $d.SelectedPath 'SkyrimSE.exe'))) { $game = $d.SelectedPath }
}
if (-not $game) { Say "Couldn't find Skyrim Special Edition. Install it from Steam, run it once, then run this again." Red; Read-Host 'Press Enter'; exit 1 }
Say "Skyrim: $game" Green
if (Get-Process SkyrimSE -ErrorAction SilentlyContinue) { Say "Close Skyrim first, then run this again." Red; Read-Host 'Press Enter'; exit 1 }
$ver = (Get-Item (Join-Path $game 'SkyrimSE.exe')).VersionInfo.FileVersion
Say "Skyrim version: $ver"
if ($ver -notlike '1.7.104*') { Say "Undying Nights is built for Skyrim 1.7.104 (the current Steam version). Update Skyrim in Steam if it doesn't start." Yellow }

# ---- copy the files
$data = Join-Path $game 'Data'
# older packs named the game files BO3Zombies.asi / .dll: two copies must never load together
foreach ($old in (Join-Path $game 'BO3Zombies.asi'), (Join-Path $data 'SKSE\Plugins\BO3Zombies.dll')) {
    if (Test-Path $old) { Remove-Item $old -Force; Say "Removed the old $(Split-Path -Leaf $old)" }
}
Say 'Copying Undying Nights...'
Copy-Item (Join-Path $pack 'files\Game\*') $game -Recurse -Force
Say 'Copying Skyrim Together Reborn...'
Copy-Item (Join-Path $pack 'files\SkyrimTogether\*') $data -Recurse -Force
Say 'Copying Undying Nights data (last, so its menu art wins)...'
Copy-Item (Join-Path $pack 'files\Data\*') $data -Recurse -Force

# ---- Noah's graphics (optional, the same look as the host: weathers, lighting, roads, water, trees, grass,
#      armour (Cathedral), NPC faces (Pandorable)). Everything is in files\Graphics\Data, already installed and sorted.
$gfx = Join-Path $pack 'files\Graphics\Data'
$gfxOn = $false
if (Test-Path $gfx) {
    $ans = Read-Host "`nAlso install Noah's graphics mods (about 16 GB: the same look as the host)? [Y/n]"
    if ($ans -notmatch '^\s*[nN]') {
        Say 'Copying the graphics mods (this takes a few minutes)...'
        robocopy $gfx $data /E /MT:16 /NFL /NDL /NJH /NJS /NP | Out-Null
        if ($LASTEXITCODE -ge 8) { Say 'Some graphics files could not be copied (is the disk full?).' Red } else { $gfxOn = $true; Say 'Graphics mods installed.' Green }
    }
}

# ---- co-op role
Set-Content -Path (Join-Path $game 'UndyingNights.ini') -Value "[Coop]`r`n; host = your game raises the nights; client = you join the host's nights`r`nRole=$Role" -Encoding ASCII

# ---- plugins on (Skyrim Together first, Undying Nights last)
$pdir = Join-Path $env:LOCALAPPDATA 'Skyrim Special Edition'
New-Item -ItemType Directory -Force -Path $pdir | Out-Null
$plist = Join-Path $pdir 'Plugins.txt'
$lines = @(); if (Test-Path $plist) { $lines = Get-Content $plist }
# the graphics plugins, in the host's load order (each needs the ones before it)
$gfxPlugins = @('RealisticWaterTwo - Resources.esm', 'Lux Orbis - Master plugin.esm', 'Lux - Resources.esp', 'Lux Via.esp', 'SMIM-SE-Merged-All.esp',
    'Lit Road Signs.esp', 'HappyLittleTrees.esp', 'Skoglendi - A Grass Mod.esp', 'RealisticWaterTwo.esp', 'Skyland Watercolor - Slow Green-RW2.esp',
    'Azurite Weathers III.esp', 'Lux Orbis.esp', 'Lux Via - plugin.esp', 'Lux Via - Blended Roads Bridges Texture sets.esp', 'PAN_NPCs.esp')
$ours = @('SkyrimTogether.esp', 'SkyrimTogetherQuestPatches.esp', 'BO3Zombies.esp', 'UndyingNights.esp') + $gfxPlugins
$keep = $lines | Where-Object { $n = ($_ -replace '^\*', '').Trim(); -not ($ours -contains $n) }
$mid = @()
if ($gfxOn -or (Test-Path (Join-Path $data 'Lux Via.esp'))) { $mid = $gfxPlugins | Where-Object { Test-Path (Join-Path $data $_) } | ForEach-Object { '*' + $_ } }
$out = @($keep | Where-Object { $_ -match '^\s*#' }) + @('*SkyrimTogether.esp', '*SkyrimTogetherQuestPatches.esp') + @($keep | Where-Object { $_ -notmatch '^\s*#' -and $_.Trim() }) + $mid + @('*BO3Zombies.esp')
Set-Content -Path $plist -Value $out -Encoding UTF8
Say 'Plugins turned on.' Green

# ---- the party's address and password
$party = @{}
Get-Content (Join-Path $pack 'Party.txt') | ForEach-Object { if ($_ -match '^\s*(\w+)\s*=\s*(.*)$') { $party[$Matches[1]] = $Matches[2].Trim() } }
$str = Join-Path $data 'SkyrimTogetherReborn'

if ($Role -eq 'host') {
    $ini = Join-Path $str 'config\STServer.ini'
    $t = Get-Content $ini
    $t = $t -replace '^uTimeScale=.*', 'uTimeScale=58'                              # Undying Nights' day: 15 minutes of day, 10 of night
    $t = $t -replace '^sPassword=.*', ('sPassword=' + $party['Password'])
    $t = $t -replace '^sServerName=.*', 'sServerName=Undying Nights'
    $t = $t -replace '^bAutoPartyJoin=.*', 'bAutoPartyJoin=true'
    $t = $t -replace '^bEnableDeathSystem=.*', 'bEnableDeathSystem=true'
    Set-Content $ini $t
    Say 'Server set up (30-minute days, your password).' Green
}

# ---- desktop shortcut
$ws = New-Object -ComObject WScript.Shell
$lnk = $ws.CreateShortcut((Join-Path ([Environment]::GetFolderPath('Desktop')) 'Undying Nights Co-op.lnk'))
$lnk.TargetPath = Join-Path $str 'SkyrimTogether.exe'
$lnk.WorkingDirectory = $str
$lnk.Save()
if ($Role -eq 'host') {
    $lnk2 = $ws.CreateShortcut((Join-Path ([Environment]::GetFolderPath('Desktop')) 'Undying Nights Server.lnk'))
    $lnk2.TargetPath = Join-Path $str 'SkyrimTogetherServer.exe'
    $lnk2.WorkingDirectory = $str
    $lnk2.Save()
}
Say "`nDone! Shortcut 'Undying Nights Co-op' is on your desktop.`n" Yellow
Say "Always start the game from that shortcut (not Steam's Play button)." Cyan
if ($Role -eq 'host') {
    Say "In the Undying Nights main menu: CO-OP PARTY > HOST A PARTY - INVITE FRIENDS, invite your friend in the Steam overlay." Cyan
} else {
    Say "Wait in the Undying Nights main menu, then accept the host's invite there (Shift+Tab) or click JOIN <host> in the CO-OP PARTY panel." Cyan
}
Read-Host "`nPress Enter to close"
