// Ties the simulation, world view and HUD together and runs the frame loop.
#pragma once
#include <string>
#include <vector>
#include "engine/Ui.h"
#include "game/Inspect.h"
#include "game/WorldView.h"
#include "sim/Sim.h"

struct Options {
    uint32_t seed = 20261017;
    int width = 1600, height = 900;
    // headless capture: fast-forward and save a screenshot, then exit
    std::string screenshot;
    int days = 0;
    float hour = -1;
    float zoom = 0;
    int selectFirst = 0;
    int tab = 0;
    bool fullscreen = false;
    bool showJournal = false;
    bool allLinks = false;
    bool gallery = false;
    bool people = false;
    bool buildings = false;           // capture a sheet of assembled buildings and their construction              // capture a sheet of settler figures, poses and facings
    int bench = 0;                    // capture: time this many frames and print the average             // capture a sheet of every tree and plant instead of the world            // draw every settler's link to their work
    bool hasCenter = false, hasPick = false;
    float centerX = 0, centerY = 0;   // tile to center the camera on
    float pickX = 0, pickY = 0;       // screen point to click, result printed to stdout
};

class Game {
public:
    explicit Game(const Options& o) : opt(o) {}
    int run();

private:
    void newGame(uint32_t seed);
    void frame(float dt);
    void layout();
    void drawHud();
    void drawTopBar();
    void drawSidebar();
    void drawSettlers(Rectangle area);
    void drawNarrator(Rectangle area);
    void drawCamp(Rectangle area);
    void drawJournal();
    void drawFate();
    void drawInspect();
    void drawGallery();
    void drawPeopleGallery();
    void drawBuildingGallery();
    float settlerCard(dw::Person& p, float x, float y, float w, bool drawIt);
    void select(int id, bool center);

    Options opt;
    dw::Sim sim;
    WorldView view;
    int selected = -1;
    Pick inspected;             // a non-settler the info card describes
    float inspectH = 0;         // the card's height last frame, so it can block map clicks
    float speed = 1, prevSpeed = 1;
    int tab = 0;
    bool journalOpen = false, sandFocus = false;   // collapsed to a one-line strip so the map stays clear
    std::string sandText, sandNote;
    ui::Scroll sideScroll, journalScroll;
    Rectangle rTop{}, rSide{}, rJournal{}, rMap{};
    std::vector<float> journalHeights;
    int journalVersionCached = -1;
    float journalWidthCached = 0;
};
