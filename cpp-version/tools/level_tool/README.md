# level_tool

Converts FreedroidClassic Paradroid.maps files to Tiled Map Editor TMX format.

## Usage

```bash
# Use all defaults
./level_tool --convert

# Override input file
./level_tool --convert --input /path/to/Paradroid.maps

# Override output directory
./level_tool --convert -o ./my_output

# Show help
./level_tool --help
```

## Default Paths

| Setting | Default | Description |
|---------|---------|-------------|
| Input | `../../../../tiled/Paradroid.maps` | Source map file (relative to binary) |
| Output | `./output/ships/ship1/levels` | Output directory for TMX files |
| Tileset | `default.tsx` | Tileset reference in TMX files |

## Output

The tool generates:
- One TMX file per level (16 total): `level_0_maintenance.tmx`, `level_1_engineering.tmx`, etc.
- Copies `default.tsx` tileset definition to output
- Copies `map_blocks.png` tileset image to output

Files can be opened directly in Tiled without external dependencies.

## Source Format (Paradroid.maps)

```
Area name="U.S.S. Paradroid"

Levelnumber: 0
xlen of this level: 38
ylen of this level: 16
color of this level: 0
Name of this level=maintenance
Comment of the Influencer on entering this level="..."
Name of background song for this level=BYCOLOR
begin_map
33 33 33 33 33 33 33 33  7 10 10 10 ...
 7 10 10 10 10 10 10 10  6  0  0  0 ...
...
begin_waypoints
Nr.=  0 x=  11 y=   1	 connections:  5  1  2  3
...
end_level
```

## Target Format (TMX)

```xml
<?xml version="1.0" encoding="UTF-8"?>
<map version="1.10" tiledversion="1.11.2" orientation="orthogonal"
     renderorder="right-down" width="38" height="16"
     tilewidth="64" tileheight="64" infinite="0"
     nextlayerid="3" nextobjectid="19">
 <tileset firstgid="1" source="default.tsx"/>
 <layer id="1" name="Tile Layer 1" width="38" height="16">
  <data encoding="csv">
34,34,34,34,...
  </data>
 </layer>
 <objectgroup id="2" name="waypoints">
  <object id="1" x="736" y="96">
   <properties>
    <property name="link-0" type="object" value="6"/>
    <property name="link-1" type="object" value="2"/>
   </properties>
   <point/>
  </object>
  ...
 </objectgroup>
</map>
```

**Notes:**
- TMX tile IDs = Paradroid index + 1 (due to TMX firstgid=1 convention)
- Waypoint object IDs = waypoint number + 1 (1-based)
- Waypoint positions are pixel coordinates at tile center

## Level Data

| Level | Name | Size | Waypoints | Links |
|-------|------|------|-----------|-------|
| 0 | maintenance | 38x16 | 18 | 48 |
| 1 | engineering | 48x16 | 14 | 27 |
| 2 | robostores | 18x11 | 8 | 14 |
| 3 | quarterd | 31x13 | 16 | 36 |
| 4 | repairs | 32x13 | 14 | 29 |
| 5 | staterooms | 38x15 | 24 | 53 |
| 6 | stores | 38x15 | 20 | 42 |
| 7 | research | 38x15 | 19 | 43 |
| 8 | bridge | 38x12 | 13 | 30 |
| 9 | observation | 20x8 | 5 | 10 |
| 10 | airlock | 22x5 | 5 | 12 |
| 11 | reactor | 34x15 | 18 | 44 |
| 12 | upper cargo | 34x16 | 20 | 56 |
| 13 | mid carga | 34x16 | 17 | 56 |
| 14 | vehicle hold | 26x16 | 14 | 36 |
| 15 | shuttle bay | 10x15 | 8 | 17 |

**Totals:** 233 waypoints, 553 links

## Design Decisions

### Single Tool with Multiple Modes
Follows the project pattern (like scene_tool). Currently implements `--convert` mode; `--view` mode planned for future.

### TMX Format Choice
- **CSV encoding** for human-readable tile data
- **External tileset reference** (`default.tsx`) for reusability
- **64x64 tile size** matching the existing tileset

### Waypoint Export
Waypoints are exported as a TMX object layer named "waypoints":
- Each waypoint is a point object at tile-centered pixel coordinates
- Object IDs are 1-based (source waypoint number + 1)
- Connections are stored as `link-n` properties with type="object" referencing target waypoint IDs
- This format is compatible with Tiled's object reference system

### Asset Copying
The tool copies `default.tsx` and `map_blocks.png` to the output directory so TMX files work standalone without path fixups.

## Unit Tests

Tests are in `cpp-version/tests/paradroid_parser_test.cpp`:
- `ParsesCorrectNumberOfLevels` - Verifies 16 levels parsed
- `AllLevelDimensions` - Verifies xlen, ylen, name for each level
- `TileDataMatchesDimensions` - Verifies tile grid matches declared dimensions
- `AreaName` - Verifies area name is "U.S.S. Paradroid"
- `WaypointCountsPerLevel` - Verifies waypoint count per level
- `LinkCountsPerLevel` - Verifies link count per level
- `TotalWaypointsAndLinks` - Verifies totals (233 waypoints, 553 links)

Run tests:
```bash
cd cpp-version/build/tests
./run_tests --gtest_filter="ParadroidParser*"
```

## Files

```
level_tool/
├── CMakeLists.txt        # Build configuration
├── README.md             # This file
├── main.cpp              # CLI and conversion orchestration
├── paradroid_parser.h    # Data structures and parser interface
├── paradroid_parser.cpp  # Paradroid.maps parser
├── tmx_writer.h          # TMX generation interface
└── tmx_writer.cpp        # TMX file writer (uses tinyxml2)
```

## Future Work

- `--view` mode for visual preview using shared rendering code
- Export waypoints as TMX object layer
- Support for other Freedroid map files
