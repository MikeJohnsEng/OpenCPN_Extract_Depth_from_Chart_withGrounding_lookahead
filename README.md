# cm93depth_pi — CM93 and S-57 least depth for OpenCPN

An OpenCPN plugin that reads the vector chart under the vessel (a CM93 cell, or an unencrypted S-57 ENC cell), finds the least charted depth in it, shows that in a small window, marks the spot on the chart, and sends it as an NMEA 0183 sentence on a serial port you choose. It also looks ahead along the course and reports the first charted shoal.

- **Charts:** CM93 cells and unencrypted S-57 ENC cells (`*.000` with their `*.001`, `*.002` … updates), either or both. ENC cells are found in OpenCPN's chart directories (and any extra folders you add) and indexed in the background; the index is kept in OpenCPN's plugin data folder, so later starts are quick, and an exchange set's `CATALOG.031` lets cells be indexed without reading them. Where charts of both kinds cover the vessel, **the most detailed wins**, in this order (least detailed first):

  | Z | A | ENC 1 Overview | B | ENC 2 General | C | ENC 3 Coastal | D | E | ENC 4 Approach | F | ENC 5 Harbour | G | ENC 6 Berthing |
  |---|---|---|---|---|---|---|---|---|---|---|---|---|---|

  CM93 bands are shown by their letter, ENC usage bands by their digit (`5` = harbour), in the window and in the sentences. Encrypted S-63 cells cannot be read.
- **Search mode:**
  - **Depth area here** (default): the minimum (DRVAL1) of the charted depth area the vessel is in, e.g. 15 m in a 15–20 m area. Charted dangers inside that area that have no depth, or are shallower than its minimum, are counted and shown.
  - **Whole cell**: least depth anywhere in the CM93 cell under the vessel (nearly always 0 m near a coast).
  - **Radius**: least depth within 0.05–50 nm of the vessel.
  - **Interpolated from soundings**: depth at the vessel estimated from the four nearest soundings, weighted 1/distance², limited to the charted depth band at the vessel. Soundings are pooled from every scale band and chosen purely by distance. The radius setting is the farthest a sounding may be. An option takes one sounding from each direction (NE, SE, SW, NW) first instead.
- **Areas with no depth count as 0 m:** a depth area charted without a minimum depth (a sand or mud flat), a seabed area that dries, covers and uncovers or is awash, and a seabed area (e.g. sand) with no depth where no depth area is charted. A sand patch inside a charted depth area keeps that area's depth.
- **Rocks, wrecks and obstructions under the vessel** (depth area here and interpolated modes) limit the depth: their charted depth, 0 m when charted as drying, covering and uncovering or awash, and for one with no depth at all (a submerged reef, say) 0 m by default or, as an option, the least depth of the area it lies in (the ECDIS rule). Foul areas, foul ground and fish havens are not counted as dangers anywhere. Dangers charted only on a smaller-scale chart than the one the depth comes from count too (CM93's small-scale cells carry obstruction areas its detailed cells sometimes lack); this can be switched off. But a danger **area** is not counted where a more detailed chart has a sounding inside it, within 0.25 nm (setting) of the vessel, at least the safety depth deep: small-scale charts mark whole zones as obstruction areas, and a detailed sounding of navigable depth inside such a zone beside the vessel is the better evidence. Point dangers always count.
- **Gaps in the depth areas:** CM93 depth areas cover all charted water, so a position inside a hole in them (at the most detailed scale with depth areas all round it) is on something charted without a depth: a reef, rock, islet or unsurveyed area. It is treated as a danger with no charted depth (0 m, or the least depth around it with the ECDIS-rule setting), source `U`. The look-ahead finds gaps in its zone too, exactly: it follows the depth-area edges that cross the zone and finds where the zone first enters a gap, however small. They count as dangers with no charted depth.
- **Land:** a position on charted land gives no depth in any mode (status V, "LAND" in the window). In interpolated mode, soundings with charted land between them and the vessel (the far side of a headland, behind an island) are left out, each checked against the coastline of its own scale band; this can be switched off.
- **Objects counted** (each can be switched off): soundings (`SOUNDG`), depth and dredged areas (`DEPARE`, CM93 `ITDARE`, `DRGARE`), depth contours (`DEPCNT`), and rocks/wrecks/obstructions with a charted depth (`UWTROC`, `WRECKS`, `OBSTRN`). Dangers with *no* charted depth are counted and reported separately.
- **Look-ahead:** a zone ahead of the vessel (a corridor along the course, or a sector centred on it), as long as SOG × a set time or a fixed distance. Reports the least charted depth in it and the **first shoal**: the nearest point ahead shallower than a safety depth, land, or a rock/wreck/obstruction with no charted depth. Sent as a second sentence, `$PCMLA`, and drawn on the chart (dashed green when clear, red with a ring on the first shoal).
- **Grounding alarm** on the OpenCPN PC: when the look-ahead's first shoal is within 6 minutes (at SOG) or 0.2 nm, or the depth at the vessel is below the safety depth, or the vessel is on charted land. It sounds (OpenCPN's beep, or a WAV you choose) every 5 s and opens an always-on-top window, flashing red. **Silence** mutes it for 30 s; **Acknowledge** stops the sound while the condition lasts (window turns amber). It sounds again if it gets worse (time to the shoal halves, or the depth here drops below the safety depth), and clears by itself when the condition clears. Optionally sent as `$IIALR` (IEC 61162-1) on the NMEA output. The watch officer can switch it off completely (**Alarm: on/OFF** button in the depth window, or **Switch alarm off…** in the alarm window, both ask first), e.g. for navigable water shallower than the safety depth; the depth window then shows "GROUNDING ALARM OFF" in amber. It is on again whenever OpenCPN starts, and optionally after a set time. Settings on the Alarm tab.
- **Chart selection:** auto (most detailed chart, CM93 or ENC, whose coverage contains the vessel) or a fixed CM93 scale band (ENC cells then unused).
- **Output:** the plugin's own serial port (port and baud configurable), or any existing OpenCPN NMEA 0183 output connection.

Depths are charted values below chart datum. No tide is applied.

## Getting the source

Clone or download this repository (GitHub: Code > Download ZIP). The build scripts work from wherever the folder is; where this README or the guides say `~/cm93depth_pi`, use the folder you cloned or unzipped to. The manual and the Linux / Raspberry Pi build guide are in `doc/` (LibreOffice `.odt`, with PDF copies).

## Sentence

```
$PCMLD,15.0,4307.000,S,14716.700,E,0.00,,15.0,20.0,A,0,H,,G,A*hh     (depth area here)
$PCMLD,2.3,4247.400,S,14730.720,E,0.57,090,5.0,10.0,S,1,R,1.00,G,A*hh (radius mode)
```

| # | Field | Example |
|---|---|---|
| 1 | Least charted depth, m (negative = drying). Mode H: minimum of the depth area at the vessel. Mode I: interpolated depth at the vessel | `2.3` |
| 2–3 | Latitude of least depth, ddmm.mmm, N/S | `4247.400,S` |
| 4–5 | Longitude of least depth, dddmm.mmm, E/W | `14730.720,E` |
| 6 | Range from vessel, nm (mode H: `0.00`; mode I: nearest sounding used) | `0.57` |
| 7 | True bearing from vessel, ° (empty at range 0) | `090` |
| 8 | DRVAL1 of the depth area the vessel is in, m | `5.0` |
| 9 | DRVAL2 of that area, m | `10.0` |
| 10 | Source: S sounding, A depth area, D dredged area, O obstruction, W wreck, R rock, C contour, I interpolated, U gap in the depth areas (reef, islet, unsurveyed) | `S` |
| 11 | Dangers in scope with no charted depth. Mode H: dangers inside the area with no depth or shallower than its minimum | `1` |
| 12 | Search mode: H depth area here, C whole cell, R radius, I interpolated | `R` |
| 13 | Radius, nm (empty in cell mode; mode I: farthest sounding used) | `1.00` |
| 14 | Chart band used: CM93 letter `Z`…`G`, or ENC usage band `1`…`6` | `G` |
| 15 | Status: A valid, V invalid (no/stale fix, no chart, no depths) | `A` |

Empty fields mean "not available". With "Keep sending with status V" on (the default), a sentence still goes out every interval when there is no valid data, so a receiver can tell *no data* from *link dead*. The address `PCMLD` can be changed in Preferences (must be `P` plus four capitals/digits). The worst case is 75 characters, inside the NMEA 0183 limit of 82.

## Look-ahead sentence

```
$PCMLA,1.0,1.92,090,S,5.0,1.00,10.0,079,W,1,1.00,C,2.00,0.25,E,A*2C
```

| # | Field | Example |
|---|---|---|
| 1 | Least charted depth in the zone, m below chart datum (land not counted) | `1.0` |
| 2 | Range from the vessel to it, nm | `1.92` |
| 3 | True bearing to it, ° (empty at range 0) | `090` |
| 4 | Its source: S A D O W R C as in `$PCMLD` | `S` |
| 5 | Safety depth, m (the setting) | `5.0` |
| 6 | Distance ahead to the first shoal, nm. Along the course for a corridor, range for a sector. Empty = none in the zone | `1.00` |
| 7 | Time to reach it at the current SOG, minutes. Empty when SOG is below the minimum and the heading is used | `10.0` |
| 8 | True bearing to it, ° | `079` |
| 9 | Its source: S A D O W R C, L land, or U a gap in the depth areas (reef, islet, unsurveyed) | `W` |
| 10 | Rocks, wrecks and obstructions with no charted depth in the zone | `1` |
| 11 | Distance ahead to the nearest of them, nm | `1.00` |
| 12 | Zone: C corridor, S sector | `C` |
| 13 | Zone length (sector radius), nm | `2.00` |
| 14 | Corridor half-width, nm, or sector half-angle, ° | `0.25` |
| 15 | Least detailed chart band used along the zone (as field 14) | `E` |
| 16 | Status: A valid, V invalid (no or stale fix, no chart, no course) | `A` |

"First shoal" means anything ahead charted shallower than the safety depth (sounding, depth area, dredged area, contour, danger with a depth), any land, and, with the option on (the default), any rock, wreck or obstruction with no charted depth. Field 6 tells a receiver how far away it is and field 7 how soon; together with field 8 it tells which side.

How the zone is searched:

- **Course:** COG while SOG is at least the minimum (0.5 kn). Below that the heading is used, the zone has its shortest length, and no times are given. With neither, status V.
- **Length:** time basis (default) SOG × 10 min, never shorter than 0.5 nm or longer than 5 nm; or a fixed distance. Never more than 50 nm.
- **Scale:** the zone is cut into slices of about 0.1 nm along its length. Each slice uses the most detailed scale band that charts all of it (has a depth area, dredged area or land area at each test point). Where none does, every band charting part of it is used. So a detailed chart is followed where it exists, and generalised small-scale shoals are not mixed in under it. Field 15 shows the coarsest band used.
- **Exact clipping:** areas and lines are clipped to the zone edge by edge, so a shoal that only touches the edge of the corridor is found at the point where it enters.

The worst case is 72 characters, inside the NMEA 0183 limit. The address `PCMLA` can be changed in Preferences.

## Building

The plugin targets plugin API 1.18, so it loads in OpenCPN 5.8 and later. It has to be built with the same wxWidgets version and compiler family as your OpenCPN.

### Linux and Raspberry Pi (OpenCPN from apt or the OpenCPN PPA)

```sh
bash cm93depth_pi/linux/build_linux.sh
```

It installs the build tools and wxWidgets development files with apt, builds `libcm93depth_pi.so` for this PC (x86_64, arm64 or armhf), and packages it as a tarball in `linux/dist/`. Install that on any PC with the same distribution release and architecture through **Options → Plugins → Import plugin…**, then enable the plugin. Needs OpenCPN 5.8 or later (Raspberry Pi OS / Debian 12 bookworm ship 5.6). The Flatpak OpenCPN needs a Flatpak-SDK build, not set up yet. Details: `linux/README-linux.txt`.

### Windows

Install Visual Studio 2022 Community with "Desktop development with C++" and 7-Zip, then double-click `windows\build_windows.bat`. It matches your installed OpenCPN (32/64-bit, wxWidgets version), builds the DLL and packages it as a plugin tarball in `windows\dist\`. Install that on any PC with the same type of OpenCPN through **Options → Plugins → Import plugin…**, then enable the plugin in the list. Details and troubleshooting: `windows\README-windows.txt`.

Building in-tree inside an OpenCPN source checkout also works.

### macOS

```sh
cmake -S cm93depth_pi -B build -DOCPN_API_DIR=/path/to/OpenCPN/include
cmake --build build
cp build/libcm93depth_pi.dylib "$HOME/Library/Application Support/OpenCPN/Contents/PlugIns/"
```

### In-tree on any platform

Drop the folder into `plugins/` of an OpenCPN checkout. OpenCPN's build picks up every `*_pi` folder there.

## Using it

1. Enable the plugin. A toolbar button (a sloping seabed with a red depth arrow) toggles the display window. The window starts minimised: the depth, what it is, and a red shoal warning when the look-ahead has one. **Enlarge** shows everything below; **Minimise** goes back. The choice is remembered.
2. Open **Settings…** (in the window) or Options → Plugins → Preferences. There are three tabs:
   - **Chart sources:** CM93 and/or S-57 ENC. ENC cells are found in OpenCPN's chart directories; add other folders if they live elsewhere. **Rescan ENC cells** picks up new or updated cells at once (done by itself at start-up and when OpenCPN's chart directories change).
   - **CM93 charts:** auto-detect finds the CM93 folder among OpenCPN's chart directories. Otherwise, point it at the folder that holds the 8-digit cell folders and `CM93OBJ.DIC`.
   - **Search area:** depth area here, whole cell, radius, or interpolated from the nearest soundings.
   - **NMEA output:** choose *Serial port*, then the port (e.g. `/dev/ttyUSB0`, `COM5`) and baud (4800 standard, 38400 for 0183-HS).
   - If OpenCPN already has that port open as a connection, the plugin can't open it as well. Choose *OpenCPN output connection* instead and pick it from the list. That connection must have output enabled, and any output filter must let `PCMLD` and `PCMLA` through.
   - **Look-ahead** tab: zone shape, time or distance, corridor half-width or sector half-angle, safety depth (draft + under-keel margin + an allowance for chart error), and whether undepthed dangers count as shoals.
3. The window shows:
   - the least depth,
   - range and bearing to it, and the object it came from,
   - the depth band you're in,
   - undepthed dangers,
   - the cell used, and output status,
   - the last sentence sent,
   - with the look-ahead on: the first shoal ahead (in red) or "Clear ahead", the least depth ahead, undepthed dangers, the zone used and the scales along it, and the last `$PCMLA`.

   A temporary (unsaved) mark is placed at the least-depth spot.

All defaults, with what each does, are in the **USER SETTINGS** block at the top of `src/cm93depth_pi.cpp`.

## Things to know

- **Whole-cell mode almost always reports a drying or 0 m value near any coast.** That is correct for "least depth in the cell", but not useful for steering; the default "depth area here" mode is the local figure.
- **Depth area here** reports the band the chart gives for your position, not a measured depth. Watch the "inside this area" danger lines: a rock charted at 1 m inside a 15–20 m area is shown in red and counted in field 11.
- **Interpolated depth is an estimate between charted soundings, not a charted value.** The seabed between soundings is unknown; the window lists the soundings used, the shallowest of them, and whether they surround the vessel. Treat "not surrounded" (all soundings to one side) with extra caution.
- **No chart mark in depth-area-here or interpolated mode,** since the value applies at the vessel itself. Whole-cell and radius modes mark the least-depth spot.
- **The look-ahead knows only what is charted.** A corridor narrower than your cross-track error, leeway or a planned turn will miss shoals you may meet; use a wider corridor or a sector when manoeuvring. A sector catches more abeam as it gets longer. The safety depth is compared with charted depths below chart datum; no tide is applied.
- **Look-ahead drawing in OpenGL mode** uses OpenGL's fixed-function drawing, as many plugins do. If the zone doesn't appear with OpenGL on, untick "Draw the zone" and report it; the sentence and the window are unaffected.
- **Check it against OpenCPN first.** The decoder has been tested only on synthetic CM93 cells built to the format OpenCPN reads, not on real CM93 data. Before relying on it, compare a few results against OpenCPN's right-click **Object Query** on the same spot (same depth areas and soundings, same positions).
- **Offsets:** the east/north offset in Preferences applies to every cell, with the same sign as OpenCPN's CM93 Offset dialog. Per-cell offsets you've set inside OpenCPN are not read.
- **Soundings are read as OpenCPN reads them.** OpenCPN's CM93 loader keeps only the first sounding of a record that holds several (its write pointer moves one point per record, so the next record overwrites the rest). The plugin does the same by default, so it uses exactly the soundings drawn on the chart. `OpenCpnSoundings=0` under `[PlugIns/cm93depth_pi]` in the config file uses every stored sounding instead. "Copy report" lists the nearest soundings in each scale band.
- **S-57 ENC cells** are read as OpenCPN reads them: updates are applied in sequence up to the first missing or out-of-sequence one, and a cell cancelled by an update is still used, as OpenCPN still shows it ("Copy report" lists the updates applied and any problem). In ENCs, pontoons, floating docks, hulks and unsurveyed areas are left uncovered by depth areas, so the plugin treats them like a gap (a danger with no charted depth). The ENC reader was checked against real cells (GDAL's S-57 test data) and synthetic ones; compare a few results with OpenCPN's Object Query before relying on it.
- **Data limits:** CM93 data is old and has no survey-quality (`CATZOC`) information. Treat the output as advisory. If you feed it into a control system, treat status `V`, or no sentence for a few intervals, as "no chart data".
- **Compressed cells:** `.xz` cells are read when built with liblzma (detected automatically by CMake).

## Files

```
src/cm93_reader.*    CM93 cell decoder (no wx/OpenCPN dependency)
src/s57_reader.*     ISO 8211 + S-57 ENC reader with updates (no wx/OpenCPN dependency)
src/s57_codes.cpp    S-57 object and attribute catalogue (from OpenCPN's s57data)
src/enc_index.*      finds and indexes ENC cells in the chart folders, with a cache
src/depth_engine.*   chart band selection (CM93 + ENC), coverage test, least-depth search
src/lookahead.cpp    look-ahead zone, slicing by scale, exact clipping
src/nmea_out.*       $PCMLD, $PCMLA and $IIALR sentences and checksum
src/alarm_logic.h    grounding alarm states (active / silenced / acknowledged)
src/alarm_dialog.*   alarm window
src/serial_port.*    write-only serial port, POSIX + Win32
src/cm93depth_pi.*   plugin glue, settings, timer, chart mark, output
src/depth_panel.*    display window
src/prefs_dialog.*   preferences
src/icons.*          toolbar icon drawn in code
linux/               build_linux.sh: build + plugin tarball (Linux, Raspberry Pi)
windows/             build_windows.bat/.ps1: build + plugin tarball (Windows)
tests/               unit tests (cmake -DCM93DEPTH_BUILD_TESTS=ON)
```

## Licence

GPL v2 or later. The CM93 format handling follows OpenCPN's `gui/src/cm93.cpp`; the S-57 handling follows IHO S-57 edition 3.1 and OpenCPN's `libs/s57-charts` / GDAL ISO 8211 code.
