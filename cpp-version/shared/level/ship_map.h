#ifndef SHIP_MAP_H
#define SHIP_MAP_H

#include "raylib.h"
#include <map>
#include <string>
#include <vector>

//------------------------------------------------------------------------------
// Ship map — the rendering data for the side-on ship view (shipmap.json). Holds
// the background image name, the reference frame the rectangles were authored in,
// and fractional (0..1) rectangles for each elevator shaft and each deck. Decks
// are keyed by level NUMBER (the stable "level_<N>" id), not the runtime array
// index. Purely rendering data — the logical lift graph comes from the maps (see
// LiftManager). See docs/lifts.md.
//------------------------------------------------------------------------------

class ShipMap {
public:
    // Load from a shipmap.json file. Returns false (and leaves loaded()==false) on
    // any error; callers should degrade gracefully.
    bool load(const std::string& path);
    bool loaded() const { return loaded_; }

    const std::string& name() const { return name_; }              // ship display name (metadata)
    const std::string& imageName() const { return image_; }        // dim base
    const std::string& imageLitName() const { return imageLit_; }   // lit overlay (may be empty)

    // Fractional rects (x,y,width,height in 0..1 of the image). Null if absent.
    const Rectangle* elevatorRect(int elevatorId) const;
    const std::vector<Rectangle>* deckRects(int levelNumber) const;

private:
    std::string name_;
    std::string image_;
    std::string imageLit_;
    std::vector<Rectangle> elevators_;            // index = elevator id
    std::map<int, std::vector<Rectangle>> decks_; // level number -> rects
    bool loaded_ = false;
};

#endif // SHIP_MAP_H
