// inspector.cpp — raygui link inspector: live-edit each path link's profiles and direction, add /
// remove links, and save the edited deck. Every edit rebuilds geometry (full rebuild per edit —
// inefficient but immediate, which is fine for a debug tool).
#include "viewer.h"
#include "raylib.h"
#include "raygui.h"

#include <algorithm>
#include <vector>

namespace {

// The first geometry block (where add/remove operate) and the deck's default profile set.
PathGeometry* firstGeometry(Viewer* v, std::vector<int>& defaultSet) {
    defaultSet.clear();
    for (auto& area : v->loadedDomain.areas)
        for (auto& geom : area.geometry) {
            for (const auto& p : geom.profiles) defaultSet.push_back(p.id);
            return &geom;
        }
    return nullptr;
}

std::vector<int> effectiveProfiles(const PathLink* l, const std::vector<int>& defaultSet) {
    if (!l->profiles.empty()) return l->profiles;
    if (l->useDefaultProfiles) return defaultSet;
    return {};
}

}  // namespace

void viewerDrawInspector(Viewer* viewer) {
    if (!viewer || !viewer->showInspector || !viewer->domainLoaded) return;

    std::vector<int> defaultSet;
    PathGeometry* geom = firstGeometry(viewer, defaultSet);
    if (!geom) return;
    std::vector<PathLink>& links = geom->links;

    const int PROFS[] = {0, 1, 2, 3, 4};
    const int NP = 5;
    const float rowH = 24.0f;
    const int W = GetScreenWidth(), H = GetScreenHeight();

    // Two-row toolbar above the scroll panel.
    const float px = static_cast<float>(W) - 430.0f;
    DrawText(TextFormat("Link Inspector  (%zu links)   [L close]", links.size()),
             static_cast<int>(px), 14, 14, YELLOW);
    bool changed = false;

    // Row 1: save / save-all / revert.
    float r1 = 30.0f;
    if (GuiButton({px, r1, 70.0f, 22.0f}, "Save")) viewerSaveEdited(viewer);
    if (GuiButton({px + 76.0f, r1, 90.0f, 22.0f}, "Save All")) viewerSaveAll(viewer);
    if (GuiButton({px + 172.0f, r1, 90.0f, 22.0f}, "Revert")) { viewerRevertToOriginal(viewer); return; }

    // Row 2: add link.
    float r2 = 56.0f;
    GuiLabel({px, r2, 16.0f, 22.0f}, "s");
    if (GuiValueBox({px + 16.0f, r2, 46.0f, 22.0f}, NULL, &viewer->addLinkStart, 0, 100000,
                    viewer->addEditStart)) viewer->addEditStart = !viewer->addEditStart;
    GuiLabel({px + 70.0f, r2, 16.0f, 22.0f}, "f");
    if (GuiValueBox({px + 86.0f, r2, 46.0f, 22.0f}, NULL, &viewer->addLinkFinish, 0, 100000,
                    viewer->addEditFinish)) viewer->addEditFinish = !viewer->addEditFinish;
    if (GuiButton({px + 142.0f, r2, 90.0f, 22.0f}, "Add link")) {
        int maxId = -1;
        for (const auto& l : links) maxId = std::max(maxId, l.id);
        PathLink nl;
        nl.id = maxId + 1;
        nl.start = viewer->addLinkStart;
        nl.finish = viewer->addLinkFinish;
        nl.useDefaultProfiles = true;  // gets the default wall profiles
        links.push_back(nl);
        changed = true;
    }

    // Scrollable link list.
    Rectangle panel = {px, 86.0f, 420.0f, static_cast<float>(H) - 136.0f};
    Rectangle content = {0, 0, panel.width - 18.0f, links.size() * rowH + 8.0f};
    Rectangle view = {0};
    GuiScrollPanel(panel, NULL, content, &viewer->inspectorScroll, &view);

    int removeIndex = -1;
    BeginScissorMode(static_cast<int>(view.x), static_cast<int>(view.y),
                     static_cast<int>(view.width), static_cast<int>(view.height));
    for (size_t i = 0; i < links.size(); ++i) {
        PathLink& l = links[i];
        float y = panel.y + viewer->inspectorScroll.y + 4.0f + static_cast<float>(i) * rowH;
        if (y + rowH < view.y || y > view.y + view.height) continue;  // cull offscreen rows

        float x = panel.x + 6.0f;
        GuiLabel({x, y, 84.0f, rowH}, TextFormat("L%d %d>%d", l.id, l.start, l.finish));
        x += 86.0f;

        std::vector<int> eff = effectiveProfiles(&l, defaultSet);
        for (int k = 0; k < NP; ++k) {
            int p = PROFS[k];
            bool has = std::find(eff.begin(), eff.end(), p) != eff.end();
            bool before = has;
            GuiCheckBox({x, y + 3.0f, 16.0f, 16.0f}, TextFormat("%d", p), &has);
            if (has != before) {
                std::vector<int> next = eff;
                if (has) { if (std::find(next.begin(), next.end(), p) == next.end()) next.push_back(p); }
                else next.erase(std::remove(next.begin(), next.end(), p), next.end());
                std::sort(next.begin(), next.end());
                l.profiles = next;
                l.useDefaultProfiles = false;  // edits are explicit (empty => no wall)
                changed = true;
            }
            x += 30.0f;
        }

        if (GuiButton({x, y + 1.0f, 40.0f, 20.0f}, "Rev")) { std::swap(l.start, l.finish); changed = true; }
        x += 44.0f;
        if (GuiButton({x, y + 1.0f, 34.0f, 20.0f}, "Del")) { removeIndex = static_cast<int>(i); }
    }
    EndScissorMode();

    if (removeIndex >= 0) {
        links.erase(links.begin() + removeIndex);
        changed = true;
    }

    if (changed) viewerRebuildMeshes(viewer);
}
