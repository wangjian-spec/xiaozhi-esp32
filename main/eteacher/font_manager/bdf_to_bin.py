from __future__ import annotations

import argparse
import struct
from pathlib import Path

from bdf_to_gxepd2 import collect_codepoints, flatten_glyphs, parse_bdf

MAGIC = b"BDFB"
VERSION = 1


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Serialize glyphs from wenquanyi_11pt.bdf into a compact .bin the GxEPD2 library can read."
    )
    parser.add_argument(
        "bdf",
        type=Path,
        nargs="*",
        help="Source BDF file(s). If omitted, all .bdf files in the current directory are processed.",
    )
    parser.add_argument(
        "--range",
        "-r",
        action="append",
        help="Unicode range to include (accepts 0xXXXX-0xYYYY or U+XXXX-U+YYYY).",
    )
    parser.add_argument(
        "--chars",
        help="Literal characters to include even if they fall outside the ranges.",
    )
    parser.add_argument(
        "--text-file",
        type=Path,
        help="File whose characters should be added to the selection.",
    )
    parser.add_argument(
        "--codepoints-file",
        type=Path,
        help="File with hexadecimal codepoints (U+4E00 or 0x4E01) separated by whitespace or commas.",
    )
    parser.add_argument(
        "--all",
        action="store_true",
        help="Embed every glyph from the BDF (output can be very large). This is the default when you don't request a subset.",
    )
    parser.add_argument(
        "--max-glyphs",
        type=int,
        help="Stop after collecting this many glyphs (applies after sorting by codepoint).",
    )
    parser.add_argument(
        "--output",
        type=Path,
        help="Target .bin file (defaults to <bdf-stem>.bin).",
    )
    parser.add_argument(
        "--bytes-order",
        choices=["big", "little"],
        default="big",
        help="Endianness for multi-byte integers in the binary layout (default: big).",
    )
    return parser.parse_args()


def build_header(metrics: tuple[int, int, int, int], ascent: int, descent: int, glyph_count: int, endianness: str) -> bytes:
    fmt = f">4sBHHIhhhh" if endianness == "big" else f"<4sBHHIhhhh"
    bbox_w, bbox_h, bbox_x, bbox_y = metrics
    return struct.pack(fmt, MAGIC, VERSION, ascent, descent, glyph_count, bbox_w, bbox_h, bbox_x, bbox_y)


def glyph_entry_format(endianness: str) -> str:
    return ">IHHhhHI" if endianness == "big" else "<IHHhhHI"


def main() -> None:
    args = parse_args()
    if not (args.range or args.chars or args.text_file or args.codepoints_file):
        args.all = True
    # Determine input BDF files
    bdf_inputs: list[Path]
    if args.bdf:
        bdf_inputs = [p for p in args.bdf]
    else:
        bdf_inputs = sorted(Path.cwd().glob("*.bdf"))

    if not bdf_inputs:
        raise SystemExit("No .bdf files found to process.")

    # Shared selection across files
    selection = collect_codepoints(args)
    entry_fmt = glyph_entry_format(args.bytes_order)

    for bdf_path in bdf_inputs:
        if not bdf_path.exists():
            print(f"Skipping missing file: {bdf_path}")
            continue

        glyphs, metrics = parse_bdf(bdf_path, selection, args.max_glyphs)
        if not glyphs:
            print(f"No glyphs found in {bdf_path} for the requested selection; skipping.")
            continue

        glyph_data, offsets = flatten_glyphs(glyphs)
        header = build_header(metrics.bbox, metrics.ascent, metrics.descent, len(glyphs), args.bytes_order)

        entries = bytearray()
        for glyph, offset in zip(glyphs, offsets):
            entries.extend(struct.pack(
                entry_fmt,
                glyph.codepoint,
                glyph.width,
                glyph.height,
                glyph.x_offset,
                glyph.y_offset,
                glyph.advance,
                offset,
            ))

        # Resolve output path: if --output provided and is a directory, place file there.
        if args.output:
            out = args.output
            if out.exists() and out.is_dir():
                output_path = out / bdf_path.with_suffix(".bin").name
            elif len(bdf_inputs) == 1:
                output_path = out
            else:
                raise SystemExit("When converting multiple BDFs, --output must be a directory.")
        else:
            output_path = bdf_path.with_suffix(".bin")

        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_bytes(header + entries + glyph_data)
        print(f"Wrote {len(glyphs)} glyphs into {output_path} ({output_path.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
