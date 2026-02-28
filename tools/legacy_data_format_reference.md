# Legacy Data Format Reference

This document describes the old data structures used for unit definitions in the Uberdroid project. Use this as a reference when converting legacy data to modern formats.

> **See also:** [cpp-version/tools/droid_tool/UBERDROID_DATA_FORMATS.md](cpp-version/tools/droid_tool/UBERDROID_DATA_FORMATS.md) for detailed format documentation including C++ data structures and conversion notes.

## Source Code Reference

- Header: `uber/source/uberdroid/droid_class.h`
- Implementation: `uber/source/uberdroid/droid_class.cpp`

## Data Files

All data files are located in `uber/uberdroid/data/`:

- `droidclasses.txt` - Droid class definitions
- `renderobjects.txt` - Render object definitions
- `textures.txt` - Texture index table

---

## Droid Classes (`droidclasses.txt`)

### Basic Structure

Each droid class starts with a header line followed by property lines:

```
Class <index>
<render_index> <number> <type> <energy> <armour>
<weapon> <pulses>
<speed> <acceleration> <deceleration> <scan_rate>
<visual> <aural> <ultrasonic> <subsonic> <infrared> <ultraviolet> <radar> <disruptor_shielded>
<drain_rate> <collide_radius> <proximity_radius> <aggression>
vrad <visual_radius>
head <head_index>
```

### Field Descriptions

| Field | Type | Description |
|-------|------|-------------|
| `render_index` | int | Index into renderobjects.txt for main body geometry |
| `number` | int | Droid model number (e.g., 101, 476, 999) |
| `type` | char | Droid category (0-9), affects scoring: points = 50/10 * type |
| `energy` | float | Hit points |
| `armour` | float | Base damage resistance |
| `weapon` | int | Weapon type index (-1 = none) |
| `pulses` | int | Number of shots per burst |
| `speed` | float | Maximum movement speed |
| `acceleration` | float | Acceleration rate |
| `deceleration` | float | Deceleration rate |
| `scan_rate` | float | Sensor scan frequency |
| `visual_radius` | float | Visual detection range |
| `head_index` | int | Render object index for head (-1 = no separate head) |

### Boolean Sensor Flags

Read as integers (0 or 1):
- `visual` - Has visual sensors
- `aural` - Has audio sensors
- `ultrasonic` - Has ultrasonic sensors
- `subsonic` - Has subsonic sensors
- `infrared` - Has IR sensors
- `ultraviolet` - Has UV sensors
- `radar` - Has radar
- `disruptor_shielded` - Immune to disruptor weapons

### Optional Keywords

Keywords are read in a loop until `END` is encountered:

| Keyword | Parameters | Description |
|---------|------------|-------------|
| `SENSOR` | type_name | Enable sensor: visual, aural, ultrasonic, subsonic, infrared, ultraviolet, radar |
| `TURRET` | - | Fire from head position as a turret |
| `OMNIDIRECTIONAL` | - | Can face any direction while moving |
| `FIREOFFSET` | x y z | Offset for projectile origin |
| `HEADOFFSET` | x y z | Position offset for head section |
| `ROTATIONRATE` | rate | Body rotation speed (rad/ms) |
| `HEADROTATIONRATE` | rate | Head rotation speed (rad/ms) |
| `SOUND` | sample rate looped volume constant | Normal movement sound |
| `ULTRASONIC` | sample rate looped volume constant | Ultrasonic movement sound |
| `SUBSONIC` | sample rate looped volume constant | Subsonic movement sound |
| `SPECIALSAMPLE` | index | Sound sample for special action |
| `SPECIALSAMPLERANGE` | range | Random range for special sample |
| `SPECIALRATE` | rate | Frequency of special action |
| `FAILSAMPLE` | index | Sound for failure/damage |
| `FAILSAMPLERANGE` | range | Random range for fail sample |
| `TALKTHRESHOLD` | time | Time between random sounds (ms) |
| `HEADHEIGHT` | height | Height for influence device placement |
| `PERFORMCLEAN` | - | AI flag: performs cleaning behavior |
| `DRIPTHRESHOLD` | threshold | Enable fluid drip when damaged below threshold |
| `DROIDTYPE` | type | Library classification (0=device, 1=robot, 2=droid, 3=cyborg) |
| `DRIVETYPE` | type | Drive mechanism type |
| `BRAINTYPE` | type | Brain/AI type |
| `DESCRIPTION` | line_num text | Description line for library display |
| `TARGETRETICULE` | - | Show visual targeting effect |
| `ARMOUR` | type value | Override armour for specific damage type |
| `HEADSECTION` | index | Which section is the head (for multi-section droids) |

### Section Definitions

Droids can have multiple geometry sections. Each `SECTION` block defines:

```
SECTION
Render <render_object_index>
Parent <parent_section_index>    (-1 for root)
Tag <parent_tag_index>           (-1 if not using tags, for MD3 models)
Rotate <rotation_type>           (0=none, 1=movement, 2=facing)
Offset <x> <y> <z>               (local translation)
Rotation <x> <y> <z>             (local rotation in radians)
```

**Rotation Types:**
- `0` (dr_none) - No automatic rotation
- `1` (dr_movement) - Rotates with movement direction
- `2` (dr_facing) - Rotates to face target

If no sections are defined, the system auto-generates sections from `render_index` and `head_index`.

---

## Render Objects (`renderobjects.txt`)

### Header Format

Each render object starts with:
```
<name> <index> <type>
```

### Type Values

| Type | Description |
|------|-------------|
| -1 | Null/placeholder |
| 0 | Sprite |
| 1 | MDL model (simple) |
| 3 | Particle system |
| 4 | Beam |
| 5 | MD2 model |
| 8 | ASC/full model with materials |
| 9 | Sectional (references external file) |

### Type 0: Sprites

```
<name> <index> 0
<texture_index> <size> <billboard_type> <animated> <alpha>
<frame_start> <frame_count>
drad <rotation_rate>
col <r> <g> <b> <a>
dcol <dr> <dg> <db> <da>
[Aspect <ratio>]
ENDSPRITE
DRAWTYPE <type>
END
```

### Type 1: Simple MDL Model

```
<name> <index> 1
<model_path> <flag>
[ANIMMOVING]
END
```

### Type 3: Particle System

```
<name> <index> 3
<particle_system_index>
DRAWTYPE <type>
END
```

### Type 4: Beam

```
<name> <index> 4
TEXTURE <texture_index>
WIDTH <width>
LENGTH <length>
[TEXSCROLLY <scroll_rate>]
END
ADDITIVE
<sprite_params>
END
```

### Type 5: MD2 Model

```
<name> <index> 5
MD2 <model_path>
TEXTURE <texture_index>
END
END
```

### Type 8: Full Model (Most Common)

```
<name> <index> 8
MODEL <model_path> <flag>
TEXTURES <idx0> <idx1> <idx2> <idx3> <idx4>
EFFECTTEXTURES <idx0> <idx1> <idx2> <idx3> <idx4>
[TEXTURE <slot> <relative_path>]
[EFFECTTEXTURE <slot> <relative_path>]
[EFFECTTEXTURE2 <slot> <relative_path>]
[SHADER <slot> <shader_name>]
END
[DRAWTYPE <type>]
[SPECULARITY <r> <g> <b> <a>]
[TEXROTATE <rate>]
[ANIMMOVING]
[glowcolour <r> <g> <b> <a>]
[glowscroll <rate> <x> <y>]
[glowcolourwave <type> <amplitude> <frequency> <phase>]
[glowscrollwave <type> <amplitude> <frequency> <phase>]
[glowmanualcolour]
[glowalertcolour]
END
```

### Type 9: Sectional

```
<name> <index> 9
FILE <external_file_path>
END
```

---

## Texture References

### By Index (TEXTURES / EFFECTTEXTURES)

Arrays of 5 texture indices. `-1` means empty/unused.

```
TEXTURES <idx0> <idx1> <idx2> <idx3> <idx4>
EFFECTTEXTURES <idx0> <idx1> <idx2> <idx3> <idx4>
```

Indices reference entries in `textures.txt`.

### By Path (TEXTURE / EFFECTTEXTURE)

```
TEXTURE <slot> <relative_path>
EFFECTTEXTURE <slot> <relative_path>
EFFECTTEXTURE2 <slot> <relative_path>
```

Paths are relative to the asset directory.

---

## Textures (`textures.txt`)

### Format

```
<index> <relative_path> <flags>
```

### Flag Values

| Flag | Description |
|------|-------------|
| 0 | Standard texture |
| 1 | Clamped/special texture |
| 3 | NULL/placeholder |
| 4 | Alpha texture (uses alpha channel) |

Example entries:
```
0 textures\bump\flat.png 0
4 textures\gui\centercongrey.jpg 1
45 textures\gui\crosshair3.tga 4
62 NULL 3
```

---

## Draw Types

| DrawType | Description |
|----------|-------------|
| DIFFUSE | Standard diffuse lighting, no effect textures |
| ENVMAP | Uses effect texture as environment map (UV from normals) |
| BUMP | Normal mapping with effect textures |
| ADDITIVE | Additive blending |
| ALPHAMASK | Alpha testing |
| ALPHABLEND | Alpha blending |
| CONSOLEGLOW | Special console glow shader |
| DIFFUSEGLOW | Diffuse with glow effect |
| GLOWSCROLL | Scrolling glow effect |
| DIFFUSEALPHA | Diffuse with alpha |
| WIREFRAME | Wireframe rendering |
| SHADOWCAST | Shadow casting only |

### Effect Texture Behavior

For the default draw type (ENVMAP), effect textures are used as additive environment maps:
- Bound to texture unit 1
- UV coordinates calculated from geometry normals in screen space
- Formula: `uv = (0.5, 0.5) + 0.5 * normal.xy`

---

## Model Formats

Supported formats referenced in renderobjects.txt:

| Extension | Description |
|-----------|-------------|
| `.asc` | ASCII model format (most common) |
| `.mdl` | MDL model format |
| `.md2` | Quake 2 MD2 format |
| `.txt` | Text-based model definition |

---

## Section Hierarchy Example

A complex droid with multiple sections:

```
Class 11
...
SECTION
Render 121        # Legs (root section)
Parent -1
Tag -1
Rotate 1
Offset 0.0 0.0 0.0
Rotation 0.0 0.0 0.0
SECTION
Render 139        # Torso (attached to legs)
Parent 0
Tag 0
Rotate 1
Offset 1.0 0.0 17.0
Rotation 0.0 0.0 0.0
SECTION
Render 140        # Right arm (attached to torso)
Parent 1
Tag -1
Rotate 1
Offset 0.0 9.5 -9.0
Rotation 0.0 0.0 0.0
...
HEADSECTION 4     # Section 4 is the head
END
```

This creates a hierarchy:
- Section 0: Legs (root)
  - Section 1: Torso
    - Section 2: Right arm
    - Section 3: Left arm
    - Section 4: Head (marked as HEADSECTION)
