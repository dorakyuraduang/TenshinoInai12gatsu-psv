"""Check the bundled font with the same SDL_ttf API used by the Windows port."""
import ctypes
import hashlib
import json
import os
from pathlib import Path

psv = Path(__file__).resolve().parents[1]
font_path = psv / 'assets/SourceHanSansCN-Regular.otf'
meta = json.loads((psv / 'assets/source-han-sans.json').read_text())
assert hashlib.sha256(font_path.read_bytes()).hexdigest() == meta['fontSha256']
assert hashlib.sha256((psv / 'licenses/SourceHanSans-OFL.txt').read_bytes()).hexdigest() == meta['licenseSha256']
characters = set()
layouts = 0
with (psv / 'data-reference/presentation.jsonl').open(encoding='utf-8') as reference:
    for line in reference:
        entry = json.loads(line)
        layouts += 1
        for page in entry['pages']:
            for glyph in page:
                characters.update(glyph[0].replace('\ue000', '时纪').replace('\ue001', '木田'))
# Cover UI labels, punctuation, names and diagnostic strings without adding control bytes.
for name in ['src/main.cpp', 'src/interface.inc']:
    characters.update(c for c in (psv / name).read_text(encoding='utf-8-sig') if ord(c) >= 32)
characters.discard('\x7f')
with os.add_dll_directory(str(psv / 'build-host')):
    ttf = ctypes.CDLL(str(psv / 'build-host/SDL2_ttf.dll'))
    ttf.TTF_Init.restype = ctypes.c_int
    ttf.TTF_OpenFont.argtypes = [ctypes.c_char_p, ctypes.c_int]
    ttf.TTF_OpenFont.restype = ctypes.c_void_p
    ttf.TTF_GlyphIsProvided32.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
    ttf.TTF_GlyphIsProvided32.restype = ctypes.c_int
    ttf.TTF_FontFaceFamilyName.argtypes = [ctypes.c_void_p]
    ttf.TTF_FontFaceFamilyName.restype = ctypes.c_char_p
    ttf.TTF_CloseFont.argtypes = [ctypes.c_void_p]
    assert ttf.TTF_Init() == 0
    face = ttf.TTF_OpenFont(str(font_path).encode('utf-8'), 24)
    assert face, 'SDL_ttf could not load the bundled font'
    family = ttf.TTF_FontFaceFamilyName(face).decode('utf-8')
    missing = sorted(ord(c) for c in characters if not ttf.TTF_GlyphIsProvided32(face, ord(c)))
    ttf.TTF_CloseFont(face)
    ttf.TTF_Quit()
result = {'family': family, 'layouts': layouts, 'uniqueCharacters': len(characters),
          'missingCodepoints': [f'U+{c:04X}' for c in missing], 'fontSha256': meta['fontSha256']}
(psv / 'build-host/font-coverage.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps(result, ensure_ascii=True, indent=2))
assert not missing, 'Font does not cover all rendered reference characters'
