# Offline Phreakshow map

Bundled world overview with country boundaries and selected country labels,
using Natural Earth 1:110m countries. This is an overview, without street detail.
The bundled overview works offline. Zoom levels 0–4 contain all 341 world XYZ
tiles in Web Mercator, covering latitudes ±85.05112878 degrees.

Switzerland has additional tiles at zoom levels 5–12, generated from the 2025
Swiss Map Raster 200 colour sheets, with towns, roads, railways, lakes and
terrain contours. Coverage bounds are 5.85–10.60°E, 45.70–47.95°N; see
`switzerland.json` for counts and source downloads. This is a 1:200,000 national
map, with town/regional detail. At zoom levels 13–18, the renderer requests
native-resolution tiles outside the bundled Basel region from swisstopo's `ch.swisstopo.pixelkarte-farbe` layer,
including street and building detail where provided. Turning on Satellite /
aerial uses `ch.swisstopo.swissimage` at the same zoom levels. Both use the
official [XYZ service](https://docs.geo.admin.ch/visualize-data/xyz.html).
Downloaded JPEGs are cached beside the executable in `assets/map_cache` and
`assets/satellite_cache` respectively. While tiles load or network access is
unavailable, the renderer enlarges an available parent or bundled offline tile.

Basel and surroundings (7.52–7.67°E, 47.51–47.61°N) also have **2,212 bundled
map and aerial JPEGs** in `basel/map` and `basel/sat`. These are shipped assets,
independent of the two disposable cache directories. Both layers work with
empty caches and no network: zooms 13–16 use the corresponding bundled tiles,
and zooms 17–18 enlarge zoom 16. No network request is made for covered tiles.
The JPEGs total about 60 MB; `basel/manifest.json` records bounds, layer names,
download time and SHA-256 hashes. Imagery outside Switzerland may have limited
detail, as provided by swisstopo. Regenerate or verify it with curl and Pillow:

```
python3 tools/download_basel_tiles.py
python3 tools/download_basel_tiles.py --verify-only
```

Swiss source: https://www.swisstopo.admin.ch/en/national-map-swiss-map-raster-200
Swiss download catalogue: https://data.geo.admin.ch/api/stac/v1/collections/ch.swisstopo.pixelkarte-farbe-pk200.noscale/items
Attribution: © swisstopo
Terms permitting processing and distribution with attribution:
https://www.swisstopo.admin.ch/en/terms-of-use-free-geodata-and-geoservices

Data source: https://github.com/nvkelso/natural-earth-vector/blob/master/geojson/ne_110m_admin_0_countries.geojson
Natural Earth data is public domain: https://www.naturalearthdata.com/about/terms-of-use/

Each `z/x/y.rgb.z` is a zlib-compressed, top-to-bottom 256 × 256 RGB image
(196608 bytes uncompressed). The existing zlib dependency loads these tiles;
JPEG and curl handle the optional detailed online layers.
The bundled Basel JPEGs also use the JPEG decoder on the background worker.

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

The Swiss overview download tool fetches 16 colour sheets (~213 MB) via the
official file service. The separate Basel pack tool downloads the bounded area
through swisstopo's official XYZ endpoint, using four concurrent requests.
Raw overview GeoTIFFs
are only needed for generation and are not shipped with the application.

CMake copies the tiles beside the executable for out-of-tree builds. At runtime
Phreakshow looks beside the executable, in the working directory, then in the
source directory recorded at build time. Ship the `assets/map_tiles` directory
with the executable.

The default user point is Spiegelgasse 1, 4051 Basel, from the official
[swisstopo address search](https://api3.geo.admin.ch/rest/services/api/SearchServer?searchText=Spiegelgasse%201%20Basel&type=locations&origins=address&sr=4326&limit=5).
Edit the always-visible coordinate controls and choose Set user point to change
it. Map controls, coordinates and database controls share a row when space
allows. The translucent grey crosshair marks the map center; center and cursor
coordinates are shown below the map.

Map navigation with hotkeys is enabled by default. Use WASD to pan, 1/2/3 to toggle
Swisscom/Sunrise/Salt, Q/E to shorten/lengthen azimuth rays, and +/- (including
the numeric keypad) to zoom. X toggles aerial imagery at zoom 13+. C sets the
user point to the center; V sets it to
the cursor position when the cursor is over the map. Pan speed is constant in
screen pixels, so ground distance scales with zoom. Hotkeys work while the map
is focused or hovered and pause while editing a field or using a popup.

Nearby searches have their own Sunrise/Swisscom/Salt checkboxes, independent
of the map visibility checkboxes. Both the map and nearby searches have independent **Include indoor** toggles,
checked by default. The nearby **Order** column counts distinct locations from
the closest onward, with the same order number for every co-located record.
By antennas shows the matching records; By frequency groups exactly those
records by frequency and shows each count.
Selecting or hovering a frequency highlights its matching nearby antennas in
the other tables and on the map. Selecting an individual antenna returns to a
single selection. The optional map restriction still applies both the nearby
results and the map's visibility checkboxes.

The initial view shows Switzerland; the Switzerland button returns there.
Reset view returns to the world overview. Loaded textures are cached and old
entries are evicted as the map is explored to bound GPU memory usage.

Drag with the left mouse button to pan; scroll or use +/- to zoom. Scrolling
keeps the point beneath the cursor fixed. Longitude wraps at the antimeridian;
latitude stops at the Web Mercator limits. The label displays decimal degrees
with positive values north/east and negative values south/west. Outside the map
(including beyond the polar limits) it displays `--`.
