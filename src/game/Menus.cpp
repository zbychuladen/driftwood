// The title screen, the pause menu, the save-slot picker and short notices.
#include <algorithm>
#include "engine/Draw.h"
#include "game/Game.h"
#include "game/Saves.h"

using namespace dw;
using namespace ui;

namespace {
const char* SEASON_NAMES[4] = {"spring", "summer", "autumn", "winter"};

// A card in the middle of the screen, on a soft wash over the world.
Rectangle card(float w, float h) {
    float W = float(GetScreenWidth()), H = float(GetScreenHeight());
    DrawRectangle(0, 0, int(W), int(H), Color{233, 230, 218, 120});
    Rectangle r{(W - w) / 2, (H - h) / 2, w, h};
    DrawRectangleRounded({r.x + 2, r.y + 6, r.width, r.height}, .04f, 8, Color{40, 50, 40, 40});
    panel(r, T.panel, T.line, .04f);
    return r;
}
} // namespace

void Game::say(const std::string& text) {
    toast = text;
    toastUntil = GetTime() + 3;
}

void Game::saveTo(int slot) {
    if (screen != Screen::PLAYING) return;
    std::string err;
    if (saves::write(slot, sim.save(), &err)) say(slot == saves::QUICK ? "Saved to the quick slot." : "Saved to slot " + std::to_string(slot) + ".");
    else say(err);
}

void Game::loadFrom(int slot) {
    std::string data, err;
    if (!saves::read(slot, data, &err) || !sim.load(data, &err)) { say(err); return; }
    // a different island and different people: start the view and the panels afresh
    view.reset(sim);
    selected = -1;
    inspected = {};
    journalVersionCached = -1;
    sandText.clear();
    sandNote.clear();
    speed = 1;
    screen = Screen::PLAYING;
    menuOpen = false;
    picker = Picker::NONE;
    say(std::string("Back on ") + (sim.villageName.empty() ? "the island" : sim.villageName) + ", day " + std::to_string(sim.day()) + ".");
}

void Game::drawTitle() {
    float W = float(GetScreenWidth()), H = float(GetScreenHeight());
    DrawRectangleGradientV(0, 0, int(W), int(H), Color{246, 244, 236, 60}, Color{246, 244, 236, 200});
    if (picker != Picker::NONE) return;
    float cw = 420, x = 70, y = H * .5f - 190;
    panel({x - 30, y - 34, cw + 60, 420}, draw::withA(T.panel, .94f), T.line, .04f);
    text(F.head, "Driftwood", x, y, 64, T.text);
    wrapped(F.body, "Five castaways, an island, and you: the voice they half believe in.", x, y + 82, cw, 17, T.muted);
    float by = y + 150;
    if (button({x, by, cw, 46}, "New game", true, true, 17)) {
        newGame(uint32_t(GetRandomValue(1, 1 << 30)));
        screen = Screen::PLAYING;
    }
    bool anySave = saves::any();
    if (button({x, by + 58, cw, 46}, anySave ? "Load game" : "Load game (no saves yet)", anySave, false, 17)) { picker = Picker::LOAD; confirmSlot = -1; }
    if (button({x, by + 116, cw, 46}, "Quit", true, false, 17)) quit = true;
    text(F.body, "In game: Esc for the menu, F5 to quick save, F9 to quick load.", x, by + 186, 13, T.muted);
}

void Game::drawMenu() {
    if (picker != Picker::NONE) return;
    Rectangle r = card(360, 380);
    float x = r.x + 30, w = r.width - 60, y = r.y + 26;
    text(F.head, "Paused", x, y, 30, T.text);
    std::string where = (sim.villageName.empty() ? std::string("The camp") : sim.villageName) + ", day " + std::to_string(sim.day());
    text(F.body, where, x, y + 42, 14, T.muted);
    y += 78;
    if (button({x, y, w, 40}, "Resume", true, true, 16)) menuOpen = false;
    if (button({x, y + 50, w, 40}, "Save game", true, false, 16)) { picker = Picker::SAVE; confirmSlot = -1; }
    if (button({x, y + 100, w, 40}, "Load game", saves::any(), false, 16)) { picker = Picker::LOAD; confirmSlot = -1; }
    if (button({x, y + 150, w, 40}, settlersPlace ? "Building spots: settlers choose" : "Building spots: you choose", true, false, 15)) settlersPlace = !settlersPlace;
    if (button({x, y + 200, w, 40}, "Quit to title", true, false, 16)) { menuOpen = false; screen = Screen::TITLE; titleClock = 0; }
}

void Game::drawPicker() {
    bool saving = picker == Picker::SAVE;
    auto slots = saves::list();
    float rowH = 62;
    Rectangle r = card(600, 120 + rowH * slots.size() + 50);
    float x = r.x + 26, w = r.width - 52, y = r.y + 22;
    text(F.head, saving ? "Save game" : "Load game", x, y, 28, T.text);
    text(F.body, saving ? "Choose a slot. A used slot asks before it is overwritten." : "Choose a save to go back to.", x, y + 38, 14, T.muted);
    y += 76;
    for (auto& s : slots) {
        Rectangle row{x, y, w, rowH - 8};
        bool usable = saving ? s.n != saves::QUICK || true : (s.used && !s.damaged);
        bool hot = usable && hovered(row);
        DrawRectangleRounded(row, .12f, 6, hot ? T.hover : T.panel2);
        DrawRectangleRoundedLinesEx(row, .12f, 6, 1, T.line);
        text(F.bold, s.name, row.x + 14, row.y + 9, 15, usable ? T.text : T.muted);
        std::string detail, right;
        if (!s.used) detail = "Empty";
        else if (s.damaged) detail = "Damaged save, cannot be read";
        else {
            detail = (s.info.village.empty() ? std::string("An unnamed camp") : s.info.village) + ", day " + std::to_string(s.info.day) + ", " +
                     SEASON_NAMES[std::clamp(s.info.season, 0, 3)] + " of year " + std::to_string(s.info.year) + ", " + std::to_string(s.info.people) + " people";
            right = s.when;
        }
        if (saving && confirmSlot == s.n) detail = "Click again to overwrite this save.";
        text(F.body, detail, row.x + 14, row.y + 29, 13.5f, saving && confirmSlot == s.n ? T.berry : T.muted);
        if (!right.empty()) text(F.body, right, row.x + row.width - 14 - measure(F.body, right, 13).x, row.y + 10, 13, T.muted);
        if (usable && clicked(row)) {
            if (!saving) loadFrom(s.n);
            else if (s.used && confirmSlot != s.n) confirmSlot = s.n;
            else { saveTo(s.n); picker = Picker::NONE; confirmSlot = -1; }
        }
        y += rowH;
    }
    if (button({x, y + 6, 120, 36}, "Back", true, false, 15)) { picker = Picker::NONE; confirmSlot = -1; }
}

void Game::drawToast() {
    if (toast.empty() || GetTime() > toastUntil) return;
    float W = float(GetScreenWidth()), H = float(GetScreenHeight());
    Vector2 m = measure(F.bold, toast, 15);
    Rectangle r{(W - m.x) / 2 - 18, H - 120, m.x + 36, 36};
    DrawRectangleRounded(r, .5f, 8, T.dark);
    text(F.bold, toast, r.x + 18, r.y + 9, 15, Color{246, 244, 236, 255});
}

// The narrator choosing a spot for a specialist building: what it is, who will
// build it, how good the spot under the pointer is and why, and what people say.
void Game::drawPlacement() {
    Project* b = sim.projectById(sim.pendingPlacement->project);
    if (!b) return;
    const auto& g = view.ghost;
    float W = float(GetScreenWidth());
    float w = 580, x = W / 2 - w / 2 - (rSide.width / 2), y = rTop.height + 12, pad = 18;
    x = std::max(12.f, x);
    std::string champ;
    if (auto ch = sim.champion(*b)) champ = ch->p->name + " will lead the work.";
    SiteReport r = g.legal ? sim.evaluateSite(*b, g.x, g.y) : SiteReport{};
    auto ops = g.legal ? sim.opinions(*b, g.x, g.y) : std::vector<Opinion>{};
    // measure, then draw
    int lines = 3 + int(std::max<size_t>(1, std::min<size_t>(3, r.factors.size()))) + int(std::min<size_t>(3, ops.size()));
    float h = pad * 2 + 30 + 22 * lines + 44;
    placeH = h;
    Rectangle card{x, y, w, h};
    DrawRectangleRounded({x + 2, y + 5, w, h}, .05f, 8, Color{40, 50, 40, 40});
    panel(card, draw::withA(T.panel, .97f), T.line, .05f);
    float yy = y + pad;
    text(F.head, "Where should the " + lower(b->name) + " go?", x + pad, yy, 22, T.text);
    yy += 32;
    text(F.body, (champ.empty() ? std::string() : champ + " ") + "Click a spot on the map, or let them choose.", x + pad, yy, 14, T.muted);
    yy += 26;
    if (!g.legal) {
        text(F.bold, "It cannot go here.", x + pad, yy, 15, T.berry);
        yy += 22;
    } else {
        float q = r.quality;
        std::string word = q >= 1.15f ? "An excellent spot" : q >= 1.03f ? "A good spot" : q >= .92f ? "A fair spot" : q >= .8f ? "A poor spot" : "A bad spot";
        Color qc = q >= 1.03f ? T.ok : q >= .92f ? T.ember : T.berry;
        text(F.bold, word, x + pad, yy, 15, qc);
        bar({x + pad + 160, yy + 4, 160, 9}, (q - .6f) / .65f, qc, T.panel2);
        yy += 24;
        // the strongest reasons either way
        std::vector<SiteFactor> fs = r.factors;
        std::sort(fs.begin(), fs.end(), [](const SiteFactor& a, const SiteFactor& c) { return std::abs(a.weight) > std::abs(c.weight); });
        if (fs.empty()) { text(F.body, "Nothing much for or against this spot.", x + pad + 16, yy, 14, T.muted); yy += 22; }
        for (size_t i = 0; i < fs.size() && i < 3; i++) {
            bool good = fs[i].weight > 0;
            text(F.bold, good ? "+" : "-", x + pad, yy, 15, good ? T.ok : T.berry);
            text(F.body, fs[i].text, x + pad + 16, yy, 14, T.text);
            yy += 22;
        }
        for (size_t i = 0; i < ops.size() && i < 3; i++) {
            Person* p = sim.person(ops[i].pid);
            if (!p) continue;
            std::string who = p->name;
            if (ops[i].expertise >= .45f && !b->gift.empty()) who += " (" + lower(giftInfo().at(b->gift).n) + ")";
            text(F.bold, who, x + pad, yy, 14, T.text);
            text(F.hand, ops[i].say, x + pad + measure(F.bold, who, 14).x + 10, yy - 3, 20, ops[i].perceived < .9f ? T.berry : T.muted);
            yy += 22;
        }
    }
    if (button({x + pad, y + h - pad - 34, 170, 34}, "Let them choose", true, false, 14)) sim.placeSuggested(b->id);
    text(F.body, "The dashed outline is their choice.", x + pad + 186, y + h - pad - 25, 13, T.muted);
}
