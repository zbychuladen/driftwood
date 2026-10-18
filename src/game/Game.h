// Ties the simulation, world view and HUD together and runs the frame loop.
#pragma once
#include <string>
#include <vector>
#include "engine/Ui.h"
#include "game/Inspect.h"
#include "game/Legacy.h"
#include "game/LedgerBoard.h"
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
    int narrate = 0;
    bool question = false;           // a settler asks the narrator something, shown open
    bool kit = false;                // one of each thing the narrator can build by hand, finished, near camp
    bool review = false;             // the last year in review card
    std::string ending;              // end the run this way and show the ending screen                 // narrate the selected settler this many times before the capture
    int tab = 0;
    bool fullscreen = false;
    bool showJournal = false;
    bool allLinks = false;
    bool gallery = false;
    bool people = false;
    bool buildings = false;
    bool structures = false;          // capture a sheet of the assembled structures (well, tower, dock...)
    bool title = false;               // capture the title screen instead of the game
    bool menu = false;                // capture the game paused with the save picker open
    bool placement = false;           // capture: run until a building is waiting to be placed, show the placement screen
    std::string saveDir;              // where saves go (default: the platform's place for app data)           // capture a sheet of assembled buildings and their construction              // capture a sheet of settler figures, poses and facings
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
    void drawBuildingGallery(bool structures);
    // title, pause menu and saves (Menus.cpp)
    void drawTitle();
    void drawMenu();
    void drawPicker();
    void drawToast();
    void say(const std::string& text);
    void saveTo(int slot);
    void loadFrom(int slot);
    void drawPlacement();
    void drawDecisions();
    void drawYearReview();
    void drawEnding();
    float drawLedger(float x, float y, float w);
    void drawBuild(Rectangle area);
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
    // which screen is up
    enum class Screen { TITLE, PLAYING } screen = Screen::TITLE;
    enum class Picker { NONE, SAVE, LOAD } picker = Picker::NONE;
    bool menuOpen = false, quit = false;
    int confirmSlot = -1;                 // a used slot clicked once in the save picker
    std::string toast;
    double toastUntil = 0;
    float titleClock = 0;                 // the title backdrop's camera drift
    bool settlersPlace = false;           // the settlers choose every building's spot themselves
    bool pauseQuestions = false;          // pause the game when a settler asks the narrator something
    int openQuestion = -1, seenQuestion = 0, openPetition = -1;
    int yearsShown = 0;                   // year reviews the player has seen
    bool endingSeen = false;              // the ending screen has been closed (keep playing)
    legacy::Profile profile;              // endings reached across runs
    int islandChoice = 0, startChoice = 0;   // what the next new game starts with
    bool endingRecorded = false;          // this run's ending is in the legacy
    ui::Scroll storyScroll;
    std::string ledgerNote;               // what came of the last nudge from the Ledger
    LedgerBoard ledger;                   // the Ledger, steadied
    int narratorView = 0;                 // Ledger, Narrate or Village
    std::string openGap;                  // the Ledger gap unfolded
    int renaming = -1;                    // the building whose name is being typed
    std::string renameText;
    bool renameFocus = false;
    Rectangle decisionsR{};               // the pending-decisions list, as last drawn
    float placeH = 0;                     // the placement panel's height last frame, to keep clicks off the map
    bool placingOverride = false;         // captures: aim the footprint at a fixed screen point
    Vector2 placingAt{};
};
