#ifndef WEAPON_EDITOR_PAGE_H
#define WEAPON_EDITOR_PAGE_H

#include "pages/page.h"
#include <string>

//------------------------------------------------------------------------------
// Runtime weapon-tuning overlay (opened with F4). Like every non-Game page it sits on
// top of GamePage, so gameplay freezes (pauses) while it's open. It exposes the numeric
// fields of each weapon definition (damage, speed, fireRate, maxRange, optimumRange) as
// sliders *and* editable numeric text boxes, bound to the in-memory weapon table. UP/DOWN
// cycle weapons; a Save button overwrites assets/data/weapons.json. Edits are live-applied
// to the player's and AI's active weapon states each frame, so they take effect the moment
// you ESC back into the game. Mirrors the DroidLibraryPage debug editor for unit stats.
//------------------------------------------------------------------------------

class WeaponEditorPage : public Page {
public:
    using Page::Page;

    static constexpr int kFieldCount = 5;   // damage, speed, fireRate, maxRange, optimumRange

    void activate() override;
    void handleInput() override;
    void update(float dt) override;
    void render() override;

private:
    // Push the (possibly edited) definitions into the player's and AI's cached weapon
    // states so tuning is audible/visible immediately on return to gameplay.
    void syncLiveWeaponStates();
    // Reset the text-box buffers/edit flags to the current weapon's values.
    void refreshBuffers();

    int index_ = 0;                       // current weapon (table index)
    int shownWeaponId_ = -1;              // weapon id the text buffers currently reflect
    char textBuf_[kFieldCount][32] = {};  // numeric text-box contents (raygui edits in place)
    bool editMode_[kFieldCount] = {};     // per-field text-edit state

    std::string saveMsg_;
    float saveMsgTimer_ = 0.0f;
};

#endif // WEAPON_EDITOR_PAGE_H
