#!/usr/bin/env python3
"""Build/verify the small, redistributable Basel offline map and aerial pack.

Requires curl and Pillow. Four concurrent downloads, with retries; reruns reuse
verified JPEGs. Runtime uses this pack independently of the disposable caches.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path
import subprocess

from PIL import Image

BOUNDS = (7.52, 47.51, 7.67, 47.61)  # west, south, east, north; Basel and surroundings
LAYERS = {"map": "ch.swisstopo.pixelkarte-farbe", "sat": "ch.swisstopo.swissimage"}
ROOT = Path(__file__).resolve().parents[1] / "assets/map_tiles/basel"


def tiles():
    west, south, east, north = BOUNDS
    for layer in LAYERS:
        for z in range(13, 17):
            n = 1 << z
            def y(lat):
                return int((1 - math.asinh(math.tan(math.radians(lat))) / math.pi) / 2 * n)
            for x in range(int((west + 180) / 360 * n), int((east + 180) / 360 * n) + 1):
                for row in range(y(north), y(south) + 1):
                    yield f"{layer}/{z}/{x}/{row}.jpeg"


def valid(path):
    try:
        with Image.open(path) as image:
            if image.format != "JPEG" or image.size != (256, 256):
                return False
            image.load()
            return True
    except (OSError, ValueError):
        return False


def download(key):
    target = ROOT / key
    if not valid(target):
        target.parent.mkdir(parents=True, exist_ok=True)
        layer, coordinate = key.split("/", 1)
        url = f"https://wmts.geo.admin.ch/1.0.0/{LAYERS[layer]}/default/current/3857/{coordinate}"
        temporary = target.with_suffix(".part")
        subprocess.run(["curl", "-L", "--fail", "--retry", "3", "--retry-delay", "1",
                        "--connect-timeout", "10", "--max-time", "45", "-sS",
                        "--user-agent", "CellScope-phreakShow-offline-pack/1.0",
                        "-o", str(temporary), url], check=True)
        if not valid(temporary):
            raise ValueError(f"Invalid JPEG: {url}")
        temporary.replace(target)
    return key, hashlib.sha256(target.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--verify-only", action="store_true")
    args = parser.parse_args()
    keys = list(tiles())
    if args.verify_only:
        manifest = json.loads((ROOT / "manifest.json").read_text())
        assert set(manifest["sha256"]) == set(keys), "Incomplete tile manifest"
        for key in keys:
            assert valid(ROOT / key), f"Invalid/missing tile: {key}"
            assert hashlib.sha256((ROOT / key).read_bytes()).hexdigest() == manifest["sha256"][key], key
        print(f"Verified {len(keys)} bundled Basel tiles; no network or runtime cache used.")
        return
    hashes = {}
    with ThreadPoolExecutor(max_workers=4) as pool:
        for key, digest in pool.map(download, keys):
            hashes[key] = digest
            if len(hashes) % 100 == 0:
                print(f"Basel tiles: {len(hashes)}/{len(keys)}", flush=True)
    manifest = {
        "bounds_wgs84": BOUNDS, "min_zoom": 13, "max_zoom": 16,
        "downloaded_utc": datetime.now(timezone.utc).isoformat(),
        "attribution": "© swisstopo", "layers": LAYERS,
        "source": "https://docs.geo.admin.ch/visualize-data/xyz.html",
        "terms": "https://www.swisstopo.admin.ch/en/terms-of-use-free-geodata-and-geoservices",
        "sha256": hashes,
    }
    (ROOT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Bundled {len(hashes)} Basel map/aerial tiles.")


if __name__ == "__main__":
    main()
