#include "game/Game.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include "engine/Draw.h"
#include "game/Art.h"
#include "game/Building.h"
#include "game/Figure.h"
#include "game/Iso.h"
#include "game/Saves.h"
#include "raylib.h"
#include "rlgl.h"

static const float MIN_PER_SEC = 14;   // game minutes per real second at "Slow"

void Game::newGame(uint32_t seed) {
    sim.newWorld(seed);
    view.init(sim);
    selected = -1;
    speed = 1;
    sandText.clear();
    sandNote.clear();
    journalVersionCached = -1;
    yearsShown = 0;
    endingSeen = false;
}

void Game::select(int id, bool center) {
    selected = id;
    if (center) if (auto* p = sim.person(id)) view.centerOnTile(p->x, p->y - .6f);
}

void Game::layout() {
    float W = float(GetScreenWidth()), H = float(GetScreenHeight());
    float side = std::min(420.f, std::max(340.f, W * .26f));
    rTop = {0, 0, W, 58};
    rSide = {W - side, rTop.height, side, H - rTop.height};
    float jh = journalOpen ? std::min(230.f, H * .27f) : 40;
    rJournal = {12, H - jh - 12, std::min(rSide.x - 24, 760.f), jh};
    rMap = {0, rTop.height, rSide.x, H - rTop.height};
}

void Game::frame(float dt) {
    ui::beginFrame();
    layout();
    const bool title = screen == Screen::TITLE;
    const bool ended = !title && !sim.ending.empty() && !endingSeen;   // the ending screen
    const bool overlay = title || menuOpen || picker != Picker::NONE || ended;
    // a year in review pauses the island like a fate card
    bool fate = !sim.pendingFate.empty() || (!title && int(sim.years.size()) > yearsShown);
    // the narrator places specialist buildings (unless the settlers have been left to it)
    sim.askPlacement = !title && !settlersPlace;
    const bool placing = !title && sim.pendingPlacement.has_value();
    if (placing && !overlay) ui::block({float(GetScreenWidth()) / 2 - 290, rTop.height + 12, 580, placeH});
    if (overlay) ui::block({0, 0, float(GetScreenWidth()), float(GetScreenHeight())});
    else {
        ui::block(rTop);
        ui::block(rSide);
        ui::block(rJournal);
        if (inspected.ok) ui::block({16, rTop.height + 16, 340, inspectH});
        if (decisionsR.height > 0) ui::block(decisionsR);
        if (fate) ui::block(rMap);
    }
    if (!overlay) view.handleInput(dt, rMap, !ui::mouseOverUi() && !fate);
    else view.handleInput(0, rMap, false);   // keeps the camera framed to the map, ignores the pointer
    if (IsKeyPressed(KEY_ESCAPE) && !(sandFocus || renameFocus)) {
        // Esc backs out one step: the picker, a building waiting for its spot (the settlers choose), the info card, then the menu
        if (picker != Picker::NONE) { picker = Picker::NONE; confirmSlot = -1; }
        else if (placing && !menuOpen) { if (sim.pendingPlacement->chosen) sim.cancelBlueprint(); else sim.placeSuggested(sim.pendingPlacement->project); }
        else if (!title && inspected.ok) inspected = {};
        else if (!title) menuOpen = !menuOpen;
    }
    if (!title && !(sandFocus || renameFocus) && picker == Picker::NONE) {
        if (IsKeyPressed(KEY_F5)) saveTo(saves::QUICK);
        if (IsKeyPressed(KEY_F9)) loadFrom(saves::QUICK);
    }
    if (!overlay && !(sandFocus || renameFocus)) {
        if (IsKeyPressed(KEY_SPACE)) speed = speed > 0 ? (prevSpeed = speed, 0.f) : (prevSpeed > 0 ? prevSpeed : 1.f);
        if (IsKeyPressed(KEY_ONE)) speed = 1;
        if (IsKeyPressed(KEY_TWO)) speed = 2;
        if (IsKeyPressed(KEY_THREE)) speed = 5;
        if (IsKeyPressed(KEY_J)) journalOpen = !journalOpen;
        if (IsKeyPressed(KEY_C)) view.showAllLinks = !view.showAllLinks;
        if (IsKeyPressed(KEY_R) && sim.pendingPlacement) sim.rotatePlacement();   // turn a footbridge
    }
    if (title) {
        // the island goes on living behind the title, the camera drifting round camp
        sim.step(dt * MIN_PER_SEC);
        if (!sim.pendingFate.empty()) { auto f = sim.pendingFate[0]; sim.pendingFate.clear(); sim.applyFate(f); }
        titleClock += dt;
        float a = titleClock * .05f;
        view.centerOnTile(sim.campX + .5f + std::cos(a) * 4, sim.campY + .5f + std::sin(a) * 4);
        view.cam.zoom = 1.25f;
    } else if (!overlay && !fate && speed > 0) sim.step(dt * speed * MIN_PER_SEC);
    // a new question: open it, and pause for it if the narrator asked to be
    if (!title)
        for (auto& q : sim.questions)
            if (q.id > seenQuestion) {
                seenQuestion = q.id;
                if (pauseQuestions && speed > 0) { prevSpeed = speed; speed = 0; openQuestion = q.id; }
            }
    // what the pointer is over, for name tags
    Vector2 mouse = GetMousePosition();
    view.hovered = !overlay && !fate && !ui::mouseOverUi() && CheckCollisionPointRec(mouse, rMap) ? view.pickAt(sim, mouse) : Pick{};
    // where the building under the pointer would go, and what that spot is like
    view.ghost.on = false;
    if (sim.pendingPlacement && !title) {
        dw::Project* b = sim.projectById(sim.pendingPlacement->project);
        if (b) {
            int gx, gy;
            // captures aim at a given point, or at the middle of the map (known only once laid out)
            Vector2 aim = !placingOverride ? GetMousePosition() : placingAt.x < 0 ? Vector2{rMap.x + rMap.width / 2, rMap.y + rMap.height / 2} : placingAt;
            view.footprintAt(aim, b->w, b->h, gx, gy);
            dw::SiteReport r = sim.evaluateSite(*b, gx, gy);
            view.ghost = {true, r.legal, gx, gy, b->w, b->h, sim.pendingPlacement->sx, sim.pendingPlacement->sy, r.quality};
            if (!overlay && view.clickedMap() && r.legal) sim.placeAt(b->id, gx, gy);
        }
    } else if (!overlay && view.clickedMap()) {
        // a settler opens their card in the sidebar; anything else gets the info card
        Pick p = view.pickAt(sim, view.clickPos());
        select(p.ok && p.kind == Drawable::PERSON ? p.id : -1, false);
        inspected = p.ok && p.kind != Drawable::PERSON ? p : Pick{};
    }
    if (inspected.ok && describe(sim, inspected).title.empty()) inspected = {};   // it is gone

    BeginDrawing();
    rlDisableBackfaceCulling();   // 2D shapes come in both windings
    if (title) {
        // the title shows the whole window of island, no sidebar
        Rectangle all{0, 0, float(GetScreenWidth()), float(GetScreenHeight())};
        view.cam.offset = {all.width / 2, all.height / 2};
        view.draw(sim, all, -1, dt, 1, {});
        drawTitle();
    } else {
        view.draw(sim, rMap, selected, dt, sim.pendingFate.empty() && !overlay && !sim.pendingPlacement ? speed : 0, inspected);
        drawHud();
        if (sim.pendingPlacement && !menuOpen) drawPlacement();
        if (int(sim.years.size()) > yearsShown && sim.pendingFate.empty() && !ended) drawYearReview();
        if (menuOpen) drawMenu();
        if (ended) drawEnding();
    }
    if (picker != Picker::NONE) drawPicker();
    drawToast();
    EndDrawing();
}

// A plant sheet for judging the art: rows of each tree kind across the four
// seasons and growth stages, on plain ground with the world's tone and ink.
void Game::drawGallery() {
    ClearBackground(Color{233, 230, 218, 255});
    Camera2D cam{};
    cam.zoom = opt.zoom > 0 ? opt.zoom : 1.6f;
    cam.offset = {0, 0};
    cam.target = {0, 0};
    draw::tone = {.32f, .2f, {236, 233, 220, 255}};
    draw::ink = {std::max(.55f, 1.05f / cam.zoom), {52, 62, 52, 255}};
    BeginMode2D(cam);
    rlDisableBackfaceCulling();
    struct Row { const char* label; bool palm, conifer; float size; bool sapling; float grow; };
    std::vector<Row> rows = {{"broadleaf", false, false, 1, false, 0}, {"broadleaf, size .8 to 1.25", false, false, 1.25f, false, 0},
                             {"conifer", false, true, 1, false, 0}, {"palm", true, false, 1, false, 0}, {"saplings", false, false, 1, true, .4f}};
    const float colW = 150, rowH = 175, x0 = 90, y0 = 165;
    for (size_t r = 0; r < rows.size(); r++)
        for (int c = 0; c < 8; c++) {
            dw::Obj o;
            o.type = "tree"; o.palm = rows[r].palm; o.conifer = rows[r].conifer; o.i = c * 7 + int(r) * 3;
            o.size = r == 1 ? .8f + c * .065f : rows[r].size;
            o.sapling = rows[r].sapling; o.grow = rows[r].sapling ? c / 7.f : 0;
            int season = c % 4;
            // place the tree's ground point in screen space, then find the tile there
            Vector2 at{x0 + c * colW, y0 + r * rowH};
            Vector2 t = iso::toTile(at.x / cam.zoom, at.y / cam.zoom);
            o.x = int(std::floor(t.x)); o.y = int(std::floor(t.y));
            art::Ctx ctx{&sim, 0, season, 12};
            ctx.effects = false;
            
            art::tree(ctx, o);
        }
    EndMode2D();
    draw::tone = {};
    draw::ink.width = 0;
    const char* seasons[4] = {"spring", "summer", "autumn", "winter"};
    for (int c = 0; c < 8; c++) ui::text(ui::F.bold, seasons[c % 4], x0 + c * colW - 24, 12, 14, ui::T.muted);
    for (size_t r = 0; r < rows.size(); r++) ui::text(ui::F.bold, rows[r].label, 8, y0 + r * rowH + 8, 12, ui::T.muted);
}

// A sheet of settler figures for judging the art: facings, looks, a child, the
// walk cycle, and the simple far-away version beside the full one at the
// game's default zoom.
void Game::drawPeopleGallery() {
    ClearBackground(Color{233, 230, 218, 255});
    // laid out for a 1350-pixel-tall window, scaled to whatever window it gets
    const float fit = GetScreenHeight() / 1350.f;
    rlDisableBackfaceCulling();
    const uint32_t skins[4] = {0xf1c8a0, 0xd9a273, 0xa86e45, 0x6e4430}, hairs[4] = {0x2a2420, 0x6a4428, 0xc89850, 0x8a8a88};
    const uint32_t jackets[6] = {0xc8553d, 0x3d7ec8, 0xe0b13a, 0x5a9a5b, 0x8d5bb5, 0x2fa39b};
    auto look = [&](int i, bool child = false) {
        figure::Look l;
        l.jacket = draw::hex(jackets[i % 6]); l.shirt = draw::hex(i % 2 ? 0xece6da : 0xd9d4c8);
        l.legs = draw::hex(i % 3 == 0 ? 0x5a6f8e : i % 3 == 1 ? 0x8a7a58 : 0x4e5a4a);
        l.shoes = draw::hex(i % 2 ? 0xb5523c : 0x6e5440); l.skin = draw::hex(skins[i % 4]); l.hair = draw::hex(hairs[(i + 1) % 4]);
        l.hairStyle = i % 3; l.shorts = i % 2 == 0; l.child = child;
        return l;
    };
    auto stage = [&](float zoom, auto body) {
        Camera2D cam{};
        cam.zoom = zoom;
        draw::tone = {.32f, .2f, {236, 233, 220, 255}};
        draw::ink = {std::max(.55f, 1.05f / zoom), {52, 62, 52, 255}};
        BeginMode2D(cam);
        body(cam);
        EndMode2D();
        draw::tone = {};
        draw::ink.width = 0;
    };
    // a figure whose feet land on a given screen point
    auto at = [&](const Camera2D& cam, float sx, float sy, figure::Facing f, const figure::Look& l, const figure::Pose& p, int detail) {
        Vector2 t = iso::toTile(sx * fit / cam.zoom, sy * fit / cam.zoom);
        Vector2 g = iso::wp(t.x, t.y);
        draw::ellipse(g.x, g.y, 9, 4, Color{70, 76, 56, 40});
        figure::draw(t.x, t.y, f, l, p, detail);
    };
    const float Z = 4.2f * fit;
    stage(Z, [&](const Camera2D& cam) {
        const figure::Facing fs[4] = {figure::SE, figure::SW, figure::NE, figure::NW};
        for (int i = 0; i < 4; i++) at(cam, 90 + i * 110, 210, fs[i], look(0), figure::idle(0), 1);
        for (int i = 1; i < 5; i++) at(cam, 520 + (i - 1) * 110, 210, figure::SE, look(i), figure::idle(i), 1);
        at(cam, 960, 210, figure::SE, look(5, true), figure::idle(0), 1);
        at(cam, 1060, 210, figure::SW, look(2, true), figure::idle(0), 1);
        for (int i = 0; i < 8; i++) at(cam, 90 + i * 120, 440, figure::SE, look(1), figure::walk(i * 6.283f / 8), 1);
        for (int i = 0; i < 4; i++) at(cam, 90 + i * 120, 670, figure::NW, look(3), figure::walk(i * 6.283f / 4), 1);
        for (int i = 0; i < 4; i++) at(cam, 600 + i * 120, 670, figure::SW, look(4), figure::walk(i * 6.283f / 4), 1);
        // every activity, and carrying
        const char* acts[] = {"chop", "mine", "build", "fish", "forage", "farm", "drink", "eat", "sit", "music", "take"};
        for (int i = 0; i < 11; i++) at(cam, 70 + i * 112, 920, figure::SE, look(i), figure::activity(acts[i], 1.3f, 0), 1);
        const char* loads[] = {"wood", "stone", "fish", "food", "meal"};
        for (int i = 0; i < 5; i++) { auto p = figure::walk(.8f); figure::carrying(p, loads[i]); at(cam, 70 + i * 112, 1150, figure::SW, look(i + 2), p, 1); }
        auto sick = figure::idle(0);
        figure::unwell(sick);
        at(cam, 650, 1150, figure::SE, look(3), sick, 1);
        at(cam, 790, 1150, figure::SE, look(4), figure::activity("sleep", 0, 0), 1);
        auto hermit = look(1);
        hermit.beard = true;
        at(cam, 960, 1150, figure::SW, hermit, figure::idle(0), 1);
    });
    // at the game's default zoom: simple and full versions side by side
    stage(1.15f, [&](const Camera2D& cam) {
        for (int i = 0; i < 6; i++) at(cam, 120 + i * 40, 1290, figure::SE, look(i), figure::idle(i), 0);
        for (int i = 0; i < 6; i++) at(cam, 480 + i * 40, 1290, figure::SE, look(i), figure::idle(i), 1);
    });
    ui::text(ui::F.bold, "four facings, one settler", 30 * fit, 70 * fit, 14, ui::T.muted);
    ui::text(ui::F.bold, "other settlers, and two children", 520 * fit, 70 * fit, 14, ui::T.muted);
    ui::text(ui::F.bold, "walking, facing the camera", 30 * fit, 300 * fit, 14, ui::T.muted);
    ui::text(ui::F.bold, "walking away, and across", 30 * fit, 530 * fit, 14, ui::T.muted);
    ui::text(ui::F.bold, "work and daily life: chop, mine, build, fish, forage, farm, drink, eat, sit, music, take", 30 * fit, 760 * fit, 14, ui::T.muted);
    ui::text(ui::F.bold, "carrying wood, stone, a fish, a basket, a meal; sick; asleep; the hermit", 30 * fit, 990 * fit, 14, ui::T.muted);
    ui::text(ui::F.bold, "at the default zoom: simple far-away version, then full", 30 * fit, 1210 * fit, 14, ui::T.muted);
}

// A sheet of assembled buildings: each type in three variants (doors toward
// the camera on either side), then one longhouse going up stage by stage,
// with its door open and its windows lit at the end.
void Game::drawBuildingGallery(bool structures) {
    ClearBackground(Color{233, 230, 218, 255});
    rlDisableBackfaceCulling();
    const float fit = GetScreenHeight() / (structures ? 1560.f : 1600.f);
    Camera2D cam{};
    cam.zoom = 1.15f * fit;
    draw::tone = {.32f, .2f, {236, 233, 220, 255}};
    draw::ink = {std::max(.55f, 1.05f / cam.zoom), {52, 62, 52, 255}};
    BeginMode2D(cam);
    auto place = [&](float sx, float sy, int w, int h, int& tx, int& ty) {
        Vector2 t = iso::toTile(sx * fit / cam.zoom, sy * fit / cam.zoom);
        tx = int(std::floor(t.x)) - w / 2; ty = int(std::floor(t.y)) - h / 2;
    };
    struct T { const char* key; int w, h; };
    const T houses[13] = {{"hut", 2, 2}, {"cottage", 2, 2}, {"house", 2, 2}, {"manor", 3, 3}, {"leanto", 3, 1}, {"longhouse", 4, 2}, {"store", 2, 2}, {"school", 2, 2}, {"forge", 2, 2}, {"smoke", 2, 1}, {"loomhouse", 2, 2}, {"infirmary", 2, 2}, {"workshop", 2, 2}};
    const T others[11] = {{"well", 1, 1}, {"rack", 1, 1}, {"tower", 1, 1}, {"lighthouse", 1, 1}, {"signal", 1, 1}, {"mine", 1, 1}, {"dock", 1, 3}, {"circle", 2, 2}, {"pottery", 2, 2}, {"boatyard", 2, 2}, {"monument", 1, 1}};
    const T* types = structures ? others : houses;
    const int count = structures ? 11 : 13;
    for (int i = 0; i < count * 3; i++) {
        const T& ty = types[i / 3];
        int col = i % 6, row = i / 6;
        int tx, tyy;
        place(150 + col * 265, (structures ? 200 : 185) + row * (structures ? 205 : 185), ty.w, ty.h, tx, tyy);
        int front = (i % 3) == 1 ? 1 : 0;
        auto d = bld::make(ty.key, 11 + i * 7, tx, tyy, ty.w, ty.h, front, false, 0, 1);
        bld::draw(d, 1, 0, false);
    }
    if (structures) {
        // a watchtower and a boat going up, stage by stage
        const float stages[6] = {.08f, .25f, .5f, .75f, .95f, 1};
        for (int i = 0; i < 6; i++) {
            int tx, tyy;
            place(110 + i * 170, 1500, 1, 1, tx, tyy);
            bld::draw(bld::make("tower", 77, tx, tyy, 1, 1, 0, false), stages[i], 0, false);
            place(1130 + (i % 3) * 170, 1500, 2, 2, tx, tyy);
            if (i >= 3) continue;
            bld::draw(bld::make("boatyard", 78, tx, tyy, 2, 2, 0, false), stages[i * 2 + 1], 0, false);
        }
    }
    // construction of one longhouse, then finished with the door open, then lit at night
    const float stages[7] = {.06f, .22f, .45f, .62f, .8f, .95f, 1};
    for (int i = 0; i < 8 && !structures; i++) {
        int tx, tyy;
        place(130 + i * 200, 1520, 4, 2, tx, tyy);
        auto d = bld::make("longhouse", 501, tx, tyy, 4, 2, 0, false);
        bld::draw(d, i < 7 ? stages[i] : 1, i >= 6 ? 1 : 0, i == 7);
    }
    EndMode2D();
    draw::tone = {};
    draw::ink.width = 0;
    for (int t = 0; t < count; t++) {
        int i = t * 3, col = i % 6, row = i / 6;
        ui::text(ui::F.bold, types[t].key, (40 + col * 265) * fit, (structures ? 40 + row * 205 : 50 + row * 185) * fit, 13, ui::T.muted);
    }
    ui::text(ui::F.bold, structures ? "a watchtower going up, and a boat taking shape" : "a longhouse going up: foundation, frame, walls rising, roof, details; then the door open, and lit at night", 30 * fit, (structures ? 1310 : 1375) * fit, 13, ui::T.muted);
}

int Game::run() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(opt.width, opt.height, "Driftwood");
    SetExitKey(KEY_NULL);
    SetWindowMinSize(1100, 680);
    SetTargetFPS(60);
    std::string assets = std::string(GetApplicationDirectory()) + "assets";
    if (!DirectoryExists(assets.c_str())) assets = "assets";
    ui::load(assets);
    if (!opt.saveDir.empty()) saves::setDir(opt.saveDir);
    newGame(opt.seed);
    if (opt.fullscreen) ToggleFullscreen();
    // the title's backdrop: the island a few weeks on, with a village to look at
    if (opt.screenshot.empty() || opt.title) {
        int target = sim.day() + 24;
        while (sim.day() < target || sim.hour() < 10.5) {   // a few weeks on, mid-morning light
            sim.step(30);
            if (!sim.pendingFate.empty()) { auto f = sim.pendingFate[0]; sim.pendingFate.clear(); sim.applyFate(f); }
        }
        screen = Screen::TITLE;
    } else screen = Screen::PLAYING;

    if (!opt.screenshot.empty()) {
        // Headless capture for docs and visual checks.
        int target = sim.day() + opt.days;
        while (sim.day() < target) {
            sim.step(60);
            if (!sim.pendingFate.empty()) { auto f = sim.pendingFate[0]; sim.pendingFate.clear(); sim.applyFate(f); }
        }
        if (opt.placement) {
            // the narrator picks a blueprint from the build menu: the first unlocked one not yet built
            sim.askPlacement = true;
            for (auto* d : sim.blueprints()) {
                if (sim.built(d->key) || !sim.unlocked(d->key)) continue;
                if (sim.blueprint(d->key) >= 0) break;
            }
            if (sim.pendingPlacement) view.centerOnTile(sim.pendingPlacement->sx + 1.f, sim.pendingPlacement->sy + 1.f);
        }
        if (opt.kit) {
            // everything the narrator can build by hand, placed where the settlers would and finished
            sim.askPlacement = true;
            for (auto* p : sim.members()) p->tiers = {90, 90, 90, 90, 10};
            for (auto k : {"orchard", "flowers", "lantern", "bench", "banner", "flowers", "lantern"}) {
                int id = sim.blueprint(k);
                if (id < 0 || !sim.placeSuggested(id)) { sim.cancelBlueprint(); continue; }
                if (auto* b = sim.projectById(id)) { b->done = true; b->progress = float(b->work); if (b->key == "orchard") b->state = "ripe"; if (sim.active == id) sim.active = -1; }
            }
            if (auto* o = sim.built("orchard")) { opt.hasCenter = true; opt.centerX = o->x + 1.5f; opt.centerY = o->y + 1.5f; }
        }
        if (opt.hour >= 0 && !sim.pendingPlacement) {
            double now = sim.hour(), add = opt.hour - now;
            if (add < 0) add += 24;
            sim.step(add * 60);
            sim.pendingFate.clear();
        }
        sim.flash = 0;
        yearsShown = opt.review && !sim.years.empty() ? int(sim.years.size()) - 1 : int(sim.years.size());
        if (!opt.ending.empty()) sim.endWith(opt.ending);
        endingSeen = opt.ending.empty();
        tab = opt.tab;
        if (opt.selectFirst) { auto m = sim.members(); if (!m.empty()) select(m[std::min<int>(opt.selectFirst - 1, int(m.size()) - 1)]->id, true); }
        else if (!sim.pendingPlacement) view.centerOnTile(sim.campX + 1.f, sim.campY + 1.f);
        if (auto* p = sim.person(selected)) for (int i = 0; i < opt.narrate; i++) sim.narrate(*p, "wood");
        if (opt.question) {
            auto m = sim.members();
            if (m.size() > 2) { sim.ask(*m[1], "friend", m[2]->id); openQuestion = sim.questions.back().id; seenQuestion = openQuestion; }
        }
        if (opt.placement && sim.pendingPlacement) {
            // aim at the suggestion, or at a given point (--pick X,Y) to show another spot
            placingOverride = true;
            placingAt = opt.hasPick ? Vector2{opt.pickX, opt.pickY} : Vector2{-1, -1};
        }
        if (opt.hasCenter) view.centerOnTile(opt.centerX, opt.centerY);
        if (opt.zoom > 0) view.cam.zoom = opt.zoom;
        if (opt.showJournal) journalOpen = true;
        view.showAllLinks = opt.allLinks;
        speed = 1;
        if (opt.gallery || opt.people || opt.buildings || opt.structures)
            for (int i = 0; i < 3; i++) {
                BeginDrawing();
                if (opt.buildings || opt.structures) drawBuildingGallery(opt.structures);
                else if (opt.people) drawPeopleGallery();
                else drawGallery();
                EndDrawing();
            }
        else {
            if (opt.menu) { menuOpen = true; picker = Picker::LOAD; }
            for (int i = 0; i < 4; i++) frame(1.f / 60);
        }
        if (opt.bench > 0) {
            // whole frames, sim and drawing, as the player gets them (vsync off so they are not capped)
            ClearWindowState(FLAG_VSYNC_HINT);
            SetTargetFPS(0);
            double t0 = GetTime(), worst = 0;
            for (int i = 0; i < opt.bench; i++) { double f0 = GetTime(); frame(1.f / 60); worst = std::max(worst, GetTime() - f0); }
            std::printf("bench: %d frames, %.2f ms average, %.2f ms worst, zoom %.2f\n", opt.bench, (GetTime() - t0) * 1000 / opt.bench, worst * 1000, view.cam.zoom);
        }
        if (opt.hasPick && !opt.placement) {
            // Report what a click at this screen point would select (for scripted checks).
            Pick p = view.pickAt(sim, {opt.pickX, opt.pickY});
            Info in = describe(sim, p);
            if (p.ok) {
                std::printf("pick: %s | %s", in.kind.c_str(), in.title.c_str());
                for (auto& [k, v] : in.rows) std::printf(" | %s: %s", k.c_str(), v.c_str());
                std::printf("\n");
                if (p.kind != Drawable::PERSON) inspected = p; else select(p.id, false);
                for (int i = 0; i < 2; i++) frame(1.f / 60);   // the capture reads the frame before last
            } else {
                // say where the closest settler was, so a script can aim better
                const dw::Person* near = nullptr;
                Vector2 at{};
                float bd = 1e9f;
                for (auto& q : sim.people) {
                    if (q.dead || q.hidden) continue;
                    Vector2 w = iso::wp(q.x, q.y);
                    w.y -= WorldView::bodyHeight(q);
                    Vector2 sc = GetWorldToScreen2D(w, view.cam);
                    float d = std::hypot(sc.x - opt.pickX, sc.y - opt.pickY);
                    if (d < bd) { bd = d; near = &q; at = sc; }
                }
                if (near) std::printf("pick: none (nearest settler %s at %.0f,%.0f)\n", near->name.c_str(), at.x, at.y);
                else std::printf("pick: none\n");
            }
        }
        // Save with opaque alpha (blending leaves the framebuffer alpha < 1).
        Image img = LoadImageFromScreen();
        ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8);
        ExportImage(img, opt.screenshot.c_str());
        UnloadImage(img);
        ui::unload();
        view.unload();
        CloseWindow();
        return 0;
    }

    while (!WindowShouldClose() && !quit) frame(std::min(GetFrameTime(), .1f));
    ui::unload();
    view.unload();
    CloseWindow();
    return 0;
}
