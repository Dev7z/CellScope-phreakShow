#!/usr/bin/env python3
"""Download 2025 Swiss Map Raster 200 colour sheets from the OGD catalogue."""
import json
from pathlib import Path
import subprocess
import sys

catalogue = json.loads(Path(sys.argv[1]).read_text())
folder = Path(sys.argv[2])
folder.mkdir(parents=True, exist_ok=True)
assets = []
for feature in catalogue['features']:
    if feature['id'].startswith('swiss-map-raster200_2025_'):
        for name, asset in feature['assets'].items():
            if '_komb_10_2056.tif' in name:
                assets.append((name, asset['href']))
assert len(assets) == 16, f'Expected 16 colour sheets, found {len(assets)}'
for name, url in sorted(assets):
    target = folder / name
    if target.exists():
        continue
    print(f'Downloading {name}', flush=True)
    subprocess.run(['curl', '-L', '--fail', '--retry', '2', '--max-time', '180', '-sS', '-o', str(target)+'.part', url], check=True)
    Path(str(target)+'.part').rename(target)
(folder/'sources.json').write_text(json.dumps(dict(assets), indent=2)+'\n')
