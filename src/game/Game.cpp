#include "game/Game.h"
#include <algorithm>
#include <cmath>
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
    bool fate = !sim.pendingFate.empty();
    if (fate) ui::block(rMap);
    view.handleInput(dt, rMap, !ui::mouseOverUi() && !fate);
    if (!sandFocus) {
        if (IsKeyPressed(KEY_SPACE)) speed = speed > 0 ? (prevSpeed = speed, 0.f) : (prevSpeed > 0 ? prevSpeed : 1.f);
        if (IsKeyPressed(KEY_ONE)) speed = 1;
        if (IsKeyPressed(KEY_TWO)) speed = 2;
        if (IsKeyPressed(KEY_THREE)) speed = 5;
        if (IsKeyPressed(KEY_J)) journalOpen = !journalOpen;
    }
    if (!fate && speed > 0) sim.step(dt * speed * MIN_PER_SEC);
    if (view.clickedMap()) select(view.pick(sim, view.clickPos()), false);

    BeginDrawing();
    rlDisableBackfaceCulling();   // 2D shapes come in both windings
    view.draw(sim, rMap, selected, dt, sim.pendingFate.empty() ? speed : 0);
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
        if (opt.zoom > 0) view.cam.zoom = opt.zoom;
        speed = 1;
        for (int i = 0; i < 4; i++) frame(1.f / 60);
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
