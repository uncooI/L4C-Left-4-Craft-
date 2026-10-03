$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (Get-Process left4dead -ErrorAction SilentlyContinue) { throw 'Close L4D1 first.' }
if (!(Test-Path -LiteralPath "$root/settings.json")) { Write-Host 'No saved installation.'; exit }
$game = (Get-Content -LiteralPath "$root/settings.json" -Raw | ConvertFrom-Json).GameDirectory
$target = Join-Path $game 'left4dead/addons/left4craft.dll'
if (Test-Path -LiteralPath $target) {
    if ((Get-FileHash -LiteralPath $target).Hash -ne (Get-FileHash -LiteralPath "$root/bin/left4craft.dll").Hash) { throw 'Installed DLL differs from this package; leaving it in place.' }
    Remove-Item -LiteralPath $target
    if (Test-Path -LiteralPath "$target.backup") { Move-Item -LiteralPath "$target.backup" -Destination $target }
}
Remove-Item -LiteralPath "$root/settings.json"
Write-Host 'Removed this installation. Minecraft saves remain in the extracted package.'
