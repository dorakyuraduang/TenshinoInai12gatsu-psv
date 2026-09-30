param(
    [string]$Resources = 'D:/tenshi/天使不在的12月',
    [string]$Compiler = 'D:/msys2/mingw64/bin/g++.exe',
    [string]$SdlRoot = 'D:/SDL2-2.30.7'
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$build = Join-Path $repo 'build-host'
$testState = Join-Path $build ('parity-audit-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testState | Out-Null
Copy-Item -LiteralPath (Join-Path $repo 'data-reference/startup.json') -Destination $testState
$source = [IO.File]::ReadAllText((Join-Path $repo 'src/main.cpp'))
$anchor = '  public:'
if (($source.Split([string[]]@($anchor), [StringSplitOptions]::None).Length - 1) -ne 1) {
    throw 'App public anchor changed; review the audit injection.'
}
$source = $source.Replace($anchor, ('#include "parity_audit.inc"' + "`n" + $anchor))
if (-not $source.Contains('            verifyUi();')) { throw 'UI smoke entry changed.' }
$source = $source.Replace('            verifyUi();', '            auditParity();')
$auditSource = Join-Path $build 'parity-audit-main.cpp'
[IO.File]::WriteAllText($auditSource, $source, [Text.UTF8Encoding]::new($false))
$ttf = Join-Path $repo '.tools/SDL2_ttf-2.24.0/x86_64-w64-mingw32'
$mixer = Join-Path $repo '.tools/SDL2_mixer-2.8.1/x86_64-w64-mingw32'
$auditExe = Join-Path $build 'parity-audit.exe'
$previousPath = $env:PATH
$previousVideo = $env:SDL_VIDEODRIVER
$previousAudio = $env:SDL_AUDIODRIVER
try {
    $env:PATH = (Split-Path $Compiler) + ';' + $env:PATH
    & $Compiler -std=c++17 -O2 -Wall -Wextra -static-libgcc -static-libstdc++ "-I$repo/src" "-I$repo/tests" "-I$SdlRoot/include" "-I$ttf/include/SDL2" "-I$mixer/include/SDL2" $auditSource "$SdlRoot/lib/x64/SDL2.dll" "$SdlRoot/lib/x64/SDL2_image.dll" "$ttf/bin/SDL2_ttf.dll" "$mixer/bin/SDL2_mixer.dll" -o $auditExe
    if ($LASTEXITCODE) { throw "Audit compilation failed: $LASTEXITCODE" }
    $env:SDL_VIDEODRIVER = 'dummy'
    $env:SDL_AUDIODRIVER = 'dummy'
    & $auditExe $Resources --ui-smoke 1 $testState
    if ($LASTEXITCODE) { throw "Audit execution failed: $LASTEXITCODE" }
    $result = Join-Path $testState 'audit-results.json'
    Copy-Item -LiteralPath $result -Destination (Join-Path $build 'parity-audit-results.json')
    Write-Output "Audit JSON: $result"
} finally {
    $env:PATH = $previousPath
    $env:SDL_VIDEODRIVER = $previousVideo
    $env:SDL_AUDIODRIVER = $previousAudio
}
