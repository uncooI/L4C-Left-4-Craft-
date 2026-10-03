$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$sdk = Join-Path $root '.deps/hl2sdk-l4d'
if (!(Test-Path "$sdk/.git")) {
    git clone --branch l4d https://github.com/alliedmodders/hl2sdk.git $sdk
    if ($LASTEXITCODE) { throw 'SDK clone failed' }
}
git -C $sdk checkout 0a8e862697335b12976a124daf728c38e975e381
if ($LASTEXITCODE) { throw 'SDK checkout failed' }
# This old SDK's constructor template-id is invalid in modern C++20.
$thread = Join-Path $sdk 'public/tier0/threadtools.h'
$text = [IO.File]::ReadAllText($thread).Replace('CAutoLockT<MUTEX_TYPE>( const CAutoLockT<MUTEX_TYPE> & );','CAutoLockT( const CAutoLockT<MUTEX_TYPE> & );')
[IO.File]::WriteAllText($thread, $text)
cmake -S $root -B "$root/build" -A Win32 "-DHL2SDK=$sdk"
if ($LASTEXITCODE) { throw 'Configure failed' }
cmake --build "$root/build" --config Release
if ($LASTEXITCODE) { throw 'Build failed' }
ctest --test-dir "$root/build" -C Release --output-on-failure
if ($LASTEXITCODE) { throw 'Tests failed' }
Write-Host "Built experimental prototype: $root/build/Release/left4craft.dll (in-game verification still required)"
