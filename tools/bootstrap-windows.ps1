param([Parameter(Mandatory=$true)][string]$MsysBin)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$tools=Join-Path $root '.tools'
New-Item -ItemType Directory -Force $tools | Out-Null
$tar=Join-Path $MsysBin 'tar.exe'
if(!(Test-Path $tar)){throw 'Provide the MSYS2 usr/bin directory containing tar, xz and bzip2.'}
$env:PATH=$MsysBin+';'+$env:PATH
$lock=Get-Content (Join-Path $PSScriptRoot 'dependencies.lock.json') -Raw | ConvertFrom-Json
function Fetch([string]$Url,[string]$Hash,[string]$Path){
    if(!(Test-Path $Path)){Invoke-WebRequest $Url -OutFile $Path}
    if((Get-FileHash $Path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $Hash.ToLowerInvariant()){
        throw "Checksum mismatch: $Path. Upstream may have replaced its package. Do not use it without reviewing the dependency lock."
    }
}
$sdkArchive=Join-Path $tools 'vitasdk.tar.bz2'
Fetch $lock.sdk.url $lock.sdk.sha256 $sdkArchive
& $tar --force-local -xjf $sdkArchive -C $tools
if($LASTEXITCODE -ne 0){throw 'SDK extraction failed'}
foreach($pkg in $lock.packages){
    $archive=Join-Path $tools ($pkg.name+'.tar.xz')
    Fetch $pkg.url $pkg.sha256 $archive
    & $tar --force-local -xJf $archive -C (Join-Path $tools 'vitasdk/arm-vita-eabi')
    if($LASTEXITCODE -ne 0){throw ('Package extraction failed: '+$pkg.name)}
}
Write-Output 'VitaSDK is ready. Run ./tools/build-vita.ps1 from the repository root.'
