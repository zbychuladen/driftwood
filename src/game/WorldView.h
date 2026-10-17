// Renders the simulation as an isometric world: terrain, objects, people,
// fog of war, weather and lighting. Owns the camera and map input.
#pragma once
#include <functional>
#include <unordered_map>
#include <vector>
#include "engine/Lighting.h"
#include "game/Art.h"
#include "game/Depth.h"
#include "raylib.h"
#include "sim/Sim.h"

// One thing in the world to draw: what it is, where it stands, and its art.
struct Drawable {
    enum Kind { PERSON, OBJECT, BUILDING, POI, STOCKPILE, BOAT, WOLF } kind;
    int id;                                   // person id, object index, project id or poi index
    depth::Item at;                           // footprint and measured screen box
    std::function<void(const art::Ctx&)> draw;
};

// What a click landed on. ok is false for empty ground or sea.
struct Pick {
    Drawable::Kind kind = Drawable::PERSON;
    int id = -1;
    bool ok = false;
    bool is(Drawable::Kind k, int i) const { return ok && kind == k && id == i; }
};

// Where a settler's focus points on the ground, and the thing it names.
// ok is false when the target is gone (a felled tree, say).
struct FocusTarget { bool ok = false; float x = 0, y = 0; Pick what; };
FocusTarget focusTarget(dw::Sim& sim, const dw::Focus& f);

class WorldView {
public:
    static constexpr float MIN_ZOOM = .35f, MAX_ZOOM = 5.f;
    // From this zoom up the ground is painted live for the visible tiles, so it
    // stays sharp close up; below it the baked image is cheaper and sharp enough.
    static constexpr float LIVE_GROUND_ZOOM = .6f;
    struct TileRange { int x0, y0, x1, y1; };
    // The tiles that can show inside a screen rectangle, with a margin.
    TileRange visibleTiles(Rectangle screen) const;
    void init(dw::Sim& sim);
    // After loading a save: forget everything tracked about the last game.
    void reset(dw::Sim& sim);
    void unload();
    // Camera input. mapRect is the visible map area (excludes the sidebar).
    void handleInput(float dt, Rectangle mapRect, bool pointerFree);
    void draw(dw::Sim& sim, Rectangle mapRect, int selectedId, float realDt, float speed, Pick inspected = {});
    bool showAllLinks = false;                // every settler's link to their work, not just the selected one
    // A building being placed: its footprint under the pointer, and the settlers' suggestion.
    struct Ghost { bool on = false, legal = false; int x = 0, y = 0, w = 1, h = 1, sx = 0, sy = 0; float quality = 1; } ghost;
    // The footprint of a w-by-h building centred on the tile under a screen point.
    void footprintAt(Vector2 screen, int w, int h, int& x, int& y) const;
    // Doors open as settlers go in to sleep or come out, then swing shut.
    void updateDoors(dw::Sim& sim);
    float doorOpen(int buildingId) const;
    static constexpr double DOOR_OPEN_MIN = 15, DOOR_CLOSE_MIN = 10;   // game minutes
    Pick hovered;                             // what is under the pointer this frame, set by the game
    // Name tags appear only for the settler under the pointer, the selected
    // one, and (hovering their hut) whoever sleeps inside.
    static bool showTag(dw::Sim& sim, const dw::Person& p, Pick hovered, int selectedId);
    // Returns a person id under the screen point, or -1.
    int pick(dw::Sim& sim, Vector2 screen);
    // Whatever is drawn front-most under the screen point: its visible shape
    // counts, not a box around it. Settlers also answer to a near miss.
    Pick pickAt(dw::Sim& sim, Vector2 screen) const;
    // Height above the feet (world px) of the point a click on a person aims for.
    static float bodyHeight(const dw::Person& p);
    // Everything standing in the world inside the given world-pixel rectangle
    // (flat ground art excluded), with screen boxes measured from the art.
    std::vector<Drawable> collect(dw::Sim& sim, const art::Ctx& ctx, Rectangle worldView, int selectedId) const;
    // The same list in back-to-front drawing order.
    static std::vector<Drawable> sorted(std::vector<Drawable> items);
    void centerOnTile(float tx, float ty);
    void zoomBy(float f, Vector2 screenPivot);
    bool clickedMap() const { return clicked; }   // left click without drag this frame
    Vector2 clickPos() const { return clickAt; }

    Camera2D cam{};
private:
    void buildGround(dw::Sim& sim);
    void paintGround(dw::Sim& sim, int x0, int y0, int x1, int y1);
    void bakeGround(dw::Sim& sim);
    void buildFog(dw::Sim& sim);
    void drawFog(dw::Sim& sim);
    void drawWeather(dw::Sim& sim, float dt, float speed);
    void drawLabels(dw::Sim& sim, int selectedId);
    // Which way each settler faces: the way they last moved, or toward their work.
    void updateFacings(dw::Sim& sim);
    struct Track { float x, y; int facing; };
    std::unordered_map<int, Track> tracks;
    std::unordered_map<int, bool> wasInside;      // person id: hidden in a building last frame
    std::unordered_map<int, double> lastPass;     // building id: when someone last went through its door
    double lastPassClock = 0;                     // the sim's clock at the last door update
    void drawHighlight(dw::Sim& sim, Pick p);
    void drawGhost();
    void drawLinks(dw::Sim& sim, int selectedId, Pick inspected);
    struct Caption { Vector2 world; std::string text; };
    std::vector<Caption> captions;            // filled by drawLinks, drawn in screen space
    void clampCam();

    RenderTexture2D ground{};
    // ground colors and marks for one season and seed, filed by tile
    struct GroundCache {
        int season = -1;
        uint32_t seed = 0;
        std::vector<Color> col, corner;
        struct Mark { float tx, ty; enum Kind : uint8_t { SNOW, BLADE, FLOWER, LEAF, PEBBLE, SPECK } kind; Color c; float a, b; };
        std::vector<Mark> marks;
        std::vector<int> markStart;   // marks of tile i are [markStart[i], markStart[i + 1])
    } gc;
    int bakedSeason = -1;
    uint32_t bakedSeed = 0;
    std::vector<float> fogCorner;       // (GW+1)*(GH+1) fog density per tile corner
    int fogVersion = -1;
    std::vector<int> waterTiles;
    Lighting lighting;
    struct Drop { float x, y, v; };
    std::vector<Drop> drops;
    bool dragging = false, pressOnMap = false, clicked = false;
    Vector2 pressAt{}, clickAt{}, camAtPress{};
    Rectangle lastMap{};
};
