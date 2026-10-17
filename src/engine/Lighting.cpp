#include "engine/Lighting.h"
#include "engine/Draw.h"
#include "rlgl.h"

void Lighting::resize(int nw, int nh) {
    if (nw == w && nh == h && rt.id) return;
    if (rt.id) UnloadRenderTexture(rt);
    w = nw; h = nh;
    rt = LoadRenderTexture(w, h);
}

void Lighting::unload() { if (rt.id) UnloadRenderTexture(rt); rt = {}; }

void Lighting::render(float darkness, Color tint, const std::vector<Light>& lights) {
    if (darkness <= 0.001f) return;
    BeginTextureMode(rt);
    rlDisableBackfaceCulling();
    ClearBackground(Color{tint.r, tint.g, tint.b, (unsigned char)(darkness * 255)});
    // erase: dst = dst * (1 - srcAlpha)
    rlSetBlendFactors(0 /*GL_ZERO*/, 0x0303 /*GL_ONE_MINUS_SRC_ALPHA*/, 0x8006 /*GL_FUNC_ADD*/);
    BeginBlendMode(BLEND_CUSTOM);
    for (auto& l : lights) {
        draw::radial(l.pos.x, l.pos.y, l.radius, l.radius, Color{255, 255, 255, 255}, Color{255, 255, 255, 0}, 40);
        draw::radial(l.pos.x, l.pos.y, l.radius * .55f, l.radius * .55f, Color{255, 255, 255, 140}, Color{255, 255, 255, 0}, 32);
    }
    EndBlendMode();
    EndTextureMode();
    DrawTextureRec(rt.texture, {0, 0, float(w), float(-h)}, {0, 0}, WHITE);
    BeginBlendMode(BLEND_ADDITIVE);
    for (auto& l : lights) {
        Color g = l.glow;
        g.a = (unsigned char)(g.a * darkness / .62f);
        draw::radial(l.pos.x, l.pos.y, l.radius * .8f, l.radius * .8f, g, Color{g.r, g.g, g.b, 0}, 36);
    }
    EndBlendMode();
}
