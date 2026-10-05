#!/usr/bin/env python3
"""Reproject downloaded Swiss Map Raster 200 sheets into offline XYZ tiles.

Requires Pillow, numpy, scipy, pyproj. Run after download_swiss_maps.py:
  python3 tools/generate_swiss_tiles.py /tmp/phreakshow-swiss-raster
The native map is 1:200,000; extra zoom enlarges it rather than adding detail.
"""
import argparse
import json
import math
from pathlib import Path
import zlib
import numpy as np
from PIL import Image
from pyproj import Transformer
from scipy.ndimage import map_coordinates

BOUNDS = (5.85, 45.7, 10.6, 47.95)  # Covers all Switzerland and a small border margin.
TILE = 256


def tile_position(lon, lat, zoom):
    n = 1 << zoom
    return (lon + 180) / 360 * n, (1 - math.asinh(math.tan(math.radians(lat))) / math.pi) / 2 * n


def overview(root, z, x, y):
    factor = 1 << (z - 4)
    path = root / '4' / str(x // factor) / f'{y // factor}.rgb.z'
    image = Image.frombytes('RGB', (TILE, TILE), zlib.decompress(path.read_bytes()))
    left, top = (x % factor) * TILE / factor, (y % factor) * TILE / factor
    return np.array(image.resize((TILE, TILE), Image.Resampling.BILINEAR,
                                 box=(left, top, left + TILE / factor, top + TILE / factor)))


def generate(source, output):
    sheets = []
    for path in sorted(source.glob('*_komb_10_2056.tif')):
        with Image.open(path) as image:
            pixel_x, pixel_y, _ = image.tag_v2[33550]
            tie = image.tag_v2[33922]
            assert tie[:3] == (0.0, 0.0, 0.0)
            assert pixel_x == pixel_y == 10.0
            # 20m sampling suits XYZ zoom 12 (~26m/pixel in Switzerland).
            # Reduce RGB, since reducing palette indices would corrupt colours.
            pixels = np.array(image.convert('RGB').reduce(2))
            sheets.append((tie[3], tie[4], pixel_x * 2, pixels))
        print(f'Loaded {path.name}', flush=True)
    assert len(sheets) == 16
    transform = Transformer.from_crs(4326, 2056, always_xy=True)
    manifest = {'source': 'Swiss Map Raster 200, 2025, colour without shading',
                'attribution': '© swisstopo', 'bounds': BOUNDS, 'native_max_zoom': 12,
                'source_urls': json.loads((source / 'sources.json').read_text()), 'tiles_per_zoom': {}}
    for zoom in [12]:
        west, south, east, north = BOUNDS
        x0, y0 = tile_position(west, north, zoom)
        x1, y1 = tile_position(east, south, zoom)
        count = 0
        world = TILE * (1 << zoom)
        for x in range(math.floor(x0), math.floor(x1) + 1):
            folder = output / str(zoom) / str(x)
            folder.mkdir(parents=True, exist_ok=True)
            for y in range(math.floor(y0), math.floor(y1) + 1):
                longitude = (x * TILE + np.arange(TILE) + 0.5) / world * 360 - 180
                latitude = np.degrees(np.arctan(np.sinh(math.pi * (1 - 2 * (y * TILE + np.arange(TILE) + 0.5) / world))))
                lon, lat = np.meshgrid(longitude, latitude)
                easting, northing = transform.transform(lon, lat)
                pixels = overview(output, zoom, x, y)
                for left, top, resolution, sheet in sheets:
                    height, width, _ = sheet.shape
                    valid = ((easting >= left) & (easting < left + width * resolution) &
                             (northing <= top) & (northing > top - height * resolution))
                    if not valid.any():
                        continue
                    sx = (easting[valid] - left) / resolution - 0.5
                    sy = (top - northing[valid]) / resolution - 0.5
                    for channel in range(3):
                        pixels[:, :, channel][valid] = map_coordinates(sheet[:, :, channel], [sy, sx], order=1, mode='nearest')
                (folder / f'{y}.rgb.z').write_bytes(zlib.compress(pixels.tobytes(), 9))
                count += 1
        manifest['tiles_per_zoom'][str(zoom)] = count
        print(f'Zoom {zoom}: {count} Switzerland tiles', flush=True)
    build_pyramid(output, manifest)
    (output / 'switzerland.json').write_text(json.dumps(manifest, indent=2, ensure_ascii=False)+'\n')


def build_pyramid(output, manifest):
    # Filtering child images removes fine-line aliasing when zooming out.
    for zoom in range(11, 4, -1):
        west, south, east, north = BOUNDS
        x0, y0 = tile_position(west, north, zoom)
        x1, y1 = tile_position(east, south, zoom)
        count = 0
        for x in range(math.floor(x0), math.floor(x1) + 1):
            folder = output / str(zoom) / str(x)
            folder.mkdir(parents=True, exist_ok=True)
            for y in range(math.floor(y0), math.floor(y1) + 1):
                children = Image.new('RGB', (512, 512))
                for dx in range(2):
                    for dy in range(2):
                        cx, cy = x * 2 + dx, y * 2 + dy
                        path = output / str(zoom + 1) / str(cx) / f'{cy}.rgb.z'
                        if path.exists():
                            child = Image.frombytes('RGB', (256, 256), zlib.decompress(path.read_bytes()))
                        else:
                            child = Image.fromarray(overview(output, zoom + 1, cx, cy))
                        children.paste(child, (dx * 256, dy * 256))
                pixels = children.resize((256, 256), Image.Resampling.LANCZOS).tobytes()
                (folder / f'{y}.rgb.z').write_bytes(zlib.compress(pixels, 9))
                count += 1
        manifest['tiles_per_zoom'][str(zoom)] = count
        print(f'Filtered zoom {zoom}: {count} Switzerland tiles', flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('--output', type=Path, default=Path(__file__).resolve().parents[1] / 'assets/map_tiles')
    parser.add_argument('--pyramid-only', action='store_true', help='Refilter existing Swiss zoom-12 tiles')
    args = parser.parse_args()
    if args.pyramid_only:
        path = args.output / 'switzerland.json'
        manifest = json.loads(path.read_text())
        build_pyramid(args.output, manifest)
        path.write_text(json.dumps(manifest, indent=2, ensure_ascii=False)+'\n')
    else:
        generate(args.source, args.output)
