# JSON Output Formats

Specification for the converted JSON scene data.

## Design Principles

1. **Explicit hierarchy**: Ship → Domain → Area structure is clear
2. **Named references**: Asset paths instead of numeric indices
3. **Inlined data**: Geometry XML content embedded directly
4. **Collision shapes included**: Static physics geometry stored for Box2D
5. **Omit defaults**: Schema defines defaults, JSON only includes non-default values
6. **Metadata**: Version, source file, conversion date for traceability

---

## Ship JSON (ship.json)

Top-level file representing a complete ship with all domains.

```json
{
  "version": "1.0",
  "name": "USF Metahawk",
  "crew": 32,
  "capacity": 2000000,
  "description": ["Cargo: Battle droids"],
  "domains": [
    "domains/domain_0.json",
    "domains/domain_1.json"
  ],
  "transporters": [
    {
      "id": 0,
      "domainIndex": 0,
      "position": [1120, 608, 20],
      "levelUp": 1,
      "levelDown": -1,
      "liftRow": 0
    }
  ],
  "decks": {
    "elevators": [
      {
        "id": 0,
        "rect": { "x": 68, "y": 18, "w": 16, "h": 160 }
      }
    ],
    "domainRects": [
      {
        "domainIndex": 0,
        "rectNumber": 0,
        "rect": { "x": 0, "y": 162, "w": 68, "h": 16 }
      }
    ]
  },
  "metadata": {
    "sourceFile": "ship1.txt",
    "conversionDate": "2025-01-15T10:30:00Z",
    "toolVersion": "1.0.0"
  }
}
```

### Field Reference

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| version | string | Yes | Schema version |
| name | string | Yes | Ship display name |
| crew | int | No | Crew count (default: 0) |
| capacity | int | No | Cargo capacity (default: 0) |
| description | string[] | No | Description lines |
| domains | string[] | Yes | Relative paths to domain JSON files |
| transporters | Transporter[] | No | Lift/elevator connections |
| decks | Decks | No | Deck plan UI layout |
| metadata | Metadata | Yes | Conversion metadata |

### Transporter Object

| Field | Type | Description |
|-------|------|-------------|
| id | int | Unique transporter ID |
| domainIndex | int | Which domain this transporter is in |
| position | float[3] | World position [x, y, z] |
| levelUp | int | ID of transporter above (-1 if none) |
| levelDown | int | ID of transporter below (-1 if none) |
| liftRow | int | Elevator shaft grouping |

**Position Conversion**: Original (PosX, PosY) → (PosX * 64 + 32, PosY * 64 + 32, 20)

### Decks Object

| Field | Type | Description |
|-------|------|-------------|
| elevators | Elevator[] | Elevator shaft rectangles |
| domainRects | DomainRect[] | Domain visualization rectangles |

### Elevator Object

| Field | Type | Description |
|-------|------|-------------|
| id | int | Elevator shaft ID |
| rect | Rect | Position and size {x, y, w, h} |

### DomainRect Object

| Field | Type | Description |
|-------|------|-------------|
| domainIndex | int | Which domain this represents |
| rectNumber | int | Sub-rectangle index (domains can have multiple) |
| rect | Rect | Position and size {x, y, w, h} |

---

## Domain JSON (domain_{n}.json)

Individual level/domain definition.

```json
{
  "version": "1.0",
  "levelNumber": 0,
  "name": "Maintenance",
  "ambience": 28,
  "profile": [2, 0, 1, 3, 1, 0, 0, 0, 0],
  "bounds": {
    "min": [0, 0, -23],
    "max": [2400, 1040, 25.1]
  },
  "areas": [
    {
      "bounds": {
        "min": [0, 0, -23],
        "max": [800, 400, 25]
      },
      "tiles": [],
      "features": [],
      "geometry": null,
      "collision": null
    }
  ],
  "waypoints": [],
  "objects": {
    "doors": [],
    "consoles": [],
    "chargers": [],
    "destructibles": []
  },
  "spawns": [],
  "metadata": {
    "sourceFile": "xmapfile0.txt",
    "conversionDate": "2025-01-15T10:30:00Z"
  }
}
```

### Field Reference

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| version | string | Yes | Schema version |
| levelNumber | int | Yes | Domain index (0-15) |
| name | string | No | Human-readable name |
| ambience | int | No | Ambient sound ID (-1 for none) |
| profile | int[9] | No | Droid spawn profile per class tier |
| bounds | Bounds | Yes | Overall domain bounds |
| areas | Area[] | Yes | Area definitions |
| waypoints | Waypoint[] | No | Navigation graph nodes |
| objects | Objects | No | Placed game objects |
| spawns | Spawn[] | No | Explicit droid placements |
| metadata | Metadata | Yes | Conversion metadata |

---

## Area Object

```json
{
  "bounds": {
    "min": [0, 0, -23],
    "max": [800, 400, 25]
  },
  "tiles": [],
  "features": [],
  "geometry": null,
  "collision": {
    "polygons": [],
    "chains": []
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| bounds | Bounds | Area bounding box |
| tiles | Tile[] | Floor/surface geometry |
| features | Feature[] | Placed static objects |
| geometry | PathGeometry | Inlined procedural geometry (null if none) |
| collision | CollisionData | Static physics shapes for Box2D |

---

## CollisionData Object

Static physics collision shapes derived from geometry XML.

```json
{
  "polygons": [
    {
      "vertices": [[100, 200], [150, 200], [150, 250], [100, 250]]
    }
  ],
  "chains": [
    {
      "vertices": [[0, 0], [100, 0], [100, 100]],
      "loop": false
    }
  ]
}
```

### CollisionPolygon Object

Convex polygon for solid areas. Box2D requires convex polygons with max 8 vertices.

| Field | Type | Description |
|-------|------|-------------|
| vertices | float[2][] | Polygon vertices in CCW order (2D: x, y) |

### CollisionChain Object

Chain shape for wall segments. Used for non-convex boundaries.

| Field | Type | Description |
|-------|------|-------------|
| vertices | float[2][] | Chain vertices in order (2D: x, y) |
| loop | bool | True if chain forms a closed loop |

**Generation from Geometry XML:**
1. Path areas → convex polygons (decomposed if concave)
2. Link segments → edge chains for walls
3. All coordinates in 2D (X, Y) - Z is fixed height for 3D rendering

---

## Tile Object

Represents floor/surface geometry.

```json
{
  "vertices": [
    { "position": [0, 64, 1], "uv1": [0, 1], "uv2": [0, 1] },
    { "position": [0, 0, 1], "uv1": [0, 0], "uv2": [0, 0] },
    { "position": [64, 64, 1], "uv1": [1, 1], "uv2": [1, 1] },
    { "position": [64, 0, 1], "uv1": [1, 0], "uv2": [1, 0] }
  ],
  "textures": {
    "diffuse": "textures/floor_plate.png",
    "bump": "textures/norm_tile0.png"
  },
  "properties": {
    "diffuseColour": [0.8, 0.8, 1.0],
    "tileType": 1
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| vertices | Vertex[] | Vertex data (position + UVs) |
| textures | Textures | Named texture paths |
| properties | TileProperties | Optional rendering properties |

### Vertex Object

| Field | Type | Description |
|-------|------|-------------|
| position | float[3] | World position [x, y, z] |
| uv1 | float[2] | Primary UV coordinates |
| uv2 | float[2] | Secondary UV coordinates |

### Textures Object

| Field | Type | Description |
|-------|------|-------------|
| diffuse | string | Diffuse/albedo texture path |
| bump | string | Normal/bump map path (optional) |
| effect | string | Effect texture path (optional) |

### TileProperties Object

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| diffuseColour | float[3] | [1,1,1] | Color multiplier |
| specularColour | float[3] | [0,0,0] | Specular color |
| tileType | int | 0 | Tile type flag |
| additiveBlend | bool | false | Additive blending |
| alphaBlend | bool | false | Alpha blending |

---

## Feature Object

Static placed object (prop, obstacle, etc.).

```json
{
  "position": [288, 864, 1],
  "rotation": [0, 0, 0],
  "flags": {
    "indestructible": true,
    "solid": true,
    "fullbright": false
  },
  "model": "models/block.gltf",
  "collision": {
    "type": "box",
    "halfExtents": [1.0, 1.0]
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| position | float[3] | World position |
| rotation | float[3] | Rotation in radians [rx, ry, rz] |
| flags | FeatureFlags | Behavior flags |
| model | string | Path to GLTF model |
| collision | FeatureCollision | Physics shape (generated from GLTF bounds) |

### FeatureFlags Object

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| indestructible | bool | false | Cannot be destroyed |
| solid | bool | true | Blocks movement (has collision) |
| fullbright | bool | false | Ignores lighting |

### FeatureCollision Object

Physics collision shape derived from GLTF model bounds.

```json
{
  "type": "box",
  "halfExtents": [1.0, 0.5]
}
```

or

```json
{
  "type": "circle",
  "radius": 0.75
}
```

| Field | Type | Description |
|-------|------|-------------|
| type | string | "box" or "circle" |
| halfExtents | float[2] | Box half-width and half-height (for type="box") |
| radius | float | Circle radius (for type="circle") |

**Note**: Collision is only present if `flags.solid` is true.

---

## PathGeometry Object

Inlined procedural geometry (converted from XML). Used for 3D rendering of walls/corridors.

```json
{
  "nodes": [
    { "id": 0, "position": [544, 976, 1] },
    { "id": 1, "position": [608, 976, 1] }
  ],
  "links": [
    {
      "id": 0,
      "start": 24,
      "finish": 26,
      "control": { "position": [580, 960, 1] },
      "profiles": [0, 1]
    }
  ],
  "profiles": [
    { "id": 0, "points": [] }
  ],
  "areas": [
    {
      "id": 0,
      "materialId": 0,
      "links": [0, 49, 17, 18]
    }
  ]
}
```

### PathNode Object

| Field | Type | Description |
|-------|------|-------------|
| id | int | Unique node ID |
| position | float[3] | World position |

### PathLink Object

| Field | Type | Description |
|-------|------|-------------|
| id | int | Unique link ID |
| start | int | Starting node ID |
| finish | int | Ending node ID |
| control | ControlPoint | Bezier control point (optional) |
| profiles | int[] | Profile IDs for extrusion |

### PathProfile Object

| Field | Type | Description |
|-------|------|-------------|
| id | int | Unique profile ID |
| points | float[2][] | 2D cross-section points |

### PathArea Object

| Field | Type | Description |
|-------|------|-------------|
| id | int | Unique area ID |
| materialId | int | Material/texture index |
| links | int[] | Boundary link IDs (ordered) |

---

## Waypoint Object

Navigation graph node.

```json
{
  "id": 18,
  "position": [2336, 288, 0],
  "flags": {
    "start": true,
    "console": false,
    "recharge": false,
    "lift": false,
    "transmat": false
  },
  "neighbors": [17, 0, 0, 0, 0, 0]
}
```

| Field | Type | Description |
|-------|------|-------------|
| id | int | Unique waypoint ID |
| position | float[3] | World position |
| flags | WaypointFlags | Behavior flags |
| neighbors | int[6] | Connected waypoint IDs (0 = none) |

### WaypointFlags Object

| Field | Type | Description |
|-------|------|-------------|
| start | bool | Valid droid spawn point |
| console | bool | Near a console |
| recharge | bool | Near a charger |
| lift | bool | Near a lift/transporter |
| transmat | bool | Transmat beam destination |

---

## Objects Container

```json
{
  "doors": [],
  "consoles": [],
  "chargers": [],
  "destructibles": []
}
```

### Door Object

```json
{
  "id": 27,
  "position": [448, 960, 0],
  "rotation": [0, 0, 1.57],
  "size": [10, 25],
  "state": 1,
  "waypoints": [5, 6],
  "properties": {
    "mass": 1.0,
    "alwaysRender": false
  },
  "collision": {
    "type": "box",
    "halfExtents": [5, 12.5]
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| id | int | Unique object ID |
| position | float[3] | World position |
| rotation | float[3] | Rotation in radians |
| size | float[2] | Door dimensions [width, height] |
| state | int | Initial state (0=closed, 1=open) |
| waypoints | int[2] | Connected waypoint IDs |
| properties | DoorProperties | Additional properties |
| collision | FeatureCollision | Physics shape (derived from size) |

### Console Object

```json
{
  "id": 15,
  "position": [320, 800, 32],
  "rotation": [0, 0, 0],
  "waypointId": 12,
  "targetObjectId": 27
}
```

| Field | Type | Description |
|-------|------|-------------|
| id | int | Unique object ID |
| position | float[3] | World position |
| rotation | float[3] | Rotation in radians |
| waypointId | int | Associated waypoint |
| targetObjectId | int | Object this console controls |

### Charger Object

```json
{
  "id": 8,
  "position": [640, 320, 0],
  "rotation": [0, 0, 0]
}
```

| Field | Type | Description |
|-------|------|-------------|
| id | int | Unique object ID |
| position | float[3] | World position |
| rotation | float[3] | Rotation in radians |

### Destructible Object

```json
{
  "id": 42,
  "position": [192, 576, 0],
  "rotation": [0, 0, 0],
  "model": "models/crate.gltf",
  "hitPoints": 50,
  "properties": {
    "fixed": false,
    "spin": { "axis": "z", "speed": 0.5 }
  },
  "collision": {
    "type": "box",
    "halfExtents": [0.5, 0.5]
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| id | int | Unique object ID |
| position | float[3] | World position |
| rotation | float[3] | Rotation in radians |
| model | string | GLTF model path |
| hitPoints | int | Health before destruction |
| properties | DestructibleProperties | Additional properties |
| collision | FeatureCollision | Physics shape |

---

## Spawn Object

Explicit droid placement.

```json
{
  "droidClass": 5,
  "waypointIndex": 3,
  "angle": 1.57
}
```

| Field | Type | Description |
|-------|------|-------------|
| droidClass | int | Droid class ID |
| waypointIndex | int | Spawn waypoint ID |
| angle | float | Initial facing angle (radians) |

---

## Physics Coordinate System

The collision data uses 2D coordinates (X, Y) matching the game's top-down physics:

- **X**: Horizontal (matches 3D X)
- **Y**: Vertical in 2D view (matches 3D Z in world space)
- **Units**: Same as world units (64 units = 1 grid cell)

**Mapping to 3D Rendering:**
- Physics X → 3D X
- Physics Y → 3D Z (ground plane)
- 3D Y is the vertical (height) axis

---

## Transformation Notes

### From Original to JSON

| Aspect | Original | JSON | Notes |
|--------|----------|------|-------|
| Structure | Flat keywords | Hierarchical | Ship → Domain → Area explicit |
| Asset refs | Numeric index | Named path | "models/crate.gltf" vs "42" |
| Textures | Atlas index | Named file | "textures/floor.png" |
| Geometry | External XML | Inlined | Embedded in domain JSON |
| Collision | Not stored | Included | Static shapes for Box2D |
| Defaults | Always written | Omitted | Schema defines defaults |
| Coordinates | Mixed units | Consistent | All world coordinates |

### Archetile Conversion

Original archetiles are converted to regular tiles:
1. Load archetype definition from tiles.txt
2. Copy vertex positions with (x, y) offset applied
3. Preserve UV coordinates, textures, and properties
4. **Optimization**: Adjacent same-texture archetiles merged into larger triangle fans

### Collision Generation

1. **Geometry XML areas** → Convex polygons (concave shapes decomposed)
2. **Geometry XML links** → Edge chains for wall segments
3. **Features (solid=true)** → Box or circle from GLTF bounding box
4. **Doors** → Box from door size
5. **Destructibles** → Box or circle from GLTF bounds

### Texture Path Resolution

| Original Index | JSON Path |
|----------------|-----------|
| 79 | "textures/floor_plate.png" |
| 1 | "textures/norm_tile0.png" |
| (effect) 5 | "textures/glow_blue.png" |

Resolution via renderobjects.txt or texture atlas metadata.

---

## Limitations

1. **Concave collision decomposition**: Complex shapes split into multiple convex polygons
2. **Box2D vertex limits**: Polygons limited to 8 vertices (larger shapes decomposed)
3. **Animation not stored**: Door/console animations handled at runtime
4. **Sound indices preserved**: No conversion to named audio files
5. **Material effects simplified**: Additive/alpha blend flags preserved, complex effects need runtime handling
6. **Feature collision approximated**: GLTF bounding box, not actual mesh
