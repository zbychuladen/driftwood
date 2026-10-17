#include "engine/Ui.h"
#include <algorithm>
#include <cmath>

namespace ui {

Fonts F;
// Light, paper-like panels with deep green ink and a sage accent, matching the
// illustrated world. tests/game_tests.cpp checks the text pairs for contrast.
Theme T = {
    {250, 249, 244, 250},// panel
    {240, 239, 231, 255},// panel2
    {218, 215, 202, 255},// line
    {34, 48, 37, 255},   // text
    {92, 102, 90, 255},  // muted
    {74, 102, 64, 255},  // accent
    {112, 78, 168, 255}, // voice
    {160, 92, 26, 255},  // ember
    {166, 52, 64, 255},  // berry
    {58, 112, 52, 255},  // ok
    {30, 40, 32, 255},   // dark
    {255, 255, 255, 255},// onAccent
    {255, 255, 252, 255},// field
    {231, 234, 222, 255},// hover
};
const Color TIER[5] = {{233, 162, 75, 255}, {98, 184, 162, 255}, {232, 138, 162, 255}, {127, 166, 230, 255}, {180, 142, 232, 255}};
const Color TIER_INK[5] = {{150, 88, 18, 255}, {28, 112, 92, 255}, {168, 56, 92, 255}, {48, 90, 168, 255}, {112, 70, 170, 255}};

static std::vector<Rectangle> blockers, blockersPrev;
static std::vector<Rectangle> clipStack;

static Font loadFont(const std::string& path, int size) {
    // Latin-1 plus a few typographic marks.
    std::vector<int> cps;
    for (int c = 32; c < 256; c++) cps.push_back(c);
    for (int c : {0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2026, 0x2022, 0x2726}) cps.push_back(c);
    Font f = LoadFontEx(path.c_str(), size, cps.data(), int(cps.size()));
    if (f.texture.id == 0) return GetFontDefault();
    GenTextureMipmaps(&f.texture);
    SetTextureFilter(f.texture, TEXTURE_FILTER_TRILINEAR);
    return f;
}

bool load(const std::string& dir) {
    F.body = loadFont(dir + "/fonts/Lato-Regular.ttf", 48);
    F.bold = loadFont(dir + "/fonts/Lato-Bold.ttf", 48);
    F.head = loadFont(dir + "/fonts/YoungSerif-Regular.ttf", 64);
    F.hand = loadFont(dir + "/fonts/Caveat-Variable.ttf", 56);
    return F.body.texture.id != GetFontDefault().texture.id;
}

void unload() {
    for (Font* f : {&F.body, &F.bold, &F.head, &F.hand})
        if (f->texture.id != GetFontDefault().texture.id) UnloadFont(*f);
}

void beginFrame() { blockersPrev = blockers; blockers.clear(); }
void block(Rectangle r) { blockers.push_back(r); }
bool mouseOverUi() {
    Vector2 m = GetMousePosition();
    for (auto& r : blockersPrev) if (CheckCollisionPointRec(m, r)) return true;
    for (auto& r : blockers) if (CheckCollisionPointRec(m, r)) return true;
    return false;
}

Vector2 measure(const Font& f, const std::string& s, float size) { return MeasureTextEx(f, s.c_str(), size, 0); }
void text(const Font& f, const std::string& s, float x, float y, float size, Color c) { DrawTextEx(f, s.c_str(), {std::round(x), std::round(y)}, size, 0, c); }
void textCentered(const Font& f, const std::string& s, Rectangle r, float size, Color c) {
    Vector2 m = measure(f, s, size);
    text(f, s, r.x + (r.width - m.x) / 2, r.y + (r.height - m.y) / 2, size, c);
}

float wrapped(const Font& f, const std::string& s, float x, float y, float w, float size, Color c, bool draw, float lineH) {
    float lh = size * lineH, cy = y, spaceW = measure(f, " ", size).x;
    size_t i = 0;
    std::string line;
    float lineW = 0;
    auto flush = [&] { if (draw && !line.empty()) text(f, line, x, cy, size, c); cy += lh; line.clear(); lineW = 0; };
    while (i < s.size()) {
        if (s[i] == '\n') { flush(); i++; continue; }
        size_t j = s.find_first_of(" \n", i);
        if (j == std::string::npos) j = s.size();
        std::string word = s.substr(i, j - i);
        float ww = measure(f, word, size).x;
        if (!line.empty() && lineW + spaceW + ww > w) flush();
        if (!line.empty()) { line += ' '; lineW += spaceW; }
        line += word; lineW += ww;
        i = j < s.size() && s[j] == ' ' ? j + 1 : j;
    }
    if (!line.empty()) flush();
    return cy - y;
}

static bool insideClip(Vector2 m) {
    for (auto& c : clipStack) if (!CheckCollisionPointRec(m, c)) return false;
    return true;
}
bool hovered(Rectangle r) { Vector2 m = GetMousePosition(); return CheckCollisionPointRec(m, r) && insideClip(m); }
bool clicked(Rectangle r) { return IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && hovered(r); }

bool button(Rectangle r, const std::string& label, bool enabled, bool active, float size) {
    bool h = enabled && hovered(r);
    Color bg = active ? T.accent : h ? T.hover : T.panel2;
    Color fg = active ? T.onAccent : enabled ? T.text : T.muted;
    DrawRectangleRounded(r, .35f, 8, bg);
    if (!active) DrawRectangleRoundedLinesEx(r, .35f, 8, 1, T.line);
    textCentered(F.bold, label, r, size, fg);
    return enabled && clicked(r);
}

void panel(Rectangle r, Color bg, Color border, float round) {
    DrawRectangleRounded(r, round, 8, bg);
    DrawRectangleRoundedLinesEx(r, round, 8, 1, border);
}

void bar(Rectangle r, float v, Color fill, Color back) {
    DrawRectangleRounded(r, .5f, 4, back);
    if (v > 0) DrawRectangleRounded({r.x, r.y, std::max(r.height, r.width * std::clamp(v, 0.f, 1.f)), r.height}, .5f, 4, fill);
}

void beginScroll(Rectangle r, Scroll& s) {
    if (hovered(r)) s.y -= GetMouseWheelMove() * 48;
    s.y = std::clamp(s.y, 0.f, std::max(0.f, s.content - r.height));
    clipStack.push_back(r);
    BeginScissorMode(int(r.x), int(r.y), int(r.width), int(r.height));
}

void endScroll(Rectangle r, Scroll& s, float h) {
    EndScissorMode();
    clipStack.pop_back();
    s.content = h;
    if (h > r.height) {
        float th = std::max(24.f, r.height * r.height / h), ty = r.y + (r.height - th) * (s.y / (h - r.height));
        DrawRectangleRounded({r.x + r.width - 4, ty, 3, th}, 1, 4, Color{160, 166, 152, 200});
    }
    if (!clipStack.empty()) { auto& c = clipStack.back(); BeginScissorMode(int(c.x), int(c.y), int(c.width), int(c.height)); }
}

bool textField(Rectangle r, std::string& buf, bool& focus, int maxLen, const std::string& ph) {
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) focus = hovered(r);
    DrawRectangleRounded(r, .25f, 6, T.field);
    DrawRectangleRoundedLinesEx(r, .25f, 6, 1, focus ? T.voice : T.line);
    bool enter = false;
    if (focus) {
        for (int c = GetCharPressed(); c > 0; c = GetCharPressed())
            if (c >= 32 && c < 127 && int(buf.size()) < maxLen) buf += char(c);
        if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && !buf.empty()) buf.pop_back();
        if (IsKeyPressed(KEY_ENTER)) enter = true;
        if (IsKeyPressed(KEY_ESCAPE)) focus = false;
    }
    float size = 22;
    std::string shown = buf.empty() && !focus ? ph : buf;
    Color c = buf.empty() && !focus ? T.muted : T.text;
    // keep the end of long text visible
    while (!shown.empty() && measure(F.hand, shown, size).x > r.width - 20) shown.erase(0, 1);
    text(F.hand, shown, r.x + 10, r.y + (r.height - size) / 2 - 1, size, c);
    if (focus && int(GetTime() * 2) % 2 == 0) {
        float cx = r.x + 10 + measure(F.hand, buf.empty() ? "" : shown, size).x + 1;
        DrawRectangle(int(cx), int(r.y + 8), 2, int(r.height - 16), T.voice);
    }
    return enter;
}

} // namespace ui
