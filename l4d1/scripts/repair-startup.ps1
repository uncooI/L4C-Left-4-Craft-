$ErrorActionPreference = 'Stop'
try {
    $root = Split-Path $PSScriptRoot -Parent
    if (Get-Process left4dead -ErrorAction SilentlyContinue) { throw 'Close L4D1 before repairing.' }
    if (!(Test-Path -LiteralPath "$root/settings.json")) { throw 'Extract the patch beside your existing Launch.cmd and settings.json. Run Setup.cmd if you have not installed yet.' }
    $game = (Get-Content -LiteralPath "$root/settings.json" -Raw | ConvertFrom-Json).GameDirectory
    if (!(Test-Path -LiteralPath (Join-Path $game 'left4dead.exe'))) { throw 'Saved L4D1 installation path is invalid.' }
    $bytes = [Convert]::FromBase64String([IO.File]::ReadAllText("$root/payload/left4craft.dll.b64"))
    $expected = ([IO.File]::ReadAllText("$root/payload/left4craft-sha256.txt")).Trim()
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $actual = ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '') }
    finally { $sha.Dispose() }
    if ($actual -ne $expected) { throw 'DLL payload failed verification; nothing was installed.' }
    New-Item -ItemType Directory -Force -Path "$root/bin" | Out-Null
    [IO.File]::WriteAllBytes("$root/bin/left4craft.dll", $bytes)
    [IO.File]::WriteAllText("$root/bin/left4craft-sha256.txt", $expected)
    $addons = Join-Path $game 'left4dead/addons'
    New-Item -ItemType Directory -Force -Path $addons | Out-Null
    Copy-Item -LiteralPath "$root/bin/left4craft.dll" -Destination (Join-Path $addons 'left4craft.dll') -Force
    if ((Get-FileHash -LiteralPath (Join-Path $addons 'left4craft.dll')).Hash -ne $expected) { throw 'Installed DLL verification failed.' }
    Write-Host 'Plugin DLL repaired and verified. Close Prism, then use Launch.cmd.' -ForegroundColor Green
} catch {
    Write-Host $_.Exception.Message -ForegroundColor Red
    exit 1
}
