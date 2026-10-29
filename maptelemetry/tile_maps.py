"""Create offline map tiles from an image you have permission to use.

Example:
    python maptelemetry/tile_maps.py --map chernarusplus --image C:/maps/my-map.jpg

The viewer expects a 16,384 x 16,384 source image. This script does not
include, download, or alter source imagery.
"""
from __future__ import annotations

import argparse
from pathlib import Path
from PIL import Image

Image.MAX_IMAGE_PIXELS = None
ROOT = Path(__file__).resolve().parents[1]
DEST = ROOT / "maptelemetry" / "web" / "tiles"
TILE = 512
MAX_ZOOM = 5


def make_tiles(slug: str, source_path: Path) -> None:
    with Image.open(source_path) as source:
        if source.size != (16384, 16384):
            raise ValueError(f"Expected a 16384 x 16384 image, got {source.size}")
        full = source.convert("RGB")
    for zoom in range(MAX_ZOOM, -1, -1):
        side = TILE * (1 << zoom)
        image = full if zoom == MAX_ZOOM else full.resize(
            (side, side), Image.Resampling.LANCZOS
        )
        count = 1 << zoom
        for x in range(count):
            folder = DEST / slug / str(zoom) / str(x)
            folder.mkdir(parents=True, exist_ok=True)
            for y in range(count):
                region = image.crop((x * TILE, y * TILE, (x + 1) * TILE, (y + 1) * TILE))
                region.save(folder / f"{y}.jpg", "JPEG", quality=82, subsampling=0)
        print(f"{slug}: zoom {zoom}, {count * count} tiles", flush=True)
        if image is not full:
            image.close()
    full.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--map", required=True,
                        choices=("chernarusplus", "namalsk", "nasdara"))
    parser.add_argument("--image", required=True, type=Path)
    args = parser.parse_args()
    make_tiles(args.map, args.image)
