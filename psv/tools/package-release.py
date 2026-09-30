"""Package explicit program/font files plus the user-requested opening MP4; exclude other game assets."""
from pathlib import Path
import zipfile, json, hashlib
def png_chunks(data):
    pos=8
    while pos+8<=len(data):
        size=int.from_bytes(data[pos:pos+4],'big')
        yield data[pos+4:pos+8],data[pos+8:pos+8+size]
        pos+=12+size
root=Path(__file__).resolve().parents[2]
psv=root/'psv'; dist=psv/'dist'; dist.mkdir(exist_ok=True)
source=dist/'tenshi-vita-source.zip'
font_entry='assets/SourceHanSansCN-Regular.otf'
video_entry='assets/opening.mp4'
index_entry='assets/opening.idx'
video_meta=json.loads((psv/'assets/opening.json').read_text(encoding='utf-8'))
if hashlib.sha256((psv/video_entry).read_bytes()).hexdigest()!=video_meta['outputSha256']:
    raise RuntimeError('Opening video differs from its conversion manifest')
if hashlib.sha256((psv/index_entry).read_bytes()).hexdigest()!=video_meta['packetIndex']['sha256']:
    raise RuntimeError('Opening packet index differs from manifest')
font_meta=json.loads((psv/'assets/source-han-sans.json').read_text())
if hashlib.sha256((psv/font_entry).read_bytes()).hexdigest()!=font_meta['fontSha256']:
    raise RuntimeError('Bundled font differs from pinned official release')
if hashlib.sha256((psv/'licenses/SourceHanSans-OFL.txt').read_bytes()).hexdigest()!=font_meta['licenseSha256']:
    raise RuntimeError('Font license differs from pinned official release')
allowed=[psv/'assets/opening.json', psv/'VIDEO_LOGO_FIXES_0.20.md', psv/'VIDEO_LOGO_FIXES_0.21.md', psv/'VIDEO_LOGO_FIXES_0.22.md', psv/'VIDEO_LOGO_FIXES_0.23.md', psv/'UI_FIXES_0.24.md', psv/font_entry, psv/'assets/source-han-sans.json', psv/'GODOT_PARITY_AUDIT.md', psv/'GODOT_PARITY_FIXES_0.17.md', psv/'FONT_LICENSE.md']
for name in ['src','tests','tools','licenses','third_party','sce_sys']:
    allowed.extend(p for p in (psv/name).rglob('*') if p.is_file() and '__pycache__' not in p.parts)
allowed.extend(p for p in (psv/'prepare').iterdir() if p.is_file() and (p.name.endswith('.cs.in') or p.suffix=='.csproj'))
allowed.extend(psv/name for name in ['README.md','GITHUB_README.md','INSTALL.md','VALIDATION.md','THIRD_PARTY_NOTICES.md','CMakeLists.txt','.gitignore','.gdignore','.clang-format'])
allowed.extend(root/name for name in ['LICENSE','THIRD_PARTY_NOTICES.md','icon.png'])
allowed.extend(root/'Scripts'/name for name in ['Archive.cs','ScenarioScript.cs','ScenarioRuntime.cs','PxDecoder.cs','UiPxDecoder.cs','AudioDecoder.cs','VoiceDecoder.cs','MessageView.cs','OriginalEffectMath.cs','GameMain.cs'])
allowed.extend((root/'Scripts').glob('GameMain.*.cs'))
with zipfile.ZipFile(source,'w',zipfile.ZIP_DEFLATED) as z:
    z.write(psv/'GITHUB_README.md','README.md')
    for p in sorted(set(allowed)):z.write(p,p.relative_to(root).as_posix())
for name in ['README.md','INSTALL.md','VALIDATION.md','THIRD_PARTY_NOTICES.md','FONT_LICENSE.md','GODOT_PARITY_AUDIT.md','GODOT_PARITY_FIXES_0.17.md','VIDEO_LOGO_FIXES_0.20.md','VIDEO_LOGO_FIXES_0.21.md','VIDEO_LOGO_FIXES_0.22.md','VIDEO_LOGO_FIXES_0.23.md','UI_FIXES_0.24.md']:
    text=(psv/name).read_text(encoding='utf-8-sig').replace('](../LICENSE)','](LICENSE)')
    (dist/name).write_text(text,encoding='utf-8')
(dist/'LICENSE').write_bytes((root/'LICENSE').read_bytes())
# Keep public artifact names stable; the application version remains in param.sfo.
release=dist/'tenshi-vita.zip'
with zipfile.ZipFile(release,'w',zipfile.ZIP_DEFLATED) as z:
    for name in ['tenshi-vita.vpk','tenshi-vita-source.zip','README.md','INSTALL.md','VALIDATION.md','THIRD_PARTY_NOTICES.md','FONT_LICENSE.md','GODOT_PARITY_AUDIT.md','GODOT_PARITY_FIXES_0.17.md','VIDEO_LOGO_FIXES_0.20.md','VIDEO_LOGO_FIXES_0.21.md','VIDEO_LOGO_FIXES_0.22.md','VIDEO_LOGO_FIXES_0.23.md','UI_FIXES_0.24.md','LICENSE']:z.write(dist/name,name)
    z.write(psv/'assets/source-han-sans.json','assets/source-han-sans.json')
    z.write(psv/'assets/opening.json','assets/opening.json')
    for p in (psv/'licenses').iterdir():
        if p.is_file():z.write(p,'licenses/'+p.name)
    for p in (psv/'third_party').iterdir():
        if p.is_file():z.write(p,'third_party/'+p.name)
with zipfile.ZipFile(dist/'tenshi-vita.vpk') as z:
    names=z.namelist()
    for required in ['eboot.bin','sce_sys/param.sfo','sce_sys/icon0.png','LICENSE','THIRD_PARTY_NOTICES.md',font_entry,'licenses/SourceHanSans-OFL.txt','FONT_LICENSE.md','assets/source-han-sans.json',video_entry,index_entry,'assets/opening.json']:
        if required not in names:raise RuntimeError('Missing VPK entry: '+required)
    icon=z.read('sce_sys/icon0.png')
    if icon != (psv/'sce_sys/icon0.png').read_bytes():raise RuntimeError('VPK icon differs from project output')
    # Real Vita installs fail with 0x8010113D unless the icon is an 8-bit indexed PNG without alpha.
    icon_chunks=dict(png_chunks(icon))
    if icon[:8]!=b'\x89PNG\r\n\x1a\n' or icon_chunks.get(b'IHDR',b'')[:10]!=(128).to_bytes(4,'big')*2+bytes([8,3]) or b'tRNS' in icon_chunks:raise RuntimeError('icon0.png must be a 128x128 8-bit indexed PNG without alpha')
    if z.read('eboot.bin')[:4]!=b'SCE\x00':raise RuntimeError('Not a Vita SELF executable')
    if b'00.26\x00' not in z.read('sce_sys/param.sfo'):raise RuntimeError('Wrong VPK version')
    if z.read(index_entry) != (psv/index_entry).read_bytes():raise RuntimeError('Bundled packet index mismatch')
    if z.read(video_entry) != (psv/video_entry).read_bytes():raise RuntimeError('Bundled opening mismatch')
    if z.read(font_entry) != (psv/font_entry).read_bytes():raise RuntimeError('Bundled font mismatch')
    if z.read('licenses/SourceHanSans-OFL.txt') != (psv/'licenses/SourceHanSans-OFL.txt').read_bytes():raise RuntimeError('Font license mismatch')
    for name in ['THIRD_PARTY_NOTICES.md','FONT_LICENSE.md','assets/source-han-sans.json','assets/opening.json']:
        if z.read(name) != (psv/name).read_bytes():raise RuntimeError('Outdated VPK document: '+name)
    for name in names:
        if name in [font_entry,video_entry]:continue
        if name.endswith(('.ttf','.otf','.ttc','.ogg','.wav','.mpg','.mp4','.a','.v')) or name in ['manifest.json','prologue.json']:raise RuntimeError('Player asset in VPK: '+name)
with zipfile.ZipFile(source) as z:
    for name in z.namelist():
        if any(part.startswith('data') or part in ['.tools','dist','bin','obj'] for part in Path(name).parts):raise RuntimeError('Unexpected source entry: '+name)
checks={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in [dist/'tenshi-vita.vpk',source,release]}
(dist/'SHA256SUMS.txt').write_text(''.join(h+'  '+name+'\n' for name,h in checks.items()))
print(json.dumps({'vpkBytes':(dist/'tenshi-vita.vpk').stat().st_size,'releaseBytes':release.stat().st_size,'sourceFiles':len(allowed),'checksums':checks},indent=2))
