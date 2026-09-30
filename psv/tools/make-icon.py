"""Convert the Godot Android launcher icon to Vita's 128x128 PNG size.

Real Vita hardware rejects LiveArea images that are not 8-bit indexed PNGs
without alpha: VitaShell stops near 99% with error 0x8010113D. Vita3K does not
check this, so the icon is saved as an opaque 256-colour palette PNG.

Requires Pillow (python -m pip install Pillow). No artwork is generated.
"""
from pathlib import Path
from PIL import Image

root = Path(__file__).resolve().parents[2]
source = root / "icon.png"
target = root / "psv/sce_sys/icon0.png"
with Image.open(source) as image:
    rgba = image.convert("RGBA").resize((128, 128), Image.Resampling.LANCZOS)
# Flatten any transparency onto black; the current source icon is fully opaque.
opaque = Image.new("RGB", rgba.size, (0, 0, 0))
opaque.paste(rgba, mask=rgba.getchannel("A"))
indexed = opaque.quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.FLOYDSTEINBERG)
indexed.save(target, bits=8)
print(f"Vita icon copied from {source.name}: 128x128 8-bit indexed, no alpha")
