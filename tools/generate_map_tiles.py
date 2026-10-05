#!/usr/bin/env python3
"""Generate bundled offline XYZ tiles from Natural Earth countries (Pillow).

Usage: python3 tools/generate_map_tiles.py /path/to/ne_110m_admin_0_countries.geojson
Source: https://github.com/nvkelso/natural-earth-vector/tree/master/geojson
"""
import argparse
import json
import math
from pathlib import Path
import zlib
from PIL import Image, ImageDraw, ImageFont

OCEAN = (28, 48, 66)
LAND = (100, 129, 117)
BORDER = (162, 186, 168)
GRID = (39, 61, 78)
LIMIT = 85.0511287798066


def project(lon, lat, width):
    lat = math.radians(max(-LIMIT, min(LIMIT, lat)))
    return (lon + 180) / 360 * width, (1 - math.asinh(math.tan(lat)) / math.pi) / 2 * width


def generate(source, destination):
    features = json.loads(source.read_text())["features"]
    for zoom in range(5):
        width = 256 * (1 << zoom)
        scale = 2
        image = Image.new("RGB", (width * scale, width * scale), OCEAN)
        draw = ImageDraw.Draw(image)
        for longitude in range(-180, 181, 30):
            x, _ = project(longitude, 0, width * scale)
            draw.line((x, 0, x, width * scale), fill=GRID, width=scale)
        for latitude in range(-60, 61, 30):
            _, y = project(0, latitude, width * scale)
            draw.line((0, y, width * scale, y), fill=GRID, width=scale)
        for feature in features:
            geometry = feature["geometry"]
            polygons = geometry["coordinates"] if geometry["type"] == "MultiPolygon" else [geometry["coordinates"]]
            for polygon in polygons:
                for index, ring in enumerate(polygon):
                    points = [project(lon, lat, width * scale) for lon, lat in ring]
                    draw.polygon(points, fill=LAND if index == 0 else OCEAN)
                    draw.line(points, fill=BORDER, width=scale)
        image = image.resize((width, width), Image.Resampling.LANCZOS)
        if zoom >= 2:
            draw = ImageDraw.Draw(image)
            font = ImageFont.load_default()
            occupied = []
            for feature in sorted(features, key=lambda f: f["properties"].get("LABELRANK", 10)):
                p = feature["properties"]
                if p.get("LABELRANK", 10) > zoom + 1:
                    continue
                x, y = project(p["LABEL_X"], p["LABEL_Y"], width)
                label = p["NAME"].encode("latin-1", "replace").decode("latin-1")
                box = draw.textbbox((x, y), label, font=font, anchor="mm")
                padded = (box[0] - 5, box[1] - 3, box[2] + 5, box[3] + 3)
                if any(padded[0] < b[2] and padded[2] > b[0] and padded[1] < b[3] and padded[3] > b[1] for b in occupied):
                    continue
                occupied.append(padded)
                draw.text((x + 1, y + 1), label, fill=(25, 40, 36), font=font, anchor="mm")
                draw.text((x, y), label, fill=(231, 240, 226), font=font, anchor="mm")
        for x in range(1 << zoom):
            folder = destination / str(zoom) / str(x)
            folder.mkdir(parents=True, exist_ok=True)
            for y in range(1 << zoom):
                tile = image.crop((x * 256, y * 256, (x + 1) * 256, (y + 1) * 256))
                (folder / f"{y}.rgb.z").write_bytes(zlib.compress(tile.tobytes(), 9))
        print(f"Zoom {zoom}: {(1 << zoom) ** 2} tiles")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parents[1] / "assets/map_tiles")
    args = parser.parse_args()
    generate(args.source, args.output)
