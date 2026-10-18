// A small immediate-mode UI toolkit: fonts, wrapped text, buttons,
// scroll regions and a text field. Widgets act on mouse release so that
// dragging the map never triggers a button.
#pragma once
#include <string>
#include <vector>
#include "raylib.h"

namespace ui {

struct Fonts { Font body, bold, head, hand; };
extern Fonts F;

struct Theme {
    Color panel, panel2, line, text, muted, accent, narrator, ember, berry, ok, dark;
    Color onAccent;   // text on an accent fill
    Color field;      // text input background
    Color hover;      // button under the pointer
};
extern Theme T;
// The five needs (body, safety, belonging, esteem, purpose): fills, and darker
// versions for text on the light panels.
extern const Color TIER[5], TIER_INK[5];

bool load(const std::string& assetDir);
void unload();

// Call once per frame before any widget.
void beginFrame();
// True if the mouse is over any registered UI rectangle this frame.
bool mouseOverUi();
void block(Rectangle r);                  // mark a rectangle as UI (blocks map input)

Vector2 measure(const Font& f, const std::string& s, float size);
void text(const Font& f, const std::string& s, float x, float y, float size, Color c);
void textCentered(const Font& f, const std::string& s, Rectangle r, float size, Color c);
// Draws wrapped text and returns its height. With draw=false it only measures.
float wrapped(const Font& f, const std::string& s, float x, float y, float w, float size, Color c, bool draw = true, float lineH = 1.28f);

bool hovered(Rectangle r);
bool clicked(Rectangle r);                // released inside r this frame
bool button(Rectangle r, const std::string& label, bool enabled = true, bool active = false, float size = 15);
// A note about what something does, shown near the pointer while it rests on r.
// drawTip() draws it, last thing in the frame.
void tip(Rectangle r, const std::string& text);
void drawTip();
void panel(Rectangle r, Color bg, Color border, float round = .04f);
void bar(Rectangle r, float v01, Color fill, Color back);

// Scroll regions clip their contents and keep their own offset.
struct Scroll { float y = 0, content = 0; };
void beginScroll(Rectangle r, Scroll& s);
void endScroll(Rectangle r, Scroll& s, float contentHeight);

// Single-line text input. Returns true when Enter is pressed.
bool textField(Rectangle r, std::string& buf, bool& focus, int maxLen, const std::string& placeholder);

} // namespace ui
