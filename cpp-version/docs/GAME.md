# Game overview

## Architecture

* raylib rendering following openGL conventions
* box2D physics
* GLTF 3d models

Level definitions are loaded and then realised into rendering and physics objects. Different rules
for realisation are possible.

## Game Units

The active entities that represent the player or enemies are instances of predefined units.
The player will be able to control and change what kind of unit they are using.

## Rendering style

The game will be rendered from a top down view, generally in perspective camera mode. Lighting effects
are generated from the point of view of the playre unit, not the camera.

The first world render style will be 2D tiles rendered in perspective. Units and effects will be in
3D. Later we will add 3D geometry as a different realisation of the level definitions.

## Gameplay

There are many levels in a 'ship' which are all loaded and eventually simulated simultaneously.
Initially simulation is only live for the level the player is on.

When a player starts the game or completes a ship then the next ship is loaded and a starting level selected.
A random waypoint on the level can be used as starting position.

Units of types specified for the level will be spawned at other waypoints. Some of these are aggressive,
others are not. Each will have definitions for health, armour and weapons. Those that have weapons
will attempt to attack the player.

The player will start as unit type 0 and attempt to destroy or take over (capture) enemy units.

Capture of a unit will put that under player control. The unit type 0 will be rendered above (+Y) the
captured unit. If the captured unit has weapons, the player controls will use those weapons. Otherwise
the type 0 weapons will be used. Only one unit can be captured at a time, capturing another destroys
the previously captured unit.

Captured units slowly lose health over time until they are destroyed, when the player resumes control
of the type 0 unit.

When all enemy units in a level are destroyed or have been captured, the level lights are dimmed.
When all enemy units on a ship are destroyed or captured, the ship is cleared and the next ship loaded.
Players may retain a captured unit to transfer to the next ship.

## Level design

Level data is loaded from data files into abstract types, then converted to rendering and physics data
for gameplay.

The player changes levels by activating lift tiles in the map data. Each life tile accesses a lift
definition which can transfer the player to related lift tiles in other levels. There are multiple
lift definitions and not all lifts reach all levels. Multiple lifts can access different lift tiles
in a level.

