$ErrorActionPreference = 'Stop'
try {
    $mods = Join-Path $PSScriptRoot 'Minecraft/Prism/instances/Left4Craft/.minecraft/mods'
    if (!(Test-Path -LiteralPath $mods -PathType Container)) { throw 'Extract this repair directly into Left4Craft/l4d1, beside Launch.cmd, then run Repair-Mods.cmd.' }
    $manifest = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'payload/mods.json') -Raw | ConvertFrom-Json
    foreach ($entry in $manifest) {
        $payload = Join-Path $PSScriptRoot ('payload/' + $entry.file + '.b64')
        $bytes = [Convert]::FromBase64String([IO.File]::ReadAllText($payload))
        $sha = [Security.Cryptography.SHA256]::Create()
        try { $actual = ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant() }
        finally { $sha.Dispose() }
        if ($bytes.Length -ne $entry.bytes -or $actual -ne $entry.sha256) { throw "Repair payload failed verification: $($entry.file). Nothing was written for this file." }
        $target = Join-Path $mods $entry.file
        $temporary = "$target.left4craft-repair-tmp"
        try {
            [IO.File]::WriteAllBytes($temporary, $bytes)
            if ((Get-FileHash -LiteralPath $temporary -Algorithm SHA256).Hash -ne $entry.sha256) { throw "Written file failed verification: $($entry.file)" }
            Move-Item -LiteralPath $temporary -Destination $target -Force
        } finally {
            if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary -Force }
        }
        Write-Host "Repaired $($entry.file): $($bytes.Length) bytes, SHA256 verified." -ForegroundColor Green
    }
    Write-Host 'Finished. Launch the Left4Craft instance again. Account data and saves were not changed.'
} catch {
    Write-Host $_.Exception.Message -ForegroundColor Red
    exit 1
}
