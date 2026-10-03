# Configure a private Java 25 runtime without relying on Prism's version metadata.
function Set-Left4CraftIniValues {
    param([string]$Path, [hashtable]$Values)
    $lines = New-Object 'System.Collections.Generic.List[string]'
    foreach ($line in [IO.File]::ReadAllLines($Path)) { $lines.Add($line) }
    foreach ($key in $Values.Keys) {
        $found = $false
        for ($index = 0; $index -lt $lines.Count; $index++) {
            if ($lines[$index].StartsWith("$key=")) { $lines[$index] = "$key=$($Values[$key])"; $found = $true }
        }
        if (!$found) { $lines.Add("$key=$($Values[$key])") }
    }
    [IO.File]::WriteAllLines($Path, $lines, (New-Object System.Text.UTF8Encoding($false)))
}
function Initialize-Left4CraftJava {
    param([string]$Root)
    $prism = Join-Path $Root 'Minecraft/Prism'
    $prismExe = [IO.Path]::GetFullPath((Join-Path $prism 'prismlauncher.exe'))
    foreach ($process in @(Get-Process prismlauncher -ErrorAction SilentlyContinue)) {
        if ($process.Path -eq $prismExe) { throw 'Close this package''s Prism Launcher before running Setup.cmd or Launch.cmd again, so it can read the updated Java settings.' }
    }
    $runtime = Join-Path $Root 'Minecraft/Java25'
    $java = Join-Path $runtime 'bin/java.exe'
    if (!(Test-Path -LiteralPath $java)) {
        Write-Host 'Downloading Java 25 for Windows. Internet is needed only for this first-time setup.'
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        $uri = 'https://api.adoptium.net/v3/assets/latest/25/hotspot?architecture=x64&image_type=jre&os=windows&vendor=eclipse'
        $assets = @(Invoke-RestMethod -Uri $uri)
        if (!$assets.Count) { throw 'Adoptium did not return a Windows Java 25 runtime.' }
        $package = $assets[0].binary.package
        if (!$package.link -or !$package.checksum -or !$package.name.EndsWith('.zip')) { throw 'Unexpected Java download metadata.' }
        $archive = Join-Path $Root 'Minecraft/Java25-download.zip'
        $temporary = Join-Path $Root ('Minecraft/Java25-extract-' + [guid]::NewGuid().ToString('N'))
        try {
            # Windows PowerShell's per-buffer progress UI makes large downloads very slow.
            $previousProgress = $ProgressPreference
            $ProgressPreference = 'SilentlyContinue'
            try { Invoke-WebRequest -Uri $package.link -UseBasicParsing -OutFile $archive }
            finally { $ProgressPreference = $previousProgress }
            if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $package.checksum) { throw 'Java checksum did not match. The download was not installed.' }
            Write-Host 'Java download verified. Extracting...'
            Expand-Archive -LiteralPath $archive -DestinationPath $temporary -Force
            $folder = Get-ChildItem -LiteralPath $temporary -Directory | Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'bin/java.exe') } | Select-Object -First 1
            if (!$folder) { throw 'Java archive did not contain bin/java.exe.' }
            if (Test-Path -LiteralPath $runtime) { throw 'An incomplete Java25 folder exists. Rename it and retry.' }
            Move-Item -LiteralPath $folder.FullName -Destination $runtime
        } finally {
            if (Test-Path -LiteralPath $archive) { Remove-Item -LiteralPath $archive -Force }
            if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary -Recurse -Force }
        }
    }
    $oldPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { $version = (& $java -version 2>&1 | Out-String); $javaExit = $LASTEXITCODE }
    finally { $ErrorActionPreference = $oldPreference }
    if ($javaExit -ne 0 -or $version -notmatch 'version "25[.\"]') { throw "The downloaded Java could not run as Java 25: $version" }
    $values = @{
        JavaPath = $java.Replace('\', '/')
        AutomaticJavaSwitch = 'false'
        AutomaticJavaDownload = 'false'
    }
    Set-Left4CraftIniValues -Path (Join-Path $prism 'prismlauncher.cfg') -Values $values
    $values['OverrideJavaLocation'] = 'true'
    Set-Left4CraftIniValues -Path (Join-Path $prism 'instances/Left4Craft/instance.cfg') -Values $values
    Write-Host "Java 25 configured: $java"
}
