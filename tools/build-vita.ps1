param([string]$VitaSdk = (Join-Path $PSScriptRoot '../.tools/vitasdk'))
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$sdk=[IO.Path]::GetFullPath($VitaSdk)
$build=Join-Path $root 'build-vita'
$dist=Join-Path $root 'dist'
New-Item -ItemType Directory -Force $build,$dist | Out-Null
$env:PATH=(Join-Path $sdk 'bin')+';'+$env:PATH
function Run-Tool([string]$Name,[string[]]$ToolArgs) {
    & (Join-Path $sdk "bin/$Name.exe") @ToolArgs
    if($LASTEXITCODE -ne 0){throw "$Name failed ($LASTEXITCODE)"}
}
$libs=@('SDL2_ttf','SDL2_image','SDL2_mixer','SDL2','freetype','harfbuzz','bz2','png','jpeg','webp','webpdemux','z','vorbisfile','vorbis','ogg','opusfile','opus','modplug','xmp-lite','stdc++','pthread','SceVideodec_stub','SceCodecEngine_stub','SceAudiodec_stub','SceGxm_stub','SceDisplay_stub','SceCtrl_stub','SceAppMgr_stub','SceAppUtil_stub','SceAudio_stub','SceAudioIn_stub','SceSysmodule_stub','SceIofilemgr_stub','SceCommonDialog_stub','SceTouch_stub','SceHid_stub','SceMotion_stub','ScePower_stub','SceProcessmgr_stub','m')
$compile=@('-std=c++17','-O2','-Wno-psabi',"-I$sdk/arm-vita-eabi/include/SDL2", "$root/src/main.cpp",'-Wl,--start-group')
$compile+=@($libs | ForEach-Object { '-l'+$_ })
# Same link flags as VitaSDK's vita.toolchain.cmake. -Wl,-q keeps the relocation table that
# vita-elf-create needs: real hardware loads the segments at kernel-chosen addresses, so an
# eboot without it crashes at launch (C2-12828-1) even though Vita3K runs it.
$compile+=@('-Wl,--end-group','-Wl,-q','-Wl,-z,nocopyreloc','-o',"$build/tenshi.elf")
Run-Tool 'arm-vita-eabi-g++' $compile
Run-Tool 'vita-elf-create' @('-s',"$build/tenshi.elf","$build/tenshi.velf")
Run-Tool 'vita-make-fself' @('-s','-c',"$build/tenshi.velf","$build/eboot.bin")
Run-Tool 'vita-mksfoex' @('-s','TITLE_ID=TNSH00001','-s','APP_VER=00.26','Tenshi Vita',"$build/param.sfo")
$pack=@('-s',"$build/param.sfo",'-b',"$build/eboot.bin",'-a',"$root/LICENSE=LICENSE",'-a',"$root/THIRD_PARTY_NOTICES.md=THIRD_PARTY_NOTICES.md")
if(Test-Path "$root/sce_sys/icon0.png"){$pack+=@('-a',"$root/sce_sys/icon0.png=sce_sys/icon0.png")}
foreach($license in Get-ChildItem "$root/licenses" -File){$pack+=@('-a',($license.FullName+'=licenses/'+$license.Name))}
$pack+=@('-a',"$root/assets/SourceHanSansCN-Regular.otf=assets/SourceHanSansCN-Regular.otf")
$pack+=@('-a',"$root/assets/source-han-sans.json=assets/source-han-sans.json",'-a',"$root/FONT_LICENSE.md=FONT_LICENSE.md")
$pack+=@('-a',"$root/assets/opening.mp4=assets/opening.mp4",'-a',"$root/assets/opening.json=assets/opening.json",'-a',"$root/assets/opening.idx=assets/opening.idx")
$pack+=@("$dist/tenshi-vita.vpk")
Run-Tool 'vita-pack-vpk' $pack
Write-Output "Built $dist/tenshi-vita.vpk (program, OFL font and user-requested H.264 opening; other game archives excluded)"
