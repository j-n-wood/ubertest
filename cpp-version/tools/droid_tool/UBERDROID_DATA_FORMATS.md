# Uberdroid Source Data Formats

Reference documentation for the legacy data formats used in the uberdroid game data. These formats are shared across multiple parsing tools.

## File Locations

| File | Path | Description |
|------|------|-------------|
| droidclasses.txt | `uber/uberdroid/data/droidclasses.txt` | 24 droid class definitions |
| renderobjects.txt | `uber/uberdroid/data/renderobjects.txt` | 159 render object definitions |
| ASC models | `uber/uberdroid/models/*.asc` | MilkShape 3D ASCII models |

---

## renderobjects.txt Format

Defines render objects (models, sprites, effects) indexed 0-158. Each entry has a type that determines its structure.

### Header Line

```
<name> <index> <type>
```

- **name**: String identifier (e.g., "101model", "BIPED_LEGS_MDL")
- **index**: Integer 0-158, used as lookup key from droidclasses.txt
- **type**: Determines parsing structure (see below)

### Render Object Types

| Type | Name | Description |
|------|------|-------------|
| 0 | Sprite | 2D animated sprite |
| 1 | ModelMDL | Half-Life .mdl model |
| 3 | Particles | Particle system |
| 4 | Beam | Beam/laser effect |
| 5 | ModelMD2 | Quake II .md2 model |
| 8 | ModelASC | MilkShape .asc model |
| 9 | Sectional | External file reference |
| -1 | Invalid | Obsolete/unused entry |

### Type 8 (ASC Model) Format

Most common for droid parts. Contains model path and texture references.

```
<name> <index> 8
MODEL <path> <flags>
TEXTURE <slot> <path>           # Up to 5 slots (0-4)
EFFECTTEXTURE <slot> <path>     # Normal/effect maps
EFFECTTEXTURE2 <slot> <path>    # Secondary effect
SHADER <slot> <shader_name>     # Per-material shader
TEXTURES <t0> <t1> <t2> <t3> <t4>       # Texture index refs
EFFECTTEXTURES <e0> <e1> <e2> <e3> <e4> # Effect index refs
END
DRAWTYPE <type>                 # DIFFUSE, BUMP, ENVMAP, etc.
SPECULARITY <r> <g> <b> <a>     # Specular color
END
```

**Example:**
```
servant_torso 95 8
MODEL models\servant_torso.asc 0
TEXTURES 79 79 -1 -1 -1
EFFECTTEXTURES 130 130 -1 -1 -1
END
DRAWTYPE ENVMAP
END
```

### Type 1 (MDL Model) Format

Half-Life model format. Path on second line.

```
<name> <index> 1
<model_path> <flags>
END
```

**Example:**
```
Chair 14 1
models\chair.mdl 1
END
```

### Type 5 (MD2 Model) Format

Quake II model format.

```
<name> <index> 5
MD2 <model_path>
TEXTURE <texture_index>
END
END
```

**Example:**
```
GREY_MD2 65 5
MD2 models\grey.md2
TEXTURE 3
END
END
```

### Type 0 (Sprite) Format

2D animated sprite with color and animation parameters.

```
<name> <index> 0
<texture_index> <size> <animated> <looping> <billboard>
<anim_speed> <frame_count>
drad <delta_radius>
col <r> <g> <b> <a>
dcol <dr> <dg> <db> <da>
Aspect <ratio>                  # Optional
ENDSPRITE
DRAWTYPE <type>                 # ADDITIVE, ALPHAMASK, ALPHABLEND
END
```

**Example:**
```
Flare 0 0
2 16.0 0 1 0
0.0 0.0
drad 0.0
col 1.0 1.0 1.0 1.0
dcol 0.0 0.0 0.0 0.0
ENDSPRITE
DRAWTYPE ADDITIVE
END
```

### Type 3 (Particles) Format

Particle system reference.

```
<name> <index> 3
<particle_system_index>
DRAWTYPE <type>
END
```

### Type 4 (Beam) Format

Beam/laser effect.

```
<name> <index> 4
TEXTURE <texture_index>
WIDTH <width>
LENGTH <length>
TEXSCROLLY <scroll_speed>       # Optional
END
ADDITIVE
<sprite_params>                 # Sprite for end caps
END
```

### Type 9 (Sectional) Format

References external sectional file.

```
<name> <index> 9
FILE <path>
END
```

### Invalid/Obsolete Entries

```
OBSOLETE <index> -1
```

---

## droidclasses.txt Format

Defines 24 droid classes (Class 0-23). Each class specifies stats, sensors, and hierarchical sections.

### Class Block Structure

```
Class <id>
<render_index> <type_code> <energy_cost> <armour> <weapon>
<weapon_type> <pulses>
<speed1> <speed2> <speed3> <speed4>
<8 sensor flags: visual aural ultrasonic infrared motion radio radar magnetic>
<collide_radius> <proximity_radius> <aggression> <unused>
vrad <visual_radius>
head <head_render_index>        # -1 if no head
[optional keywords]
[SECTION blocks]
END
```

### Numeric Lines (After "Class N")

| Line | Format | Description |
|------|--------|-------------|
| 1 | `ri type energy armour weapon` | Basic stats |
| 2 | `weapon_type pulses` | Weapon config |
| 3 | `s1 s2 s3 s4` | 4 speed values |
| 4 | `v a u i m r ra ma` | 8 sensor flags (0/1) |
| 5 | `coll prox aggr unused` | Collision/behavior |

### Optional Keywords

| Keyword | Format | Description |
|---------|--------|-------------|
| `vrad` | `vrad <float>` | Visual detection radius |
| `head` | `head <int>` | Head render index (-1 = none) |
| `SOUND` | `SOUND <int>` | Sound effect index |
| `TURRET` | `TURRET` | Has turret capability |
| `OMNIDIRECTIONAL` | `OMNIDIRECTIONAL` | Can fire in any direction |
| `TARGETRETICULE` | `TARGETRETICULE` | Shows targeting reticule |
| `ULTRASONIC` | `ULTRASONIC` | Ultrasonic attack |
| `SPECIALSAMPLE` | `SPECIALSAMPLE <int>` | Special sound sample |
| `HEADSECTION` | `HEADSECTION <int>` | Section index for head |
| `HEADOFFSET` | `HEADOFFSET <x> <y> <z>` | Head position offset |
| `FIREOFFSET` | `FIREOFFSET <x> <y> <z>` | Weapon fire point |
| `HEADROTATIONRATE` | `HEADROTATIONRATE <float>` | Head turn speed |
| `ROTATIONRATE` | `ROTATIONRATE <float>` | Body turn speed |
| `DESCRIPTION` | `DESCRIPTION <n> <text>` | Multi-line description |
| `DROIDTYPE` | `DROIDTYPE <0-3>` | Droid classification |
| `DRIVETYPE` | `DRIVETYPE <0-5>` | Movement type |
| `BRAINTYPE` | `BRAINTYPE <0-3>` | AI type |

### SECTION Blocks

Defines hierarchical body parts. Can have 0-N sections per class.

```
SECTION
Render <render_index>           # Index into renderobjects.txt
Parent <section_index>          # -1 = root, else parent section
Tag <tag_id>                    # MD3 tag attachment (-1 = none)
Rotate <mode>                   # 0=none, 1=movement, 2=facing
Offset <x> <y> <z>              # Position relative to parent
Rotation <rx> <ry> <rz>         # Rotation offset (radians)
```

**Rotation Modes:**
- 0 (`dr_none`): No rotation
- 1 (`dr_movement`): Rotates with movement direction
- 2 (`dr_facing`): Rotates to face target

### Section Hierarchy

Sections form a tree via parent indices:
- `Parent -1`: Root section (usually body/torso)
- `Parent 0`: Child of section 0
- `Parent N`: Child of section N

**Example (Class 3 - Service Robot):**
```
Section 0: Render 95 (torso), Parent -1, Root
Section 1: Render 99 (base), Parent 0, Child of torso
Section 2: Render 100 (ring), Parent 0, Child of torso
Section 3: Render 94 (head), Parent 0, Child of torso
Section 4: Render 96 (arm), Parent 0, Child of torso
Section 5: Render 96 (arm), Parent 0, Child of torso (mirrored)
```

### Auto-Generated Sections

If no SECTION blocks present, sections are auto-created:
- Section 0: Uses class `render_index`
- Section 1: If `head > -1`, uses head render index as child

### Complete Class Example

```
Class 3
95 247 1 20.0 0.0
4 5
200.0 300.0 300.0 0.0
1 1 0 0 0 0 0 0
0.0 8.0 25.0 0.0
vrad 300.0
head -1
SOUND 0
TURRET
DESCRIPTION 0 SERVANT  Droid
DESCRIPTION 1 Class: 247
DROIDTYPE 1
DRIVETYPE 1
BRAINTYPE 1
HEADSECTION 3
HEADOFFSET 0.0 0.0 0.0
FIREOFFSET 0.0 0.0 14.0
HEADROTATIONRATE 0.05
SECTION
Render 95
Parent -1
Tag -1
Rotate 2
Offset 0.0 0.0 12.0
Rotation 0.0 0.0 0.0
SECTION
Render 99
Parent 0
Tag -1
Rotate 0
Offset 0.0 0.0 -2.0
Rotation 0.0 0.0 0.0
SECTION
Render 100
Parent 0
Tag -1
Rotate 0
Offset 0.0 0.0 -1.0
Rotation 0.0 0.0 0.0
SECTION
Render 94
Parent 0
Tag -1
Rotate 2
Offset 0.0 0.0 15.0
Rotation 0.0 0.0 0.0
SECTION
Render 96
Parent 0
Tag -1
Rotate 0
Offset 0.0 10.0 8.0
Rotation 0.0 0.0 0.0
SECTION
Render 96
Parent 0
Tag -1
Rotate 0
Offset 0.0 -10.0 8.0
Rotation 0.0 0.0 3.141590
END
```

---

## Coordinate System

### Source (Uberdroid)
- Z-up coordinate system
- Offset format: `X Y Z` where Y is forward, Z is up
- Units: Game units (approximately inches)

### Target (Unit System)
- Y-up coordinate system (Raylib/GLTF standard)
- Physics plane: X-Z
- Height: Y axis
- Units: Meters

### Conversion
```
Source (x, y, z) Z-up → Target Y-up:
  localOffset: [x * scale, y * scale]  (physics X, Z)
  height: z * scale                     (render Y)
  localRotation: rz                     (yaw only)

Scale factor: 0.0254 (inches to meters)
```

---

## C++ Data Structures

### RenderObject (from render_control.h)

```cpp
enum render_type_t {
    render_type_sprite = 0,
    render_type_model = 1,      // MDL
    render_type_particles = 3,
    render_type_beam = 4,
    render_type_md2 = 5,
    render_type_md3 = 6,
    render_type_q3 = 7,
    render_type_static = 8,     // ASC
    render_type_sectional = 9
};

struct render_object {
    render_type_t type;
    std::string name;
    std::string model_path;
    std::vector<std::string> textures;
    // ... additional properties
};
```

### DroidSection (from droid_class.h)

```cpp
enum droidrotation_t {
    dr_none = 0,
    dr_movement = 1,
    dr_facing = 2
};

struct droidsection_t {
    int mRenderIndex;           // Index into renderobjects
    int mParentSection;         // Parent section index (-1 = root)
    int mParentTag;             // MD3 tag attachment
    droidrotation_t mRotationIndex;
    vector mOffset;             // Position relative to parent
    vector mRotation;           // Rotation offset
};
```

### DroidClass (from droid_class.h)

```cpp
struct droid_class {
    int render_index;
    int head_index;
    int type;
    float energy;
    float armour;
    float weapon;
    float collide_radius;
    float proximity_radius;
    float aggression;
    float vrad;

    // Sensors
    bool visual, aural, ultrasonic, subsonic;
    bool infrared, ultraviolet, radar, disruptor_shielded;

    // Sections
    int mSectionCount;
    droidsection_t mSections[MAX_SECTIONS];

    // Classification
    int mDroidType;
    int mDriveType;
    int mBrainType;
};
```

---

## Model Files Summary

### ASC Models (77 files)
Located in `uber/uberdroid/models/`. Most droid parts use this format.

Subdirectories:
- `models/` - Droid body parts (302.asc, servant_torso.asc, etc.)
- `models/scenery/` - Environment objects (door.asc, console.asc)
- `models/transfer/` - Transfer circuit pieces
- `models/ship/` - Ship model

### Non-ASC Models (need future support)
- `models\chair.mdl` (render 14) - Half-Life MDL
- `models\grey.md2` (render 65) - Quake II MD2
- `models\legs.mdl` (render 143) - Half-Life MDL
