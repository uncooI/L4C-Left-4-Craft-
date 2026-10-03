param([string]$GameDirectory)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
. "$PSScriptRoot/ensure-java.ps1"
Initialize-Left4CraftJava -Root $root
Set-Left4CraftIniValues -Path "$root/Minecraft/Prism/instances/Left4Craft/instance.cfg" -Values @{
    OverrideJavaArgs = 'true'
    JvmArgs = '--enable-native-access=ALL-UNNAMED -Dskycraft.startHidden=false -Dskycraft.showWindow=true -Dskycraft.link=Local\\Left4Craft_v1'
}
if (!$GameDirectory) {
    Add-Type -AssemblyName System.Windows.Forms
    $picker = New-Object System.Windows.Forms.OpenFileDialog
    $picker.Title = 'Select Left 4 Dead 1 left4dead.exe'
    $picker.Filter = 'Left 4 Dead 1|left4dead.exe'
    if ($picker.ShowDialog() -ne 'OK') { exit }
    $GameDirectory = Split-Path $picker.FileName -Parent
}
$GameDirectory = (Resolve-Path -LiteralPath $GameDirectory).Path
if (!(Test-Path -LiteralPath (Join-Path $GameDirectory 'left4dead.exe'))) { throw 'This must be Left 4 Dead 1, not L4D2.' }
if (Get-Process left4dead -ErrorAction SilentlyContinue) { throw 'Close Left 4 Dead before installing.' }
$expected = @{
    'bin/engine.dll' = 'ca17bce993672cfe89b9cb270ea5e28e5b590523e61a6b80f862a26312f4d2e0'
    'left4dead/bin/client.dll' = '7612545f68a6b670fb4d4a99a79c69f2d3939ebb80c15c1fd5c2b8c864f4daeb'
    'left4dead/bin/server.dll' = 'b14de7055b123ff2f4ec50fd059118361cb4d601b976e39019b97bd205802265'
}
foreach ($entry in $expected.GetEnumerator()) {
    $path = Join-Path $GameDirectory $entry.Key
    if (!(Test-Path -LiteralPath $path) -or ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $entry.Value)) {
        throw "Unsupported game build: $($entry.Key). This prototype requires the exact supplied binaries; your game files have not been changed."
    }
}
$addons = Join-Path $GameDirectory 'left4dead/addons'
New-Item -ItemType Directory -Force -Path $addons | Out-Null
$target = Join-Path $addons 'left4craft.dll'
if ((Test-Path -LiteralPath $target) -and !(Test-Path -LiteralPath "$target.backup")) { Copy-Item -LiteralPath $target -Destination "$target.backup" }
Copy-Item -LiteralPath "$root/bin/left4craft.dll" -Destination $target -Force
@{ GameDirectory = $GameDirectory } | ConvertTo-Json | Set-Content -LiteralPath "$root/settings.json" -Encoding UTF8
Write-Host 'Installed. No game binaries or automatic plugin-loading files were changed.'
Write-Host 'In Prism: add your Minecraft Java account, launch Left4Craft once to download Java/game files, then close Minecraft.'
Write-Host 'After setup, use Launch.cmd. The game session will run offline.'
Start-Process -FilePath "$root/Minecraft/Prism/prismlauncher.exe" -WorkingDirectory "$root/Minecraft/Prism" -ArgumentList '--dir .'
