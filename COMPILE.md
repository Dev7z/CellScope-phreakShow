# Building CellScope on Windows

CellScope is built with the **MSYS2 / MinGW-w64** toolchain (GCC 15.x) using
**CMake** and **Ninja**. The build is reproducible from a clean MSYS2 install.

> Important: GCC must be invoked from inside the **MINGW64** environment. If you
> run `g++.exe` directly from PowerShell/cmd without that environment set up,
> `cc1plus` dies silently with exit code 1 and no diagnostics. Always build from
> the **MSYS2 MINGW64** shell (or set `MSYSTEM=MINGW64` before launching bash).

## 1. Install MSYS2

Download and install from <https://www.msys2.org>, then open the
**"MSYS2 MINGW64"** shell (not the plain "MSYS2 MSYS" shell) from the Start menu.

Update the package database once:

```bash
pacman -Syu
# close and reopen the MINGW64 shell if it asks you to, then:
pacman -Su
```

## 2. Install build tools and dependencies

```bash
pacman -S --needed \
  mingw-w64-x86_64-toolchain \
  mingw-w64-x86_64-cmake \
  mingw-w64-x86_64-ninja \
  mingw-w64-x86_64-pkgconf \
  mingw-w64-x86_64-glfw \
  mingw-w64-x86_64-rtl-sdr \
  mingw-w64-x86_64-hackrf \
  mingw-w64-x86_64-libusb \
  mingw-w64-x86_64-zstd \
  mingw-w64-x86_64-ogg \
  mingw-w64-x86_64-vorbis \
  mingw-w64-x86_64-fftw
```

The phreakShow antenna map also needs SQLite, curl and JPEG decoding:

```bash
pacman -S --needed mingw-w64-x86_64-sqlite3 mingw-w64-x86_64-curl mingw-w64-x86_64-libjpeg-turbo
```

These provide: GCC/G++, CMake, Ninja, pkg-config, GLFW (windowing), librtlsdr +
libusb (RTL-SDR), HackRF, and zstd (SDR++ server compression). OpenGL and zlib
ship with the toolchain. libogg + libvorbis provide OGG Vorbis voice recording.
**FFTW (single precision, `fftw3f`)** is used by the vendored LTE stack.

Dear ImGui, ImPlot, the JAERO DSP, mbelib, libacars, and miniaudio
are vendored in `third_party/`, as are srsRAN2 + FALCON + LTESniffer (the LTE
decode stack, under `third_party/{srsran,falcon,ltesniffer,lte}`). None need a
separate install.

### LTE stack (srsRAN / FALCON / LTESniffer)

The LTE eavesdropper stack is vendored and built as static libraries, wired into
CellScope behind the `HAS_LTE` CMake option (ON by default). To build without it:

```bash
cmake -S . -B build -G Ninja -DHAS_LTE=OFF
```

All srsRAN/FALCON/LTESniffer sources compile with MinGW-specific compatibility
shims kept in `third_party/lte/compat/`. Only downlink decoding is supported on
RTL-SDR/HackRF/Airspy; uplink sniffing needs USRP-class hardware.

### Optional: Airspy support

```bash
pacman -S --needed mingw-w64-x86_64-libairspy
```

Airspy headers are vendored in `third_party/airspy/`. The build automatically
enables Airspy (`HAS_AIRSPY=1`) when libairspy is found by CMake.

## 3. Configure and build

From the **MINGW64** shell, in the project root:

```bash
cmake -S . -B build -G Ninja
ninja -C build
```

The executable and the runtime DLLs it needs are placed in `build/`:

```
build/CellScope.exe
build/libgcc_s_seh-1.dll, libwinpthread-1.dll, libstdc++-6.dll,
      glfw3.dll, librtlsdr.dll, libhackrf.dll, libusb-1.0.dll,
      libzstd.dll, zlib1.dll, libogg-0.dll, libvorbis-0.dll,
      libvorbisenc-2.dll
```
(plus `build/libairspy.dll` when Airspy support is enabled)

The DLLs are copied next to the `.exe` automatically (POST_BUILD step), so it
runs standalone from a double-click or from PyCharm without MSYS2 on `PATH`.

Run it:

```bash
./build/CellScope.exe
```

## 4. Building from PowerShell (optional)

If you prefer to drive the build from PowerShell, you must enter the MINGW64
environment first. For example:

```powershell
$env:MSYSTEM = "MINGW64"
$env:CHERE_INVOKING = "1"
& C:\msys64\usr\bin\bash.exe -lc "cd /c/path/to/CellScope && ninja -C build"
```

The `-l` (login) shell with `MSYSTEM=MINGW64` sources `/etc/profile`, which puts
`/mingw64/bin` on `PATH` so `cc1plus` can find its runtime. `CHERE_INVOKING=1`
keeps the current directory.

## Notes / troubleshooting

- **First build is slow (~1 min on `implot_items.cpp`).** ImPlot's
  `implot_items.cpp` is extremely template-heavy; at `-O2` it takes ~5 minutes
  to compile. CMakeLists.txt overrides it to `-O1` (last `-O` flag wins) so it
  builds in about a minute. This is intentional, not a hang.
- **`cc1plus` exits 1 with no output.** You are not in the MINGW64 environment.
  Build from the MSYS2 MINGW64 shell (see the warning above).
- **Link fails with "Access is denied" on `CellScope.exe`.** A previous instance
  is still running and is locking the file. Close it, then rebuild.
- **Incremental builds.** Re-run `ninja -C build`. Only changed files recompile.
  After editing `CMakeLists.txt`, re-run `cmake -S . -B build` first.
- **Clean rebuild.** Delete the `build/` directory and re-run the configure +
  build steps. (Avoid `--clean-first`; it wipes the vendored objects and forces
  the slow `implot_items.cpp` recompile.)

## phreakShow map

Place `switzerland.db` in the project root before building, or beside the
executable afterwards. The build copies it beside the executable when present.
The map opens it read-only; it never adds indexes or changes records. You can
also enter a different database path under **Your coordinates / database**.

Select the **phreakShow** workspace and its **Intercom** tab. Below zoom 14, each visible coordinate gets
one mast marker if any antenna there belongs to an enabled company. Sunrise is
red, Swisscom blue, Salt green, and shared masts white. At zoom 14 and above,
each enabled antenna gets a direction line: 0 degrees points north, 90 east.
Line length is fixed and indicates direction, not predicted coverage.

**Basel SBB** centers the map on the station at zoom 13. Expand **Your coordinates
/ database**, enter WGS84 longitude and latitude, and press **Set user point**.
Your point is cyan. **Go to user (zoom 13)** centers the map on that saved point.
This also enables distance searches in **Nearby antennas / tuning**.

Click a mast or direction line to select an antenna. When several antennas
overlap, choose a row in the picker. **Antenna details** shows every database
field and the other antennas at the same exact coordinates. Selecting a hidden
company's antenna enables that company's checkbox. Selection is highlighted in
yellow. **Show selected on map** centers it at zoom 14.

The nearby panel searches either **K nearest antennas** or **All within radius**
(kilometers). It honors company checkboxes, retains separate antenna records at
the same mast, and sorts by great-circle distance from your saved coordinates.
Enable **Count antennas at the same coordinates as one** to count K distinct
locations while retaining every matching antenna at those locations. Each
location gets its own table background shade. The option is disabled for
radius searches, which show a cyan distance boundary around your user point.
Selecting a row updates the same map selection and details. **Set CellScope
center to selected antenna** transfers its exact MHz value to the primary radio;
existing decoders are preserved. For WAV sources it changes the frequency label.

The **settings** tab contains **BG music**. It starts
`iknowyouregettingfasterbuticatchthemall.mp3` in a continuous loop; unchecking it
stops playback. Keep the MP3 beside the executable (the build copies it there
when present in the project root). Playback continues when you change tabs.
The vendored miniaudio library handles MP3 decoding and audio output. If playback
cannot start, hover the checkbox for the error and check the console output.

**Satellite / aerial** is available at zoom 13 and above. It downloads official
swisstopo SWISSIMAGE aerial tiles on a background thread and stores them in
`assets/satellite_cache` beside the executable. Previously cached tiles work
offline. Until imagery is ready, or outside available imagery, the offline map
is shown. **Retry aerial tiles** retries failed requests. Service documentation:
<https://docs.geo.admin.ch/visualize-data/xyz.html>.

Drag the CellScope or phreakShow workspace tabs to reorder, split or detach
them. The three phreakShow panel tabs can also be moved independently. On
platforms where GLFW supports multiple viewports, dragging outside the app
creates a native window. **View > Reset Layout** restores the default arrangement.
Window arrangements are saved in `cellscope.ini`.
On Linux the app prefers X11/XWayland when available, because ImGui's GLFW
backend currently disables native multi-window viewports on Wayland.

### Where to change things

- `src/gui/antenna_database.*`: SQLite loading, exact coordinate grouping,
  distance calculations, viewport filtering, and nearest/radius queries.
- `src/gui/map_view.cpp`: map controls, projection, antenna direction lines,
  hit testing and highlights. `sectorLength` controls line length in pixels.
- `src/gui/map_tiles.*`: offline tile loading, aerial downloads, JPEG decoding,
  disk caching and OpenGL texture lifetime.
- `src/gui/phreakshow.cpp`: workspace arrangement, details/nearby tables and
  CellScope frequency handoff.
- `src/gui/phreakshow_state.h`: internal state shared by the canvas and panels.
- `src/gui/phreakshow.h`: public `mapPoints` list and `addMapPoint()` API.

External functions can still add their own points on the UI thread:

```cpp
#include "gui/phreakshow.h"
phreakshow::addMapPoint(8.393766, 46.827407, phreakshow::MapProvider::Sunrise);
// Manage these custom points without modifying antenna database records:
phreakshow::mapPoints.clear();
```

After building, run the map and database tests with:

```bash
ctest --test-dir build --output-on-failure
# Validate this supplied database snapshot (244823 antennas / 20259 masts):
./build/antenna_database_selftest switzerland.db
```
