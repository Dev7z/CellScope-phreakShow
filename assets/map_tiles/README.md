# Offline Phreakshow map

Bundled world overview with country boundaries and selected country labels,
using Natural Earth 1:110m countries. This is an overview, without street detail.
The map never makes network requests. Zoom levels 0–4 contain all 341 world XYZ
tiles in Web Mercator, covering latitudes ±85.05112878 degrees.

Switzerland has additional tiles at zoom levels 5–12, generated from the 2025
Swiss Map Raster 200 colour sheets, with towns, roads, railways, lakes and
terrain contours. Coverage bounds are 5.85–10.60°E, 45.70–47.95°N; see
`switzerland.json` for counts and source downloads. This is a 1:200,000 national
map, with town/regional detail. Zoom levels 13–16 enlarge the native zoom-12
tiles; they do not add street/building detail. Outside detailed coverage the
renderer enlarges the nearest available parent tile, including the world map.

Swiss source: https://www.swisstopo.admin.ch/en/national-map-swiss-map-raster-200
Swiss download catalogue: https://data.geo.admin.ch/api/stac/v1/collections/ch.swisstopo.pixelkarte-farbe-pk200.noscale/items
Attribution: © swisstopo
Terms permitting processing and distribution with attribution:
https://www.swisstopo.admin.ch/en/terms-of-use-free-geodata-and-geoservices

Data source: https://github.com/nvkelso/natural-earth-vector/blob/master/geojson/ne_110m_admin_0_countries.geojson
Natural Earth data is public domain: https://www.naturalearthdata.com/about/terms-of-use/

Each `z/x/y.rgb.z` is a zlib-compressed, top-to-bottom 256 × 256 RGB image
(196608 bytes uncompressed). The existing zlib dependency loads these tiles;
no extra runtime image library is needed.

To regenerate with Python and Pillow, download the source above and run:

```
python3 tools/generate_map_tiles.py /path/to/ne_110m_admin_0_countries.geojson
```

To regenerate Switzerland tiles, fetch the catalogue above into a JSON file.
With Python, Pillow, numpy, scipy and pyproj installed, run:

```
python3 tools/download_swiss_maps.py /path/to/catalogue.json /tmp/swiss-maps
python3 tools/generate_swiss_tiles.py /tmp/swiss-maps
```

The download tool fetches 16 colour sheets (~213 MB) via the official file
service. It never scrapes WMTS or OpenStreetMap tile services. Raw GeoTIFFs
are only needed for generation and are not shipped with the application.

CMake copies the tiles beside the executable for out-of-tree builds. At runtime
Phreakshow looks beside the executable, in the working directory, then in the
source directory recorded at build time. Ship the `assets/map_tiles` directory
with the executable.

The initial view shows Switzerland; the Switzerland button returns there.
Reset view returns to the world overview. Loaded textures are cached and old
entries are evicted as the map is explored to bound GPU memory usage.

Drag with the left mouse button to pan; scroll or use +/- to zoom. Scrolling
keeps the point beneath the cursor fixed. Longitude wraps at the antimeridian;
latitude stops at the Web Mercator limits. The label displays decimal degrees
with positive values north/east and negative values south/west. Outside the map
(including beyond the polar limits) it displays `--`.
