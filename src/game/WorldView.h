// Renders the simulation as an isometric world: terrain, objects, people,
// fog of war, weather and lighting. Owns the camera and map input.
#pragma once
#include <functional>
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

class WorldView {
public:
    void init(dw::Sim& sim);
    void unload();
    // Camera input. mapRect is the visible map area (excludes the sidebar).
    void handleInput(float dt, Rectangle mapRect, bool pointerFree);
    void draw(dw::Sim& sim, Rectangle mapRect, int selectedId, float realDt, float speed, Pick inspected = {});
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
    void renderGround(dw::Sim& sim);
    void buildFog(dw::Sim& sim);
    void drawFog(dw::Sim& sim);
    void drawWeather(dw::Sim& sim, float dt, float speed);
    void drawLabels(dw::Sim& sim, int selectedId);
    void drawHighlight(dw::Sim& sim, Pick p);
    void clampCam();

    RenderTexture2D ground{};
    int groundSeason = -1;
    uint32_t groundSeed = 0;
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
