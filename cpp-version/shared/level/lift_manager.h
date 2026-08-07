#ifndef LIFT_MANAGER_H
#define LIFT_MANAGER_H

#include "level_types.h"
#include "raylib.h"
#include <map>
#include <vector>

//------------------------------------------------------------------------------
// Lifts — the logical elevator graph, built from the in-map lift objects (each
// level's TmxLevel::lifts). Every lift tile is a "stop" on an elevator; stops on
// the same elevator form an ordered chain (by stopIndex, 0 = lowest deck). Using
// a lift moves the player up/down that chain to an adjacent stop, which lives on
// another level. Mirrors the ConsoleManager pattern (no physics/Box2D — a pure
// proximity + graph query). See docs/lifts.md.
//------------------------------------------------------------------------------

inline constexpr float LIFT_USE_RADIUS = 0.9f;  // player must be near the lift tile centre
                                                // (~half a 1.6 m tile, so anywhere on the tile works)

struct LiftStop {
    int level = -1;               // runtime level index (position in game->levels)
    int levelNumber = -1;         // stable deck number (for shipmap deck highlight)
    int col = 0;
    int row = 0;
    int elevator = 0;             // which elevator/shaft
    int stopIndex = 0;            // position on the elevator (0 = lowest, ascending up)
    Vector2 physicsCenter = {0, 0};
};

// A ship transporter (from transport.txt), as exported to the 3D bundle's transporters.json in the
// render-metric frame. Feeds the Objects3D lift network (LiftManager::buildFromTransporters).
struct TransporterSpec {
    int id = 0;                   // Label (referenced by levelUp/levelDown)
    int deck = 0;                 // stable deck number (matches TmxLevel::number)
    Vector3 pos = {0, 0, 0};      // render-metric (X right, Y up, Z depth)
    int levelUp = -1;             // transporter Label reached going up (-1 = top)
    int levelDown = -1;           // transporter Label reached going down (-1 = bottom)
    int liftRow = 0;              // elevator/shaft id
};

class LiftManager {
public:
    // Build the stop list + per-elevator ordered chains from all loaded levels.
    void build(const std::vector<TmxLevel>& levels);

    // Objects3D variant: build the network from exported transporters (render-metric frame).
    // Deck numbers map to runtime level indices via TmxLevel::number; per-elevator (liftRow) order
    // is derived by walking each shaft's levelDown→levelUp chain. `levels` supplies the deck→index map.
    void buildFromTransporters(const std::vector<TransporterSpec>& transporters,
                               const std::vector<TmxLevel>& levels);
    void destroy();

    // Per-frame proximity: is the player standing on a lift tile of the current level?
    void update(Vector2 playerPos, int currentLevel);
    bool onLift() const { return currentStop_ != nullptr; }
    const LiftStop* currentStop() const { return currentStop_; }

    // Adjacent stop on the same elevator: delta +1 = up, -1 = down. Null at the ends
    // or if `from` is not a known stop.
    const LiftStop* stepStop(const LiftStop* from, int delta) const;

    const std::vector<LiftStop>& stops() const { return stops_; }

private:
    std::vector<LiftStop> stops_;
    std::map<int, std::vector<int>> elevators_;  // elevator id -> indices into stops_, sorted by stopIndex
    const LiftStop* currentStop_ = nullptr;
};

#endif // LIFT_MANAGER_H
