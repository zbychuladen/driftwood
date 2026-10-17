// The title screen, the pause menu, the save-slot picker and short notices.
#include <algorithm>
#include "engine/Draw.h"
#include "game/Game.h"
#include "game/Saves.h"

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
    Rectangle r = card(360, 330);
    float x = r.x + 30, w = r.width - 60, y = r.y + 26;
    text(F.head, "Paused", x, y, 30, T.text);
    std::string where = (sim.villageName.empty() ? std::string("The camp") : sim.villageName) + ", day " + std::to_string(sim.day());
    text(F.body, where, x, y + 42, 14, T.muted);
    y += 78;
    if (button({x, y, w, 40}, "Resume", true, true, 16)) menuOpen = false;
    if (button({x, y + 50, w, 40}, "Save game", true, false, 16)) { picker = Picker::SAVE; confirmSlot = -1; }
    if (button({x, y + 100, w, 40}, "Load game", saves::any(), false, 16)) { picker = Picker::LOAD; confirmSlot = -1; }
    if (button({x, y + 150, w, 40}, "Quit to title", true, false, 16)) { menuOpen = false; screen = Screen::TITLE; titleClock = 0; }
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
