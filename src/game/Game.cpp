#include "game/Game.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include "game/Iso.h"
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
    ui::block(rTop);
    ui::block(rSide);
    ui::block(rJournal);
    if (inspected.ok) ui::block({16, rTop.height + 16, 340, inspectH});
    bool fate = !sim.pendingFate.empty();
    if (fate) ui::block(rMap);
    view.handleInput(dt, rMap, !ui::mouseOverUi() && !fate);
    if (!sandFocus) {
        if (IsKeyPressed(KEY_SPACE)) speed = speed > 0 ? (prevSpeed = speed, 0.f) : (prevSpeed > 0 ? prevSpeed : 1.f);
        if (IsKeyPressed(KEY_ONE)) speed = 1;
        if (IsKeyPressed(KEY_TWO)) speed = 2;
        if (IsKeyPressed(KEY_THREE)) speed = 5;
        if (IsKeyPressed(KEY_J)) journalOpen = !journalOpen;
        if (IsKeyPressed(KEY_ESCAPE)) inspected = {};
        if (IsKeyPressed(KEY_C)) view.showAllLinks = !view.showAllLinks;
    }
    if (!fate && speed > 0) sim.step(dt * speed * MIN_PER_SEC);
    if (view.clickedMap()) {
        // a settler opens their card in the sidebar; anything else gets the info card
        Pick p = view.pickAt(sim, view.clickPos());
        select(p.ok && p.kind == Drawable::PERSON ? p.id : -1, false);
        inspected = p.ok && p.kind != Drawable::PERSON ? p : Pick{};
    }
    if (inspected.ok && describe(sim, inspected).title.empty()) inspected = {};   // it is gone

    BeginDrawing();
    rlDisableBackfaceCulling();   // 2D shapes come in both windings
    view.draw(sim, rMap, selected, dt, sim.pendingFate.empty() ? speed : 0, inspected);
    drawHud();
    EndDrawing();
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
    newGame(opt.seed);
    if (opt.fullscreen) ToggleFullscreen();

    if (!opt.screenshot.empty()) {
        // Headless capture for docs and visual checks.
        int target = sim.day() + opt.days;
        while (sim.day() < target) {
            sim.step(60);
            if (!sim.pendingFate.empty()) { auto f = sim.pendingFate[0]; sim.pendingFate.clear(); sim.applyFate(f); }
        }
        if (opt.hour >= 0) {
            double now = sim.hour(), add = opt.hour - now;
            if (add < 0) add += 24;
            sim.step(add * 60);
            sim.pendingFate.clear();
        }
        sim.flash = 0;
        tab = opt.tab;
        if (opt.selectFirst) { auto m = sim.members(); if (!m.empty()) select(m[std::min<int>(opt.selectFirst - 1, int(m.size()) - 1)]->id, true); }
        else view.centerOnTile(sim.campX + 1.f, sim.campY + 1.f);
        if (opt.hasCenter) view.centerOnTile(opt.centerX, opt.centerY);
        if (opt.zoom > 0) view.cam.zoom = opt.zoom;
        if (opt.showJournal) journalOpen = true;
        view.showAllLinks = opt.allLinks;
        speed = 1;
        for (int i = 0; i < 4; i++) frame(1.f / 60);
        if (opt.hasPick) {
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

    while (!WindowShouldClose()) frame(std::min(GetFrameTime(), .1f));
    ui::unload();
    view.unload();
    CloseWindow();
    return 0;
}
