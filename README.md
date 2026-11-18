# WSPR Scheduler Map 3

Shows your WSPR reports on a world map: the stations you heard (blue pins, green great-circle paths)
and the stations that heard you (red pins, purple paths), from the WSPR Scheduler databases or straight
from [wspr.live](https://wspr.live), with the night side of the world shaded.
Written in C++ with Qt 6; a port of the C# *WSPR Scheduler Map 2* by Neville, G4GCI.

## Features

- **Map:** OpenStreetMap, drawn by the program itself (no web browser component): drag to move, mouse
  wheel, double click or + / − to zoom, hover over a pin for the callsign. Map tiles are kept on the PC.
- **Filters:** period (10 minutes to 28 days), band, *de-clutter* (only stations at least so far away,
  in miles or km), TX and/or RX, paths on or off, hide special calls (Q, 0 or 1).
- **Auto track:** the map updates every 10 minutes.
- **wspr.live:** the map for any callsign (up to 24 hours), without the databases.
- **Keeps going:** if MySQL isn't there, the map says why and tries again at the next update.

## Where the reports come from

- `wspr_rpt.received` - the stations you heard, written by WSPR Scheduler.
- `wspr_rx.reported` - the stations that heard you, written by WSPR Scheduler Live.

The database password is read from `DBmapcredential` in your user folder (as WSPR Scheduler Map 2), or
the default one; change it with Config.

## Requirements

- Qt 6.4 or later: Widgets, Sql and Network, plus the QMYSQL driver.
- MySQL or MariaDB with the WSPR Scheduler databases.
- An internet connection (map tiles from tile.openstreetmap.org; wspr.live).

## Building

```
cmake -S . -B build
cmake --build build
```

Tests: configure with `-DWSPRMAP_TESTS=ON`. The database tests use scratch databases
(`wspr_map_test_rpt`, `wspr_map_test_rx`) on a local MySQL, never the real ones.
The Windows installer for SourceForge is made with `installer\make_release.bat` (see installer\README.md).

## Linux

On Ubuntu, Debian or Raspberry Pi OS one script installs everything and builds the program:

```
wget https://raw.githubusercontent.com/npalmer1/wspr-scheduler-map-3/main/linux/install.sh
bash install.sh
```

Run it again to update. See [linux/README.md](linux/README.md).

## Licence

GNU General Public License v3.0. See [LICENSE](LICENSE).
Map data © [OpenStreetMap](https://www.openstreetmap.org/copyright) contributors.
