# Original File Formats

Documentation of the legacy Uberdroid level data formats.

## File Hierarchy

```
uber/uberdroid/
├── data/
│   ├── ship0.txt, ship1.txt      # Ship definitions (entry points)
│   ├── tiles.txt                  # Archetile definitions (24 types)
│   ├── materials.xml              # Material profiles
│   └── renderobjects.txt          # Asset index (0-158)
└── ship1/                         # Per-ship data
    ├── xmapfile0.txt ... xmapfile15.txt  # Domain/level definitions
    ├── transport.txt              # Transporter/lift connections
    ├── lifts.txt                  # Deck layout for UI
    ├── levels.txt                 # Domain names (optional)
    └── lvl{n}section{m}.xml       # Procedural geometry paths
```

---

## Ship File (ship{n}.txt)

Entry point defining a ship with multiple domains.

### Format
```
Name <ship name>
Crew <number>
Capacity <number>
Desc <index> <description text>
Domain <relative path to xmapfile>
Domain <relative path to xmapfile>
...
Transporters <relative path to transport.txt>
Decks <relative path to lifts.txt>
End
```

### Example (ship1.txt)
```
Name USF Metahawk
Crew 32
Capacity 2000000
Desc 0 Cargo: Battle droids
Domain ship1\xmapfile0.txt
Domain ship1\xmapfile1.txt
...
Domain ship1\xmapfile15.txt
Transporters ship1\transport.txt
Decks ship1\lifts.txt
End
```

### Fields
| Field | Type | Description |
|-------|------|-------------|
| Name | string | Ship display name |
| Crew | int | Crew count |
| Capacity | int | Cargo capacity |
| Desc | int string | Description lines (index + text) |
| Domain | path | Relative path to domain file (Windows-style backslashes) |
| Transporters | path | Relative path to transport.txt |
| Decks | path | Relative path to lifts.txt |

### Source Reference
`uber/source/uberdroid/paraship.cpp` - `paraship_t::load()`

---

## Transport File (transport.txt)

Defines transporters (lifts/elevators) connecting domains vertically.

### Format
```
Label <id> Deck <domain_index> PosX <grid_x> PosY <grid_y> LevelUp <up_id> LevelDown <down_id> LiftRow <row>
```

### Example
```
Label 00 Deck  0 PosX 17 PosY  9 LevelUp  1 LevelDown -1 LiftRow 0
Label 01 Deck  1 PosX 17 PosY  9 LevelUp  2 LevelDown  0 LiftRow 0
Label 02 Deck 11 PosX  9 PosY  8 LevelUp  3 LevelDown  1 LiftRow 0
```

### Fields
| Field | Type | Description |
|-------|------|-------------|
| Label | int | Transporter ID (unique) |
| Deck | int | Domain index this transporter is in |
| PosX | int | Grid X position (multiply by 64 + 32 for world coords) |
| PosY | int | Grid Y position (multiply by 64 + 32 for world coords) |
| LevelUp | int | ID of transporter above (-1 if none) |
| LevelDown | int | ID of transporter below (-1 if none) |
| LiftRow | int | Elevator shaft grouping index |

### Coordinate Conversion
World position = (PosX * 64 + 32, PosY * 64 + 32, 20)

### Source Reference
`uber/source/uberdroid/paraship.cpp` - transporter loading

---

## Lifts/Deck Layout File (lifts.txt)

Defines visual layout for deck plan UI showing elevator shafts and domain rectangles.

### Format
```
Elevator <id> ElRowX <x> ElRowY <y> ElRowW <width> ElRowH <height>
...
Domain <domain_index> RectNumber <rect_id> DeckX <x> DeckY <y> DeckW <width> DeckH <height>
```

### Example
```
Elevator 0 ElRowX 68  ElRowY  18 ElRowW 16 ElRowH 160
Elevator 1 ElRowX 132 ElRowY  66 ElRowW 16 ElRowH 208

Domain 0 RectNumber 0 DeckX=0 DeckY 162 DeckW 68 DeckH 16
Domain 0 RectNumber 1 DeckX  84 DeckY 162 DeckW  48 DeckH 16
Domain 1 RectNumber 0 DeckX   0 DeckY 146 DeckW  68 DeckH 16
```

### Fields

**Elevator:**
| Field | Type | Description |
|-------|------|-------------|
| id | int | Elevator shaft ID |
| ElRowX/Y | int | Top-left position in deck plan |
| ElRowW/H | int | Size of elevator shaft rectangle |

**Domain:**
| Field | Type | Description |
|-------|------|-------------|
| domain_index | int | Which domain this rectangle represents |
| RectNumber | int | Multiple rectangles per domain (for complex shapes) |
| DeckX/Y | int | Position in deck plan |
| DeckW/H | int | Size of rectangle |

---

## Domain/Level File (xmapfile{n}.txt)

Main level definition containing areas, geometry, waypoints, and objects.

### Overall Structure
```
Level <number>
Area <bounds>
  Tile <geometry>
  ...
  Archetile <index> <x> <y>
  ...
  Feature <data>
  ...
  Geometry <xml_path>
EndArea
...
Waypoint <data>
...
<Object definitions>
...
EndDomain
NAME <level name>
AMBIENCE <sound_id>
PROFILE <9 integers>
PLACEDROID CLASS <class> INDEX <waypoint> ANGLE <radians>
...
```

### Level Header
```
Level <number>
```
Domain/level index (0-15 typically).

### Area Block
```
Area <x1> <y1> <z1> <x2> <y2> <z2> <x3> <y3> <z3> <x4> <y4> <z4>
```
Four 3D points defining area bounds (typically axis-aligned bounding box corners).

### Tile Definition
```
Tile
<vertex_count>
<x1> <y1> <z1>
<x2> <y2> <z2>
...
<u1_1> <v1_1>
<u1_2> <v1_2>
...
<u2_1> <v2_1>
<u2_2> <v2_2>
...
<texture_index_1> <texture_index_2> <tile_type>
[DiffuseColour <r> <g> <b>]
[SpecularColour <r> <g> <b>]
[EffectTexture <index>]
[AdditiveBlend]
[AlphaBlend]
```

| Field | Type | Description |
|-------|------|-------------|
| vertex_count | int | Number of vertices (typically 4) |
| x, y, z | float | Vertex positions |
| u1, v1 | float | First UV coordinate set (per vertex) |
| u2, v2 | float | Second UV coordinate set (per vertex) |
| texture_index_1 | int | Primary texture atlas index |
| texture_index_2 | int | Secondary texture index (bump/normal) |
| tile_type | int | Tile type flag |
| DiffuseColour | float[3] | Optional diffuse color multiplier |
| SpecularColour | float[3] | Optional specular color |
| EffectTexture | int | Optional effect texture index |
| AdditiveBlend | flag | Use additive blending |
| AlphaBlend | flag | Use alpha blending |

### Archetile Definition
```
Archetile <archetype_index> <x_offset> <y_offset>
```
References a predefined tile from tiles.txt, placed at given offset.

| Field | Type | Description |
|-------|------|-------------|
| archetype_index | int | Index into tiles.txt (0-23) |
| x_offset | float | X position offset |
| y_offset | float | Y position offset |

### Feature Definition
```
Feature
<x> <y> <z>
<rx> <ry> <rz>
<indestructible> <solid> <fullbright>
<render_index>
```

| Field | Type | Description |
|-------|------|-------------|
| x, y, z | float | World position |
| rx, ry, rz | float | Rotation (radians) |
| indestructible | int | Cannot be destroyed (0/1) |
| solid | int | Blocks movement (0/1) |
| fullbright | int | Ignores lighting (0/1) |
| render_index | int | Asset index from renderobjects.txt |

### Geometry Reference
```
Geometry <xml_path>
```
Path to XML file containing procedural path geometry (see Geometry XML section).

### Waypoint Definition
```
Waypoint <id> <x> <y> <z> <start> <console> <recharge> <lift> <transmat> <n1> <n2> <n3> <n4> <n5> <n6>
```

| Field | Type | Description |
|-------|------|-------------|
| id | int | Unique waypoint ID |
| x, y, z | float | World position |
| start | int | Valid droid spawn point (0/1) |
| console | int | Near a console (0/1) |
| recharge | int | Near a charger (0/1) |
| lift | int | Near a lift/transporter (0/1) |
| transmat | int | Transmat beam destination (0/1) |
| n1-n6 | int | Connected waypoint IDs (0 = none) |

### Object Definitions

Objects are created via factory dispatch based on keyword. Common types:

**Door:**
```
Door <id> <x> <y> <z> <rx> <ry> <rz> <width> <height> <state> <waypoint1> <waypoint2>
[MASS <value>]
[ALWAYSRENDER]
```

**Console:**
```
Console <id> <x> <y> <z> <rx> <ry> <rz> <waypoint> <target_obj_id>
```

**Charger:**
```
Charger <id> <x> <y> <z> <rx> <ry> <rz>
```

**Destructible:**
```
Destructible <id> <x> <y> <z> <rx> <ry> <rz> <model_index> <hit_points>
[FIXED]
[SPIN <axis> <speed>]
```

**Organic:**
```
Organic <id> <x> <y> <z> <model_index>
```

### Domain Footer
```
EndDomain
NAME <level name>
AMBIENCE <sound_index>
PROFILE <p0> <p1> <p2> <p3> <p4> <p5> <p6> <p7> <p8>
PLACEDROID CLASS <class_id> INDEX <waypoint_id> ANGLE <radians>
```

| Field | Type | Description |
|-------|------|-------------|
| NAME | string | Human-readable level name |
| AMBIENCE | int | Ambient sound index (-1 for none) |
| PROFILE | int[9] | Droid spawn profile (count per class tier) |
| PLACEDROID | - | Explicit droid placement |

### Source References
- `uber/source/uberdroid/domain.cpp` - `domain::load()`
- `uber/source/uberdroid/area.cpp` - `area::load()`, `tile::load()`
- `uber/source/uberdroid/paradomain.cpp` - Footer parsing
- `uber/source/uberdroid/waypoint.cpp` - `waypoint::load()`
- `uber/source/uberdroid/object.cpp` - Object keyword parsing

---

## Archetile Definitions (tiles.txt)

Predefined tile templates for common floor patterns.

### Format
```
number <count>
<Name> <Index>
<vertex_count>
<x1> <y1> <z1>
<x2> <y2> <z2>
<x3> <y3> <z3>
<x4> <y4> <z4>
<u1_1> <v1_1>
<u1_2> <v1_2>
<u1_3> <v1_3>
<u1_4> <v1_4>
<u2_1> <v2_1>
<u2_2> <v2_2>
<u2_3> <v2_3>
<u2_4> <v2_4>
<texture1> <texture2> <tile_type>
[DiffuseColour <r> <g> <b>]
[SpecularColour <r> <g> <b>]
[EffectTexture <index>]
...
```

### Example
```
number 24
Plain 0
4
0.000000 64.000000 1.0
0.000000 0.000000 1.0
64.000000 64.000000 1.0
64.000000 0.000000 1.0
0.0 1.0
0.0 0.0
1.0 1.0
1.0 0.0
0.0 1.0
0.0 0.0
1.0 1.0
1.0 0.0
79 1 0
DiffuseColour 0.8 0.8 1.0
```

### Known Archetypes (24 total)
| Index | Name | Description |
|-------|------|-------------|
| 0 | Plain | Standard floor tile |
| 1 | Shuttle | Shuttle bay floor |
| 2 | Lift | Elevator floor |
| 3 | Alert | Alert/warning floor |
| 4-23 | Various | ConsoleE, ConsoleS, etc. |

### Characteristics
- All archetiles are 64x64 unit flat squares
- Z typically 1.0 (floor level)
- Full 0-1 UV mapping
- 4 vertices in triangle strip order

---

## Geometry XML (lvl{n}section{m}.xml)

Procedural path-based geometry for walls and corridors.

### Structure
```xml
<Path>
  <Nodes>
    <Node id="0" x="544" y="976" z="1" />
    <Node id="1" x="608" y="976" z="1" />
    ...
  </Nodes>
  <Links>
    <Link id="0" start="24" finish="26">
      <Control x="..." y="..." z="..." />
      <Profile id="0" />
      <Profile id="1" />
    </Link>
    ...
  </Links>
  <Profiles>
    <Profile id="0">
      <!-- Profile definition -->
    </Profile>
    ...
  </Profiles>
  <Areas>
    <Area id="0" materialID="0">
      <Link id="0" />
      <Link id="49" />
      <Link id="17" />
      <Link id="18" />
    </Area>
    ...
  </Areas>
</Path>
```

### Elements

**Node:** Point in 3D space
| Attribute | Type | Description |
|-----------|------|-------------|
| id | int | Unique node ID |
| x, y, z | float | World position |

**Link:** Connection between nodes forming a path segment
| Attribute | Type | Description |
|-----------|------|-------------|
| id | int | Unique link ID |
| start | int | Starting node ID |
| finish | int | Ending node ID |

**Link Children:**
- `<Control>` - Bezier control point for curved segments
- `<Profile>` - Cross-section profile(s) to extrude along link

**Profile:** Cross-section shape definition for extrusion

**Area:** Closed region bounded by links
| Attribute | Type | Description |
|-----------|------|-------------|
| id | int | Unique area ID |
| materialID | int | Material/texture index |

Contains `<Link>` children specifying boundary links.

---

## Levels File (levels.txt)

Optional file mapping domain indices to human-readable names.

### Format
```
level <index> <name>
```

### Example (ship0/levels.txt)
```
level 0 maintenance
level 1 engineering
level 2 robostores
level 3 quarters
level 4 repairs
level 5 staterooms
level 6 stores
level 7 research
level 8 bridge
level 9 observation
level 10 airlock
level 11 reactor
level 12 upper cargo
level 13 mid cargo
level 14 vehicle hold
level 15 shuttle bay
```

---

## Asset Index (renderobjects.txt)

Maps render indices to model file paths. Used by Features and some objects.

### Format
```
<index> <model_path>
```

Index 0-158 typically, referencing .X or .mdl model files in original format.

---

## Coordinate System

- **X**: Horizontal (East-West)
- **Y**: Horizontal (North-South)
- **Z**: Vertical (Up-Down), positive is up
- **Units**: Game units (64 units = 1 grid cell for transporters)
- **Rotations**: Radians, typically around Z axis for facing direction
