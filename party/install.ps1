# The Curse of Alduin - Party Pack installer. Installs The Curse of Alduin and Skyrim Together Reborn into Skyrim Special Edition,
# turns the plugins on, sets your co-op role, and puts a "The Curse of Alduin Co-op" shortcut on the desktop.
param([ValidateSet('client', 'host')][string]$Role = 'client')
$ErrorActionPreference = 'Stop'
$pack = Split-Path -Parent $PSScriptRoot
function Say($t, $c = 'Gray') { Write-Host $t -ForegroundColor $c }

Say "`n  THE CURSE OF ALDUIN - PARTY PACK ($Role)`n" Yellow

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
if ($ver -notlike '1.7.104*') { Say "The Curse of Alduin is built for Skyrim 1.7.104 (the current Steam version). Update Skyrim in Steam if it doesn't start." Yellow }

# ---- copy the files
$data = Join-Path $game 'Data'
# older packs named the game files BO3Zombies / UndyingNights .asi and .dll: two copies must never load together
foreach ($old in (Join-Path $game 'BO3Zombies.asi'), (Join-Path $data 'SKSE\Plugins\BO3Zombies.dll'), (Join-Path $game 'UndyingNights.asi'), (Join-Path $data 'SKSE\Plugins\UndyingNights.dll')) {
    if (Test-Path $old) { Remove-Item $old -Force; Say "Removed the old $(Split-Path -Leaf $old)" }
}
Say 'Copying The Curse of Alduin and SKSE (Skyrim Together starts SKSE, and SKSE loads The Curse of Alduin and the mods)...'
Copy-Item (Join-Path $pack 'files\Game\*') $game -Recurse -Force
Say 'Copying Skyrim Together Reborn...'
Copy-Item (Join-Path $pack 'files\SkyrimTogether\*') $data -Recurse -Force
# ---- the party's gameplay mods (required: both players need the same plugins): the spell mods the Coffer draws
#      from, Ordinator, animations and movement (Open Animation Replacer, True Directional Movement, Goetia, ...),
#      spell effects and sounds. Already installed and sorted in files\Mods\Data.
$mods = Join-Path $pack 'files\Mods\Data'
if (Test-Path $mods) {
    Say 'Copying the party mods (spells, perks, animations, effects, sounds)...'
    robocopy $mods $data /E /MT:16 /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -ge 8) { Say 'Some mod files could not be copied (is the disk full?).' Red } else { Say 'Party mods installed.' Green }
}
# TK Dodge's old DLLs do not run on this Skyrim and stop it starting
foreach ($old in 'TKPlugin.dll', 'TK_Dodge_RE.dll') { $f = Join-Path $data "SKSE\Plugins\$old"; if (Test-Path $f) { Remove-Item $f -Force } }
Say 'Copying The Curse of Alduin data (last, so its menu art wins)...'
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
Set-Content -Path (Join-Path $game 'CurseOfAlduin.ini') -Value "[Coop]`r`n; host = your game raises the nights; client = you join the host's nights`r`nRole=$Role" -Encoding ASCII

# ---- plugins on (Skyrim Together first, The Curse of Alduin last)
$pdir = Join-Path $env:LOCALAPPDATA 'Skyrim Special Edition'
New-Item -ItemType Directory -Force -Path $pdir | Out-Null
$plist = Join-Path $pdir 'Plugins.txt'
$lines = @(); if (Test-Path $plist) { $lines = Get-Content $plist }
# the graphics plugins, in the host's load order (each needs the ones before it)
$gfxPlugins = @('RealisticWaterTwo - Resources.esm', 'Lux Orbis - Master plugin.esm', 'Lux - Resources.esp', 'Lux Via.esp', 'SMIM-SE-Merged-All.esp',
    'Lit Road Signs.esp', 'HappyLittleTrees.esp', 'RealisticWaterTwo.esp', 'Skyland Watercolor - Slow Green-RW2.esp',
    'Azurite Weathers III.esp', 'Lux Orbis.esp', 'Lux Via - plugin.esp', 'Lux Via - Blended Roads Bridges Texture sets.esp',
    'Lux - Master plugin.esm', 'Lux.esp', 'Lux - No grass in caves patch.esp', 'Lux - USSEP patch.esp', 'IcyFixes.esp', 'IcyCaveRemaster.esp', 'IcyBiomeRemaster.esp',
    'TamrielicGrass.esp', 'Veydosebrom Regions.esp', 'Folkvangr - Grass and Landscape Overhaul.esp', 'Seasonal Landscapes.esp', 'Short Grass Mixed.esp',
    'Cathedral - 3D Pine Grass.esp', 'Origins Of Forest - 3D Forest Grass.esp', 'Landscape Fixes For Grass Mods.esp', 'SnowOverSkyrim.esp')
# the fixes every other plugin builds on (loaded first)
$basePlugins = @('Unofficial Skyrim Special Edition Patch.esp')
# the party mods' plugins, in load order (masters first)
$modPlugins = @('Ordinator - Perks of Skyrim.esp', 'Apocalypse - Magic of Skyrim.esp', 'MysticismMagic.esp', 'MysticOrdinator.esp', 'ForgottenMagic_Redone.esp',
    'StormCalling.esl', 'Conduit.esp', 'StrangeRunes.esp', 'EnhancedAtronachs.esp', 'BURP_SpellTomes.esp', 'BURPST_Apocalypse.esp', 'BURPST_ForgottenMagic.esp',
    'BURPST_MysticismMO.esp', 'DeadlySpellImpacts.esp', 'FEC.esp', 'Embers XD.esp', 'Embers XD - Fire Magick Add-On.esp', 'ENB Light.esp', 'TKDodge.esp',
    'TKDodgeSE sound.esp', 'TrueDirectionalMovement.esp', 'Spell Sword.esp', '90fps.esp', 'sirwho_Wizard_Hats-Full.esp',
    'Sentinel - Master Plugin.esp', 'SkyUI_SE.esp', 'MCMHelper.esp', 'The Restless Dead.esp', 'mihailesobonecolossus.esp', 'mihailoldgod.esp',
    'mihailgiantfamily.esp', 'mihailfogling.esp', 'mihailhighfantasypack.esp', 'DiverseDragonsCollectionSE.esp', 'GhostLight.esp',
    'Thunderchild - Epic Shout Package.esp', 'Thundering Shouts.esp', 'Sentinel.esp', 'Sentinel - Priests and Acolytes.esp', 'Sentinel - City Guards.esp',
    'SkyParkour.esp', 'Character Menu SE.esp', 'Paragliding.esp',
    'ArteFake.esp', 'Beam Walk.esp', 'SpiderAnim.esl', 'Unique Jarl Throne Sitting Animation.esp', 'MikanEyes All in one SE.esp', "Freak's Floral Fields.esp",
    'Faithful Faces.esp', 'Faithful Faces0.esp', 'FabulousFollowersAIO.esp', "The Wolven Widow - SerketHetyt's Elisif Overhaul.esp",
    "Evigheden - SerketHetyt's Serana Overhaul.esp", 'RaySense.esp', 'YsoldaDF.esp', "Jordis of Solitude - SerketHetyt's Housecarls.esp",
    'CollegeOfWinterholdImmersive.esp', 'FabFollowers_ImmCoW_Patch.esp')
# plugins no longer used (an older pack turned them on): kept off
$retired = @('PAN_NPCs.esp', 'Phantom Horse.esp', 'Skoglendi - A Grass Mod.esp', 'Nolvus Ascension Seasonal Grass Mashup.esp')
$ours = @('SkyrimTogether.esp', 'SkyrimTogetherQuestPatches.esp', 'BO3Zombies.esp', 'UndyingNights.esp', 'CurseOfAlduin.esp') + $basePlugins + $gfxPlugins + $modPlugins + $retired
$keep = $lines | Where-Object { $n = ($_ -replace '^\*', '').Trim(); -not ($ours -contains $n) }
$mid = @()
if ($gfxOn -or (Test-Path (Join-Path $data 'Lux Via.esp'))) { $mid = $gfxPlugins | Where-Object { Test-Path (Join-Path $data $_) } | ForEach-Object { '*' + $_ } }
$out = @($keep | Where-Object { $_ -match '^\s*#' }) + @('*SkyrimTogether.esp', '*SkyrimTogetherQuestPatches.esp') + @($basePlugins | Where-Object { Test-Path (Join-Path $data $_) } | ForEach-Object { '*' + $_ }) + @($keep | Where-Object { $_ -notmatch '^\s*#' -and $_.Trim() }) + $mid + @($modPlugins | Where-Object { Test-Path (Join-Path $data $_) } | ForEach-Object { '*' + $_ }) + @('*CurseOfAlduin.esp')
[IO.File]::WriteAllLines($plist, [string[]]$out)  # (no byte-order mark: Skyrim would not read the first plugin's name)
Say 'Plugins turned on.' Green

# ---- controllers (Xbox, and PS5 through Steam Input / DualSenseX / DS4Windows): Skyrim's gamepad support on
$prefs = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'My Games\Skyrim Special Edition\SkyrimPrefs.ini'
if (Test-Path $prefs) {
    $t = [IO.File]::ReadAllLines($prefs)
    if ($t -match '^\s*bGamepadEnable\s*=') {
        $t = $t -replace '^\s*bGamepadEnable\s*=.*', 'bGamepadEnable=1'
    } elseif ($t -match '^\[Controls\]') {
        $i = [Array]::IndexOf($t, ($t | Where-Object { $_ -match '^\[Controls\]' } | Select-Object -First 1))
        $rest = if ($i -lt $t.Length - 1) { @($t[($i + 1)..($t.Length - 1)]) } else { @() }
        $t = @($t[0..$i]) + 'bGamepadEnable=1' + $rest
    } else {
        $t = @($t) + '[Controls]' + 'bGamepadEnable=1'
    }
    [IO.File]::WriteAllLines($prefs, [string[]]$t)
    Say 'Controller support on (Xbox or PS5 pads work in game).' Green
}

# ---- the party's address and password
$party = @{}
Get-Content (Join-Path $pack 'Party.txt') | ForEach-Object { if ($_ -match '^\s*(\w+)\s*=\s*(.*)$') { $party[$Matches[1]] = $Matches[2].Trim() } }
$str = Join-Path $data 'SkyrimTogetherReborn'

if ($Role -eq 'host') {
    $ini = Join-Path $str 'config\STServer.ini'
    $t = Get-Content $ini
    $t = $t -replace '^uTimeScale=.*', 'uTimeScale=58'                              # The Curse of Alduin's day: 15 minutes of day, 10 of night
    $t = $t -replace '^sPassword=.*', ('sPassword=' + $party['Password'])
    $t = $t -replace '^sServerName=.*', 'sServerName=The Curse of Alduin'
    $t = $t -replace '^bAutoPartyJoin=.*', 'bAutoPartyJoin=true'
    $t = $t -replace '^bEnableDeathSystem=.*', 'bEnableDeathSystem=true'
    Set-Content $ini $t
    Say 'Server set up (30-minute days, your password).' Green
}

# ---- desktop shortcut
$ws = New-Object -ComObject WScript.Shell
$lnk = $ws.CreateShortcut((Join-Path ([Environment]::GetFolderPath('Desktop')) 'The Curse of Alduin Co-op.lnk'))
$lnk.TargetPath = Join-Path $str 'SkyrimTogether.exe'
$lnk.WorkingDirectory = $str
$lnk.Save()
if ($Role -eq 'host') {
    $lnk2 = $ws.CreateShortcut((Join-Path ([Environment]::GetFolderPath('Desktop')) 'The Curse of Alduin Server.lnk'))
    $lnk2.TargetPath = Join-Path $str 'SkyrimTogetherServer.exe'
    $lnk2.WorkingDirectory = $str
    $lnk2.Save()
}
if (-not (Test-Path (Join-Path $game 'skse64_1_7_104.dll'))) { Say "SKSE did not copy - The Curse of Alduin will not load. Run this again (as administrator if Skyrim is under Program Files)." Red }
if ($ver -notlike '1.7.104*') { Say "Your Skyrim is $ver - this pack's SKSE and The Curse of Alduin need 1.7.104 (Steam, kept up to date). Update Skyrim in Steam, then start from the shortcut." Red }
Say "`nDone! Shortcut 'The Curse of Alduin Co-op' is on your desktop.`n" Yellow
Say "Always start the game from that shortcut (not Steam's Play button)." Cyan
if ($Role -eq 'host') {
    Say "In the Curse of Alduin main menu: CO-OP PARTY > HOST A PARTY, then invite your friend in the Steam overlay." Cyan
} else {
    Say "Wait in the Curse of Alduin main menu, then accept the host's invite there (Shift+Tab) or choose CO-OP PARTY > JOIN <host>." Cyan
}
Read-Host "`nPress Enter to close"
