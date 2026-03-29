"""Generate .bin icon files for e-paper display.

This script scans the apps/icons/{apps,top,bottom} folders, converts all
supported image files into a 1-bit packed bitmap, and writes a .bin file
next to each image. The .bin file starts with width/height (uint16 LE),
followed by packed pixel data (MSB first).

Run without arguments:
	python ImageInvert.py
"""

from __future__ import annotations

import sys
from pathlib import Path
from typing import Dict, Iterable, Tuple


try:
	from PIL import Image, ImageOps, ImageStat
except Exception as exc:  # pragma: no cover - runtime dependency check
	print("Pillow is required. Install with: pip install pillow")
	raise


ROOT_DIR = Path(__file__).resolve().parent

# Target size for images (width, height). Modify this tuple to change output size.
TARGET_SIZE: Tuple[int, int] = (80, 80)

SUPPORTED_EXTS = {".png", ".jpg", ".jpeg", ".bmp"}


def _iter_images(folder: Path) -> Iterable[Path]:
	for path in folder.iterdir():
		if path.is_file() and path.suffix.lower() in SUPPORTED_EXTS:
			yield path


def _pack_1bit(img: Image.Image) -> bytes:
	"""Pack 1-bit image into bytes (MSB first per byte), padded per row.

	Output format matches Adafruit_GFX::drawBitmap (bit7 is leftmost pixel).
	"""
	if img.mode != "1":
		img = img.convert("1")
	width, height = img.size
	stride = (width + 7) // 8
	packed = bytearray(stride * height)
	px = img.load()
	for y in range(height):
		row_offset = y * stride
		for x in range(width):
			if px[x, y] == 0:  # black pixel
				byte_index = row_offset + (x >> 3)
				bit = 0x80 >> (x & 7)
				packed[byte_index] |= bit
	return bytes(packed)


def convert_image(image_path: Path, target_size: Tuple[int, int]) -> Path:
	with Image.open(image_path) as img:
		img = img.convert("RGBA")
		if img.size != target_size:
			img = img.resize(target_size, Image.NEAREST)

		# If the image has transparency, use alpha as mask to preserve white glyphs on transparent.
		alpha = img.getchannel("A")
		if alpha.getextrema()[0] < 255:
			# Render any non-transparent pixel as black on white background.
			mask = alpha.point(lambda a: 255 if a > 0 else 0)
			base = Image.new("L", img.size, 255)
			base.paste(0, mask=mask)
			img = base
		else:
			# Fully opaque: composite onto white and proceed normally.
			bg = Image.new("RGBA", img.size, (255, 255, 255, 255))
			img = Image.alpha_composite(bg, img).convert("L")

		# Auto-invert if the icon background is dark (e.g., white glyph on black bg).
		# Skip for status bar icons (top/bottom) to avoid over-inverting thin glyphs.
		folder_name = image_path.parent.name
		if folder_name not in {"top", "bottom"}:
			try:
				w, h = img.size
				border = max(1, min(w, h) // 10)
				# Sample four borders and average
				top = img.crop((0, 0, w, border))
				bottom = img.crop((0, h - border, w, h))
				left = img.crop((0, 0, border, h))
				right = img.crop((w - border, 0, w, h))
				mean_vals = (
					ImageStat.Stat(top).mean[0]
					+ ImageStat.Stat(bottom).mean[0]
					+ ImageStat.Stat(left).mean[0]
					+ ImageStat.Stat(right).mean[0]
				) / 4.0
				if mean_vals < 128:
					img = ImageOps.invert(img)
			except Exception:
				pass

		# Convert to 1-bit (black/white) using dithering
		img = img.convert("1", dither=Image.FLOYDSTEINBERG)

		width, height = img.size
		data = _pack_1bit(img)
		header = width.to_bytes(2, "little") + height.to_bytes(2, "little")
		out_path = image_path.with_suffix(".bin")
		out_path.write_bytes(header + data)
		return out_path


def process_current_folder(target_size: Tuple[int, int]) -> None:
	"""Process all supported images in the script's current folder."""
	folder = ROOT_DIR
	for image_path in _iter_images(folder):
		out_path = convert_image(image_path, target_size)
		print(f"Generated: {out_path.name} ({target_size[0]}x{target_size[1]})")


def main() -> int:
	process_current_folder(TARGET_SIZE)
	return 0


if __name__ == "__main__":
	sys.exit(main())
