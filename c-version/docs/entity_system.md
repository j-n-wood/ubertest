# Entity System

The entity system provides a simple pooled architecture for game objects, combining 3D rendering with 2D top-down physics.

## Entity Structure

Each entity contains:

| Field | Type | Description |
|-------|------|-------------|
| `type` | `EntityType` | Classification determining behavior and physics |
| `position` | `Vector3` | World position (Y is height, usually 0 for top-down) |
| `rotation` | `float` | Y-axis rotation in radians |
| `model` | `Model` | Raylib model for rendering (GLTF/GLB) |
| `has_model` | `bool` | Whether a model is loaded |
| `physics` | `PhysicsBody` | Optional Box2D body attachment |
| `active` | `bool` | Whether entity is in use |

## Entity Pool

Entities are stored in a fixed-size array (`MAX_ENTITIES = 1024`) within the `Game` struct. This avoids dynamic allocation during gameplay.

```c
Entity entities[MAX_ENTITIES];
int entity_count;
```

New entities are created via `procgen_spawn_entity()` which:
1. Claims the next slot in the pool
2. Initializes entity fields
3. Loads model if path provided
4. Creates appropriate physics body based on type

## Physics-Graphics Synchronization

The coordinate mapping between 2D physics and 3D rendering:

| Box2D (2D) | Raylib (3D) |
|------------|-------------|
| X | X |
| Y | Z |
| — | Y (height) |

Each frame, `entity_sync_from_physics()` updates the entity's 3D position and rotation from its physics body.

## Entity Types

### ENTITY_PLAYER

The player-controlled character.

**Physics**: Dynamic circle body (radius 0.5)
- Responds to forces and collisions
- Linear damping (4.0) simulates top-down friction
- Can be moved via `physics_body_apply_force()`

**Rendering**: Custom model or placeholder cube

### ENTITY_ENEMY

AI-controlled hostile entities.

**Physics**: Dynamic circle body (radius 0.5)
- Same physical properties as player
- Can pathfind and apply forces for movement

**Rendering**: Custom model or placeholder cube

### ENTITY_OBSTACLE

Static collision geometry (walls, barriers, terrain features).

**Physics**: Static box body
- Does not move or respond to forces
- Blocks dynamic bodies

**Rendering**: Custom model or placeholder cube

### ENTITY_PROP

Decorative or interactive objects without mandatory physics.

**Physics**: None by default
- Can be extended to add physics if needed

**Rendering**: Custom model (e.g., `ellipsoid.gltf`)

## Creating Entities

Use `procgen_spawn_entity()` to create entities:

```c
// Player at origin
procgen_spawn_entity(game, ENTITY_PLAYER, (Vector3){0, 0, 0}, NULL);

// Prop with custom model
procgen_spawn_entity(game, ENTITY_PROP, (Vector3){3, 0, -2}, "assets/models/ellipsoid.gltf");

// Static obstacle
procgen_spawn_entity(game, ENTITY_OBSTACLE, (Vector3){5, 0, 5}, NULL);
```

## Extending the System

To add a new entity type:

1. Add enum value to `EntityType` in `entity.h`
2. Add physics creation case in `procgen_spawn_entity()`
3. Optionally add type-specific update logic in `game_update()`
