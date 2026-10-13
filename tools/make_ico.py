"""Packs the rendered icon PNGs (AnnealSnapshot --icons resources/icon) into one Windows .ico.

Each size is the vector icon rendered at that size (small sizes use the simpler drawing), stored
as a PNG entry, which Windows Vista and later read directly.

    python tools/make_ico.py resources/icon resources/icon/anneal.ico
"""
import struct
import sys
from pathlib import Path

SIZES = [16, 20, 24, 32, 40, 48, 64, 128, 256]


def main(src: Path, dst: Path) -> None:
    blobs = [(s, (src / f"icon_{s}.png").read_bytes()) for s in SIZES]
    header = struct.pack("<HHH", 0, 1, len(blobs))
    offset = len(header) + 16 * len(blobs)
    entries, data = b"", b""
    for size, png in blobs:
        dim = 0 if size >= 256 else size          # 0 means 256 in an ICO directory entry
        entries += struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32, len(png), offset + len(data))
        data += png
    dst.write_bytes(header + entries + data)
    print(f"wrote {dst} ({len(blobs)} sizes, {dst.stat().st_size} bytes)")


if __name__ == "__main__":
    main(Path(sys.argv[1]), Path(sys.argv[2]))
