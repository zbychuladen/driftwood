// All screen UI: top bar, sidebar tabs (settlers, narrator, camp), journal, the
// pending decisions and fate cards.
#include <algorithm>
#include <cmath>
#include <functional>
#include "engine/Draw.h"
#include "game/Game.h"

using namespace dw;
using namespace ui;

namespace {
Color C(uint32_t rgb) { return draw::hex(rgb); }

const char* moodWord(float m) { return m > 78 ? "content" : m > 60 ? "okay" : m > 42 ? "uneasy" : m > 25 ? "miserable" : "desperate"; }

std::string truncate(const Font& f, std::string s, float size, float w) {
    if (measure(f, s, size).x <= w) return s;
    while (!s.empty() && measure(f, s + "...", size).x > w) s.pop_back();
    return s + "...";
}

void miniPyramid(const Person& p, float x, float y, float W) {
    float H = W * .86f, lh = H / 5;
    for (int i = 0; i < 5; i++) {
        float yb = y + H - i * lh, yt = yb - lh + 1, wb = W * (1 - i / 5.f), wt = W * (1 - (i + 1) / 5.f);
        float xb = x + (W - wb) / 2, xt = x + (W - wt) / 2;
        float op = p.gates[i] < .3f ? .14f : .22f + p.tiers[i] / 100 * .78f;
        draw::poly({{xb, yb}, {xb + wb, yb}, {xt + wt, yt}, {xt, yt}}, draw::withA(TIER[i], op));
    }
}

// Five stacked bars, narrowest on top, like the pyramid.
float pyramidRows(const std::array<float, 5>& t, const std::array<float, 5>& g, float x, float y, float w, bool drawIt) {
    float rowH = 22, gap = 3;
    if (drawIt)
        for (int r = 0; r < 5; r++) {
            int i = 4 - r;
            float rw = w * (.44f + i * .14f), rx = x + (w - rw) / 2, ry = y + r * (rowH + gap);
            bool locked = g[i] < .3f;
            DrawRectangleRounded({rx, ry, rw, rowH}, .3f, 6, T.panel2);
            if (!locked) DrawRectangleRounded({rx, ry, std::max(rowH * .6f, rw * t[i] / 100), rowH}, .3f, 6, draw::withA(TIER[i], .85f));
            else DrawRectangleRoundedLinesEx({rx, ry, rw, rowH}, .3f, 6, 1, draw::withA(TIER[i], .35f));
            float fillW = locked ? 0 : std::max(rowH * .6f, rw * t[i] / 100);
            text(F.bold, TIER_NAMES[i], rx + 8, ry + 4, 13, locked || fillW < 70 ? T.text : T.dark);
            std::string v = locked ? "locked" : std::to_string(int(t[i]));
            float vx = rx + rw - 8 - measure(F.bold, v, 13).x;
            text(F.bold, v, vx, ry + 4, 13, locked || rx + fillW < vx + 4 ? T.muted : T.dark);
        }
    return 5 * (rowH + gap) + 4;
}

float sectionTitle(const std::string& s, float x, float y) { text(F.head, s, x, y, 20, T.text); return 30; }
} // namespace

void Game::drawHud() {
    drawTopBar();
    drawSidebar();
    drawJournal();
    drawInspect();
    if (!sim.pendingPlacement) drawDecisions();   // the placement card says the same while a spot is being chosen
    else decisionsR.height = 0;
    if (!sim.pendingFate.empty()) drawFate();
}

// The info card for a clicked building, tree, rock or place, top left of the map.
void Game::drawInspect() {
    if (!inspected.ok) { inspectH = 0; return; }
    Info in = describe(sim, inspected);
    float x = 16, y = rTop.height + 16, w = 340, pad = 16, tw = w - pad * 2;
    // measure first, then draw at that height
    float h = pad + 16 + wrapped(F.head, in.title, 0, 0, tw - 30, 22, WHITE, false, 1.1f) + 4;
    if (!in.desc.empty()) h += wrapped(F.body, in.desc, 0, 0, tw, 14.5f, WHITE, false) + 10;
    for (auto& [k, v] : in.rows) h += std::max(20.f, wrapped(F.body, v, 0, 0, tw - 104, 14, WHITE, false) + 6);
    // a standing building can be named, and given another look
    Project* bp = inspected.kind == Drawable::BUILDING ? sim.projectById(inspected.id) : nullptr;
    bool canName = bp && bp->done, canRestyle = canName && bld::assembled(bp->key);
    if (canName) h += renaming == bp->id ? 44 : 38;
    h += pad - 4;
    inspectH = h;
    Rectangle r{x, y, w, h};
    DrawRectangleRounded(r, .05f, 8, draw::withA(T.panel, .97f));
    DrawRectangleRoundedLinesEx(r, .05f, 8, 1, T.line);
    text(F.bold, in.kind, x + pad, y + pad, 12, T.accent);
    if (button({x + w - 40, y + 10, 28, 26}, "x", true, false, 14)) inspected = {};
    float yy = y + pad + 16;
    yy += wrapped(F.head, in.title, x + pad, yy, tw - 30, 22, T.text, true, 1.1f) + 4;
    if (!in.desc.empty()) yy += wrapped(F.body, in.desc, x + pad, yy, tw, 14.5f, T.muted) + 10;
    for (auto& [k, v] : in.rows) {
        text(F.bold, k, x + pad, yy, 13, T.muted);
        float vh = wrapped(F.body, v, x + pad + 104, yy - 1, tw - 104, 14, T.text);
        yy += std::max(20.f, vh + 6);
    }
    if (canName) {
        yy += 4;
        if (renaming == bp->id) {
            bool enter = textField({x + pad, yy, tw - 84, 34}, renameText, renameFocus, 24, "A name for it...");
            if (button({x + pad + tw - 76, yy + 2, 76, 30}, "Name it", true, true, 13) || enter) { sim.rename(bp->id, renameText); renaming = -1; renameFocus = false; }
        } else {
            if (button({x + pad, yy, 100, 30}, bp->customName.empty() ? "Name it" : "Rename", true, false, 13)) { renaming = bp->id; renameText = bp->customName; renameFocus = true; }
            if (canRestyle && button({x + pad + 108, yy, 120, 30}, "Another look", true, false, 13)) sim.restyle(bp->id);
        }
    }
}

void Game::drawTopBar() {
    DrawRectangleRec(rTop, T.panel);
    DrawLine(0, int(rTop.height), int(rTop.width), int(rTop.height), T.line);
    std::string title = sim.villageName.empty() ? "Driftwood Isle" : sim.villageName;
    text(F.head, title, 18, 12, 30, T.text);
    float x = 18 + measure(F.head, title, 30).x + 36;
    auto chip = [&](const std::string& label, const std::string& value) {
        text(F.bold, label, x, 9, 11, T.muted);
        text(F.bold, value, x, 24, 19, T.text);
        x += std::max(measure(F.bold, label, 11).x, measure(F.bold, value, 19).x) + 30;
    };
    int explored = 0;
    for (auto e : sim.explored) explored += e;
    std::string wx = sim.weather == "clear" ? "Clear" : sim.weather == "rain" ? "Rain" : sim.weather == "storm" ? "Storm" : "Snow";
    chip(cat(SEASONS[sim.season()], ", year ", sim.yearN()), cat("Day ", sim.day()));
    chip("TIME", sim.clock());
    chip("WEATHER", cat(wx, " ", int(std::lround(sim.temp())), "\xC2\xB0""C"));
    chip("ISLAND KNOWN", cat(explored * 100 / (GW * GH), "%"));
    chip("PEOPLE", cat(int(sim.members().size())));
    // the narrator's rapport: what requests are paid from
    {
        float mw = 150;
        text(F.bold, "RAPPORT", x, 9, 11, T.muted);
        std::string inc = cat("+", std::lround(sim.rapportIncome() * 10) / 10.0, " a day");
        text(F.body, inc, x + mw - measure(F.body, inc, 11).x, 9, 11, T.muted);
        bar({x, 30, mw - 40, 10}, sim.rapport / Sim::RAPPORT_MAX, T.narrator, T.panel2);
        text(F.bold, cat(int(sim.rapport)), x + mw - 32, 24, 19, T.narrator);
        x += mw + 30;
    }
    float bx = rTop.width - 16;
    auto btn = [&](const char* l, float w, bool active) { bx -= w; bool r = button({bx, 14, w, 30}, l, true, active, 14); bx -= 6; return r; };
    if (btn("New island", 104, false)) newGame(uint32_t(GetRandomValue(1, 1 << 30)));
    if (btn("Camp", 64, false)) view.centerOnTile(sim.campX + .5f, sim.campY + .5f);
    bx -= 12;
    if (btn("Fast", 58, speed == 5)) speed = 5;
    if (btn("Normal", 74, speed == 2)) speed = 2;
    if (btn("Slow", 58, speed == 1)) speed = 1;
    if (btn("Pause", 66, speed == 0)) speed = 0;
}

void Game::drawSidebar() {
    DrawRectangleRec(rSide, T.panel);
    DrawLine(int(rSide.x), int(rSide.y), int(rSide.x), int(rSide.y + rSide.height), T.line);
    const char* tabs[4] = {"Settlers", "Narrator", "Build", "Camp"};
    float tw = (rSide.width - 32) / 4;
    for (int i = 0; i < 4; i++)
        if (button({rSide.x + 16 + i * tw, rSide.y + 12, tw - 6, 32}, tabs[i], true, tab == i, 14)) { tab = i; sideScroll.y = 0; }
    Rectangle area{rSide.x + 12, rSide.y + 56, rSide.width - 20, rSide.height - 64};
    if (tab == 0) drawSettlers(area);
    else if (tab == 1) drawNarrator(area);
    else if (tab == 2) drawBuild(area);
    else drawCamp(area);
}

float Game::settlerCard(Person& p, float x, float y, float w, bool d) {
    bool sel = p.id == selected;
    float pad = 10, cy = y + pad, inner = w - 2 * pad;
    if (p.status != "member") {
        if (d) {
            Rectangle r{x, y, w, 52};
            if (hovered(r)) DrawRectangleRounded(r, .12f, 6, T.panel2);
            draw::circle(x + 18, y + 18, 5, C(p.tunic));
            text(F.bold, p.name, x + 30, y + 9, 16, T.text);
            std::string st = p.status == "hermit" ? "hermit" : p.status == "arriving" ? "stranger" : "leaving";
            text(F.body, st, x + w - pad - measure(F.body, st, 13).x, y + 11, 13, T.muted);
            text(F.body, p.status == "hermit" ? cat("Trust ", std::min(100, int(p.trust)), "/100") : p.doing, x + 30, y + 30, 13, T.muted);
            if (clicked(r)) select(sel ? -1 : p.id, !sel);
        }
        return 52;
    }
    float h = 0;
    // header row
    if (d) {
        miniPyramid(p, x + pad, cy + 2, 30);
        draw::circle(x + pad + 44, cy + 9, 4.5f, C(p.tunic));
        std::string nm = p.name + (p.child ? "  (child)" : "");
        text(F.bold, nm, x + pad + 54, cy, 17, T.text);
        std::string mood = p.inspired ? "inspired" : moodWord(p.mood);
        text(F.body, mood, x + w - pad - measure(F.body, mood, 13).x, cy + 3, 13, p.inspired ? T.narrator : T.muted);
        std::string doing = std::string(p.sick > 0 ? "Sick - " : "") + (p.told && p.told->until > sim.tMin ? "* " : "") + p.doing;
        text(F.body, truncate(F.body, doing, 13.5f, inner - 44), x + pad + 44, cy + 21, 13.5f, T.muted);
        float sx = x + pad + 44, sy = cy + 39;
        std::string gifts;
        for (auto& [k, v] : p.gifts) gifts += (gifts.empty() ? "" : " / ") + giftInfo().at(k).n;
        if (!gifts.empty()) { text(F.bold, gifts, sx, sy, 12.5f, T.text); sx += measure(F.bold, gifts, 12.5f).x + 8; }
        if (p.seeking >= 0) { std::string s = cat("seeking ", lower(TIER_NAMES[p.seeking])); text(F.bold, s, sx, sy, 12.5f, TIER_INK[p.seeking]); sx += measure(F.bold, s, 12.5f).x + 8; }
        else { text(F.bold, "fulfilled", sx, sy, 12.5f, TIER_INK[4]); sx += measure(F.bold, "fulfilled", 12.5f).x + 8; }
        if (!p.calling.empty()) text(F.body, callingInfo().at(p.calling).n, sx, sy, 12.5f, T.muted);
    }
    h = pad + 58;
    if (sel) {
        float dy = y + h;
        dy += pyramidRows(p.tiers, p.gates, x + pad, dy, inner, d);
        for (auto& l : sim.lacking(p)) dy += wrapped(F.body, "- " + l, x + pad, dy, inner, 13.5f, T.muted, d);
        std::string traits;
        for (size_t i = 0; i < p.traits.size(); i++) traits += (i ? " and " : "") + traitNames().at(p.traits[i]);
        std::string best;
        std::vector<std::pair<float, std::string>> sk;
        for (auto& [k, v] : p.sk) sk.push_back({v, k});
        std::sort(sk.rbegin(), sk.rend());
        for (int i = 0; i < 2 && i < int(sk.size()); i++) best += (i ? ", " : "") + skillNames().at(sk[i].second) + " " + std::to_string(int(sk[i].first * 10) / 10) + "." + std::to_string(int(sk[i].first * 10) % 10);
        Person* partner = sim.person(p.partner);
        Person* friendP = nullptr;
        for (auto* q : sim.members()) if (q != &p && (!friendP || affOf(p, q->id) > affOf(p, friendP->id))) friendP = q;
        std::string desc = cat(p.name, " is ", traits, p.calling.empty() ? "" : ", a " + lower(callingInfo().at(p.calling).n) + " at heart", ". Best at ", best, ".");
        if (partner) desc += " Partner: " + partner->name + ".";
        else if (friendP) desc += " Closest to " + friendP->name + ".";
        desc += cat("\nGifts: ", p.gifts.empty() ? "none, a jack of all trades" : sim.giftText(p), ". Charisma ", p.charisma, "/10 (", Sim::charWord(p.charisma), "). Buys into the narrator ", Sim::buyInWord(p.buyIn), ".");
        if (auto* hh = sim.householdOf(p.id)) desc += cat("\nHousehold: ", sim.householdName(*hh), ", ", Sim::statusName(sim.status(*hh)), "s", hh->home >= 0 && sim.projectById(hh->home) ? cat(", in a ", lower(sim.projectById(hh->home)->name)) : std::string(", no home of their own"), ".");
        if (p.resentment >= 20) desc += p.resentment >= 50 ? "\nBitter about how much better others live." : "\nA little resentful of those doing better.";
        if (p.duty) desc += p.duty->role ? cat("\nThe camp's ", Sim::roleName(p.duty->job), ", by the narrator's choosing.") : cat("\nSpending the season on ", Sim::roleName(p.duty->job), "'s work.");
        if (sim.contrary(p)) desc += "\nContrary: narrated once too often, and doing the opposite for now.";
        else if (p.weary) desc += "\nTired of being narrated.";
        if (sim.agenda && sim.agenda->by == p.id) desc += "\nCurrently setting the camp's agenda.";
        dy += 6;
        dy += wrapped(F.body, desc, x + pad, dy, inner, 13.5f, T.text, d);
        h = dy - y + 8;
    }
    if (d) {
        Rectangle r{x, y, w, h};
        if (sel) DrawRectangleRoundedLinesEx(r, .06f, 6, 1.5f, T.accent);
        else if (hovered(r)) DrawRectangleRoundedLinesEx(r, .12f, 6, 1, T.line);
        if (clicked(r)) select(sel ? -1 : p.id, !sel);
    }
    return h;
}

void Game::drawSettlers(Rectangle a) {
    beginScroll(a, sideScroll);
    float y = a.y - sideScroll.y, x = a.x, w = a.width - 8;
    auto m = sim.members();
    y += sectionTitle(cat("The Settlers"), x, y);
    text(F.body, cat(int(m.size()), " souls"), x + 140, y - 25, 13, T.muted);
    for (auto* p : m) { y += settlerCard(*p, x, y, w, true) + 4; }
    for (auto& p : sim.people) if (!p.dead && p.status != "member") y += settlerCard(p, x, y, w, true) + 4;
    endScroll(a, sideScroll, y + sideScroll.y - a.y + 20);
}

void Game::drawNarrator(Rectangle a) {
    beginScroll(a, sideScroll);
    float x = a.x, w = a.width - 10, y = a.y - sideScroll.y;
    y = drawLedger(x, y, w);
    // how much they buy in, on average: a feel, not a number (the rapport to spend is in the top bar)
    y += sectionTitle("How much they buy in", x, y);
    float f = sim.avgBuyIn();
    bar({x, y + 6, w, 10}, f / 100, T.narrator, T.panel2);
    y += 26;
    y += wrapped(F.body, f > 75 ? "They chat with the narrator like an old friend and mostly go along with the story." : f > 50 ? "They take the narration in their stride and usually play along." : f > 25 ? "They put up with being narrated, some days more than others." : "They are tired of being narrated. Lines rarely land.", x, y, w, 14, T.muted) + 18;
    // narrate one settler
    y += sectionTitle("Narrate", x, y);
    Person* p = sim.person(selected);
    if (!p || p->status != "member" || p->child) {
        y += wrapped(F.body, "Select an adult settler on the map or in the Settlers tab, then narrate what they do next. Lines land with those who buy in.", x, y, w, 14, T.muted) + 16;
    } else {
        y += wrapped(F.body, cat(p->name, " buys in ", Sim::buyInWord(p->buyIn), ". ", sim.contrary(*p) ? "Contrary right now: they will do the opposite." : p->lastTold == sim.day() ? "Already narrated today. Push too often and they turn contrary." : "Once a day is welcome."), x, y, w, 14, T.text) + 8;
        y += wrapped(F.body, "Choose what happens next. If they go along, the line's price comes out of your rapport. The less they buy in, the more it costs and the likelier they say no.", x, y, w, 14, T.muted) + 6;
        for (auto& l : sim.linesFor(*p)) {
            float tw = w - 28 - 52;
            float th = wrapped(F.hand, l.text, 0, 0, tw, 19, T.text, false);
            Rectangle r{x, y, w, th + 16};
            bool afford = float(l.cost) <= sim.rapport, hot = afford && hovered(r);
            std::string will = sim.likely(*p, l);
            DrawRectangleRounded(r, .14f, 6, hot ? T.hover : T.panel2);
            DrawRectangle(int(x), int(y + 6), 3, int(th + 4), T.narrator);
            wrapped(F.hand, l.text, x + 14, y + 6, tw, 19, afford ? T.text : T.muted);
            std::string price = cat(l.cost);
            const char* hint = !afford ? "can't afford" : will == "agreed" ? "will" : will == "haggled" ? "later" : "won't";
            text(F.bold, price, x + w - 14 - measure(F.bold, price, 15).x, y + 8, 15, afford ? T.narrator : T.berry);
            text(F.body, hint, x + w - 14 - measure(F.body, hint, 11).x, y + 26, 11, will == "refused" || !afford ? T.berry : T.muted);
            if (afford && clicked(r)) sim.narrate(*p, l);
            y += r.height + 6;
        }
        y += 4;
        if (sim.lastToldName == p->name && !sim.lastReply.empty()) {
            std::string r = sim.lastReply == "agreed" ? p->name + " went along with it." : sim.lastReply == "haggled" ? p->name + " will, in their own time." : sim.lastReply == "refused" ? p->name + " would rather not."
                          : sim.lastReply == "pushed" ? "Twice in a day. " + p->name + " rolled their eyes." : p->name + " has had enough narrating and is doing the opposite.";
            if (!sim.lastSaid.empty()) r += " \"" + sim.lastSaid + "\"";
            y += wrapped(F.body, r, x, y, w, 14, T.narrator) + 6;
        }
        y += 10;
    }
    // words in the sand
    y += sectionTitle("Words in the sand", x, y);
    y += wrapped(F.body, "Write a line. They will find it at dawn and take it to heart. Once a day.", x, y, w, 14, T.muted) + 8;
    bool enter = textField({x, y, w - 84, 36}, sandText, sandFocus, 120, "Winter will be long and hungry...");
    if (button({x + w - 76, y + 3, 76, 30}, "Write", true, false, 14) || enter) {
        auto r = sim.writeSand(sandText);
        if (!r.empty()) { sandNote = r; if (r.find("tide") == std::string::npos) sandText.clear(); }
        sandFocus = false;
    }
    y += 44;
    if (!sandNote.empty()) y += wrapped(F.body, sandNote, x, y, w, 13.5f, T.narrator) + 6;
    y += 12;
    // proposals
    y += sectionTitle("Propose a project", x, y);
    y += wrapped(F.body, "Plant an idea. The settlers argue and vote. One proposal a day.", x, y, w, 14, T.muted) + 8;
    bool used = sim.lastProposal == sim.day();
    auto props = sim.proposalList();
    if (props.empty()) y += wrapped(F.body, "No new ideas right now. Some unlock later (a dock, a signal fire, children).", x, y, w, 14, T.muted) + 8;
    for (auto* o : props) {
        bool gok = sim.giftOk(*o);
        float top = y;
        float tw = w - 104;
        std::string info = o->desc + cat(" ", o->wood, " wood", o->stone ? cat(", ", o->stone, " stone") : "", ".");
        float hgt = 22 + wrapped(F.body, info, x + 10, 0, tw, 13, T.muted, false);
        std::string gl;
        if (!o->gift.empty()) { auto* hld = sim.giftHolder(o->gift, o->min); gl = gok ? giftInfo().at(o->gift).n + ": " + hld->name : "needs a skilled " + lower(giftInfo().at(o->gift).n); hgt += 18; }
        DrawRectangleRounded({x, top, w, hgt + 14}, .1f, 6, T.panel2);
        text(F.bold, o->name, x + 10, top + 8, 15, T.text);
        float yy = top + 30;
        yy += wrapped(F.body, info, x + 10, yy, tw, 13, T.muted);
        if (!gl.empty()) text(F.bold, gl, x + 10, yy, 12.5f, gok ? T.ok : T.berry);
        if (button({x + w - 92, top + (hgt + 14) / 2 - 15, 82, 30}, "Propose", !used && gok, false, 13)) sim.propose(o->key);
        y = top + hgt + 20;
    }
    if (sim.vote) {
        auto& v = *sim.vote;
        std::string s = cat(v.name, ": ", v.pass ? "approved " : "rejected ", v.y, " to ", v.n, ".\n");
        for (auto& r : v.rows) if (auto* q = sim.person(r.pid)) s += q->name + (r.yes ? " yes" : " no") + " (" + r.why + ").  ";
        DrawRectangle(int(x), int(y), 3, int(wrapped(F.body, s, x + 12, y + 6, w - 16, 13.5f, T.text, false) + 12), T.narrator);
        y += wrapped(F.body, s, x + 12, y + 6, w - 16, 13.5f, T.text) + 18;
    }
    endScroll(a, sideScroll, y + sideScroll.y - a.y + 20);
}

void Game::drawCamp(Rectangle a) {
    beginScroll(a, sideScroll);
    float x = a.x, w = a.width - 10, y = a.y - sideScroll.y;
    y += sectionTitle("Village needs", x, y);
    std::vector<Person*> ad;
    for (auto* p : sim.members()) if (!p->child) ad.push_back(p);
    if (!ad.empty()) {
        std::array<float, 5> t{}, g{};
        std::map<int, int> seek;
        for (auto* p : ad) { for (int i = 0; i < 5; i++) { t[i] += p->tiers[i] / ad.size(); g[i] += p->gates[i] / ad.size(); } if (p->seeking >= 0) seek[p->seeking]++; }
        y += pyramidRows(t, g, x, y, w, true);
        int top = -1, tc = 0;
        for (auto& [k, c] : seek) if (c > tc) { tc = c; top = k; }
        std::string s = top >= 0 ? cat("Most are seeking ", lower(TIER_NAMES[top]), ". ") : "Everyone feels fulfilled. ";
        std::string calls;
        for (auto* p : ad) if (!p->calling.empty()) calls += (calls.empty() ? "" : ", ") + p->name + " (" + lower(callingInfo().at(p->calling).n) + ")";
        s += calls.empty() ? "Nobody has found a calling yet." : "Callings: " + calls + ".";
        if (!sim.inventions.empty()) { s += " Inventions: "; bool first = true; for (auto& i : inventionList()) if (sim.inventions.count(i.k)) { s += (first ? "" : ", ") + i.n; first = false; } s += "."; }
        y += wrapped(F.body, s, x, y, w, 13.5f, T.muted) + 16;
    }
    y += sectionTitle("Stores", x, y);
    float bw = (w - 16) / 3;
    struct Box { const char* k; int n; std::string sub; } boxes[3] = {
        {"FOOD", int(sim.stock.food), cat(int(sim.foodDays() * 10) / 10, ".", int(sim.foodDays() * 10) % 10, " days")},
        {"WOOD", int(sim.stock.wood), sim.fire() ? (sim.fireLit() ? "fire lit" : "fire out") : "no fire yet"},
        {"STONE", int(sim.stock.stone), cat("tools ", 100 - int(sim.toolWear), "%")}};
    for (int i = 0; i < 3; i++) {
        Rectangle r{x + i * (bw + 8), y, bw, 70};
        DrawRectangleRounded(r, .15f, 6, T.panel2);
        text(F.bold, boxes[i].k, r.x + 10, r.y + 8, 11, T.muted);
        text(F.head, std::to_string(boxes[i].n), r.x + 10, r.y + 22, 24, T.text);
        text(F.body, boxes[i].sub, r.x + 10, r.y + 50, 12, T.muted);
    }
    y += 86;
    // goods, and the kinds of food lately
    {
        std::string g;
        for (auto& gi : Sim::goodList()) g += (g.empty() ? "" : "   ") + cat(gi.name, " ", int(sim.good(gi.key)));
        y += wrapped(F.body, g, x, y, w, 13.5f, T.text) + 4;
        std::string kinds;
        for (auto& [k, d] : sim.foodSeen) if (d >= sim.day() - 5) kinds += (kinds.empty() ? "" : ", ") + k;
        y += wrapped(F.body, kinds.empty() ? "No food brought in lately." : cat("Food lately: ", kinds, " (", sim.foodVariety(), sim.foodVariety() == 1 ? " kind)" : " kinds)"), x, y, w, 13.5f, T.muted) + 14;
    }
    y += sectionTitle("Projects", x, y);
    if (sim.agenda) if (auto* ag = sim.person(sim.agenda->by)) {
        static const std::map<std::string, std::string> verb = {{"stone", "breaking stone"}, {"wood", "hauling timber"}, {"farm", "working the fields"}, {"food", "gathering food"}};
        y += wrapped(F.body, cat("Agenda: ", ag->name, " the ", lower(giftInfo().at(sim.agenda->gift).n), " (charisma ", ag->charisma, ") has people ", verb.at(sim.agenda->job), "."), x, y, w, 13.5f, T.ember) + 8;
    }
    Project* cur = sim.activeProject();
    struct Row { Project* b; int order; float score; int i; };
    std::vector<Row> rows;
    int i = 0;
    for (auto& b : sim.plan) {
        if (b.skip) { i++; continue; }
        int ord = &b == cur ? 0 : b.done ? 3 : (!sim.giftOk(b) || !sim.prereqOk(b)) ? 2 : 1;
        rows.push_back({&b, ord, sim.projScore(b, i), i});
        i++;
    }
    std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.order != b.order ? a.order < b.order : a.score > b.score; });
    for (auto& r : rows) {
        auto& b = *r.b;
        bool now = &b == cur, blocked = r.order == 2;
        float pct = b.done ? 1 : b.progress / b.work;
        float cx = x + 8, cy = y + 10;
        if (b.done) draw::circle(cx, cy, 6, T.ok);
        else if (now) { draw::ring(cx, cy, 7, 7, 2, T.ember); for (int k = 0; k < 24; k++) if (k / 24.f < pct) { float a0 = -1.5708f + k / 24.f * 6.283f; draw::line(cx, cy, cx + std::cos(a0) * 6, cy + std::sin(a0) * 6, 2, T.ember); } }
        else draw::ring(cx, cy, 7, 7, 2, T.line);
        std::string name = b.name + (b.voiced ? " *" : "");
        Color nc = b.done ? T.muted : blocked ? Color{146, 150, 140, 255} : T.text;
        text(now ? F.bold : F.body, name, x + 24, y + 2, 15, nc);
        float nx = x + 24 + measure(now ? F.bold : F.body, name, 15).x + 6;
        if (!b.gift.empty() && !b.done) text(F.body, giftInfo().at(b.gift).n, nx, y + 4, 12, T.muted);
        std::string need;
        if (b.done) need = "";
        else if (!sim.giftOk(b)) need = "needs a " + lower(giftInfo().at(b.gift).n);
        else if (!sim.prereqOk(b)) need = b.req == "child" ? "needs children" : "needs " + lower(projectDef(b.req)->name);
        else if (now) need = (b.wood - b.dw > 0 || b.stone - b.ds > 0) ? cat(b.wood - b.dw > 0 ? cat(int(std::ceil(b.wood - b.dw)), " wood ") : "", b.stone - b.ds > 0 ? cat(int(std::ceil(b.stone - b.ds)), " stone") : "", " to go") : cat(int(pct * 100), "% built");
        else if (auto ch = sim.champion(b)) need = ch->p->name + " pushes";
        else need = cat(b.wood, "w", b.stone ? cat(" ", b.stone, "s") : "");
        text(F.body, need, x + w - measure(F.body, need, 12.5f).x, y + 4, 12.5f, blocked ? T.berry : T.muted);
        y += 26;
    }
    endScroll(a, sideScroll, y + sideScroll.y - a.y + 20);
}

void Game::drawJournal() {
    Rectangle r = rJournal;
    DrawRectangleRounded(r, .06f, 8, draw::withA(T.panel, .95f));
    DrawRectangleRoundedLinesEx(r, .06f, 8, 1, T.line);
    text(F.head, "Camp Journal", r.x + 14, r.y + 8, 20, T.text);
    if (button({r.x + r.width - 86, r.y + 6, 74, 26}, journalOpen ? "Hide" : "Show", true, false, 13)) journalOpen = !journalOpen;
    if (!journalOpen) {
        // the newest line, cut to fit beside the title
        if (sim.journal.empty()) return;
        auto& e = sim.journal.front();
        float x0 = r.x + 14 + measure(F.head, "Camp Journal", 20).x + 18, room = r.x + r.width - 100 - x0;
        std::string line = e.text;
        if (measure(F.hand, line, 21).x > room) {
            while (!line.empty() && measure(F.hand, line + "...", 21).x > room) line.pop_back();
            line += "...";
        }
        Color c = e.cls == "big" ? T.accent : e.cls == "sad" ? T.berry : e.cls == "narrator" ? T.narrator : T.text;
        text(F.hand, line, x0, r.y + 9, 21, c);
        return;
    }
    Rectangle a{r.x + 12, r.y + 40, r.width - 20, r.height - 48};
    float tw = a.width - 110;
    if (journalVersionCached != sim.journalVersion || journalWidthCached != tw) {
        journalHeights.clear();
        for (auto& e : sim.journal) journalHeights.push_back(wrapped(F.hand, e.text, 0, 0, tw, 23, WHITE, false, 1.05f) + 6);
        journalVersionCached = sim.journalVersion;
        journalWidthCached = tw;
    }
    beginScroll(a, journalScroll);
    float y = a.y - journalScroll.y, total = 0;
    size_t i = 0;
    for (auto& e : sim.journal) {
        float h = i < journalHeights.size() ? journalHeights[i] : 30;
        if (y + h > a.y && y < a.y + a.height) {
            text(F.bold, cat("DAY ", e.d), a.x, y + 6, 11, T.muted);
            text(F.body, e.season, a.x, y + 20, 11, T.muted);
            Color c = e.cls == "big" ? T.accent : e.cls == "sad" ? T.berry : e.cls == "narrator" ? T.narrator : T.text;
            wrapped(F.hand, e.text, a.x + 100, y, tw, 23, c, true, 1.05f);
        }
        y += h; total += h; i++;
    }
    endScroll(a, journalScroll, total);
}

void Game::drawFate() {
    DrawRectangleRec(rMap, Color{10, 14, 22, 170});
    float cx = rMap.x + rMap.width / 2, cy = rMap.y + rMap.height / 2;
    textCentered(F.head, "The fates offer a choice", {rMap.x, cy - 190, rMap.width, 40}, 32, Color{243, 238, 226, 255});
    textCentered(F.body, "A new season turns. As the unseen narrator, choose what the island brings next.", {rMap.x, cy - 150, rMap.width, 24}, 16, Color{207, 214, 208, 255});
    int n = int(sim.pendingFate.size());
    float cw = 250, ch = 210, gap = 18, total = n * cw + (n - 1) * gap;
    int chosen = -1;
    for (int i = 0; i < n; i++) {
        auto& f = sim.pendingFate[i];
        Rectangle r{cx - total / 2 + i * (cw + gap), cy - 100, cw, ch};
        bool h = hovered(r);
        if (h) r.y -= 4;
        DrawRectangleRounded({r.x + 3, r.y + 6, r.width, r.height}, .06f, 8, Color{0, 0, 0, 90});
        DrawRectangleRounded(r, .06f, 8, Color{246, 241, 228, 255});
        if (h) DrawRectangleRoundedLinesEx(r, .06f, 8, 2, T.narrator);
        text(F.bold, "FATE", r.x + 18, r.y + 16, 12, Color{122, 79, 176, 255});
        float yy = r.y + 36;
        yy += wrapped(F.head, f.title, r.x + 18, yy, cw - 36, 20, Color{42, 36, 28, 255}, true, 1.15f) + 6;
        wrapped(F.body, f.desc, r.x + 18, yy, cw - 36, 14.5f, Color{74, 66, 54, 255});
        if (clicked(r)) chosen = i;
    }
    if (chosen >= 0) { auto f = sim.pendingFate[chosen]; sim.pendingFate.clear(); sim.applyFate(f); }
}

// What is waiting on the narrator, top right of the map: settlers' questions
// (one opens to show its replies) and a building waiting for its spot.
void Game::drawDecisions() {
    float w = 340, pad = 14, x = rMap.x + rMap.width - w - 14, y = rTop.height + 14;
    decisionsR = {x, y, w, 0};
    if (sim.questions.empty() && !sim.pendingPlacement && sim.petitions.empty()) return;
    if (openQuestion >= 0 && std::none_of(sim.questions.begin(), sim.questions.end(), [&](const Question& q) { return q.id == openQuestion; })) openQuestion = -1;
    // measure and draw in one pass: draw the panel behind once the height is known
    float yy = y + pad;
    struct Row { Rectangle r; int id; };
    std::vector<Row> rows;
    std::vector<std::function<void()>> paint;
    paint.push_back([=] { text(F.bold, "Waiting on you", x + pad, y + pad, 13, T.muted); });
    yy += 22;
    if (sim.pendingPlacement)
        if (Project* b = sim.projectById(sim.pendingPlacement->project)) {
            std::string s = "The " + lower(b->name) + " needs a spot";
            paint.push_back([=] { DrawCircle(int(x + pad + 4), int(yy + 9), 4, T.ember); text(F.body, s, x + pad + 16, yy, 14, T.text); });
            yy += 24;
        }
    for (auto& q : sim.questions) {
        Person* p = sim.person(q.pid);
        if (!p) continue;
        int left = std::max(1, int((q.expires - sim.tMin) / 60));
        bool open = openQuestion == q.id;
        std::string head = p->name + " has a question", hours = cat(left, "h");
        Rectangle hr{x + 6, yy - 4, w - 12, 26};
        rows.push_back({hr, q.id});
        paint.push_back([=] {
            if (hovered(hr) || open) DrawRectangleRounded(hr, .3f, 6, T.panel2);
            DrawCircle(int(x + pad + 4), int(yy + 9), 4, T.narrator);
            text(F.bold, head, x + pad + 16, yy, 14, T.text);
            text(F.body, hours, x + w - pad - measure(F.body, hours, 13).x, yy + 1, 13, T.muted);
        });
        yy += 26;
        if (!open) continue;
        std::string ask = "\"" + q.text + "\"";
        float th = wrapped(F.hand, ask, 0, 0, w - pad * 2, 21, T.text, false);
        paint.push_back([=] { wrapped(F.hand, ask, x + pad, yy, w - pad * 2, 21, T.text); });
        yy += th + 8;
        static const std::map<std::string, std::string> tag = {{"honest", "Honest"}, {"teasing", "Tease"}, {"misleading", "Mislead"}};
        for (int i = 0; i < int(q.replies.size()); i++) {
            const Reply& r = q.replies[i];
            std::string t = tag.count(r.kind) ? tag.at(r.kind) : r.kind;
            float rh = wrapped(F.body, r.text, 0, 0, w - pad * 2 - 74, 14, T.text, false) + 14;
            Rectangle rr{x + pad, yy, w - pad * 2, rh};
            int qid = q.id;
            paint.push_back([=, this] {
                bool hot = hovered(rr);
                DrawRectangleRounded(rr, .2f, 6, hot ? T.hover : T.panel2);
                text(F.bold, t, rr.x + 10, rr.y + 7, 13, r.kind == "misleading" ? T.berry : r.kind == "teasing" ? T.ember : T.ok);
                wrapped(F.body, r.text, rr.x + 74, rr.y + 7, rr.width - 84, 14, T.text);
                if (clicked(rr)) { sim.answer(qid, i); openQuestion = -1; }
            });
            yy += rh + 5;
        }
        yy += 6;
    }
    // petitions: a household asking for something, by a day
    for (auto& pt : sim.petitions) {
        Household* hh = sim.household(pt.hh);
        if (!hh) continue;
        int left = std::max(0, pt.due - sim.day());
        bool open = openPetition == pt.id;
        std::string head = sim.householdName(*hh) + (pt.kind == "feast" ? " ask for a feast" : pt.kind == "home" ? " ask for a better home" : " ask a favour"), when = left == 0 ? "today" : cat(left, "d");
        Rectangle hr{x + 6, yy - 4, w - 12, 26};
        rows.push_back({hr, -1000 - pt.id});
        paint.push_back([=] {
            if (hovered(hr) || open) DrawRectangleRounded(hr, .3f, 6, T.panel2);
            DrawCircle(int(x + pad + 4), int(yy + 9), 4, T.accent);
            text(F.bold, head, x + pad + 16, yy, 14, T.text);
            text(F.body, when, x + w - pad - measure(F.body, when, 13).x, yy + 1, 13, left <= 2 ? T.berry : T.muted);
        });
        yy += 26;
        if (!open) continue;
        std::string say = "\"" + pt.text + "\"", need = "Met by " + sim.petitionNeed(pt) + ".";
        float th = wrapped(F.hand, say, 0, 0, w - pad * 2, 21, T.text, false), nh = wrapped(F.body, need, 0, 0, w - pad * 2, 13.5f, T.muted, false);
        paint.push_back([=] { wrapped(F.hand, say, x + pad, yy, w - pad * 2, 21, T.text); wrapped(F.body, need, x + pad, yy + th + 4, w - pad * 2, 13.5f, T.muted); });
        yy += th + nh + 14;
    }
    float h = yy - y + pad - 6;
    decisionsR.height = h;
    DrawRectangleRounded({x + 2, y + 4, w, h}, .06f, 8, Color{40, 50, 40, 36});
    panel({x, y, w, h}, draw::withA(T.panel, .97f), T.line, .06f);
    for (auto& f : paint) f();
    for (auto& r : rows) if (clicked(r.r)) {
        if (r.id <= -1000) { int pid = -1000 - r.id; openPetition = openPetition == pid ? -1 : pid; }
        else openQuestion = openQuestion == r.id ? -1 : r.id;
    }
}

// The Narrator's Ledger: what is going wrong, who could fix it and what each
// size of nudge would cost, and the roles and seasons already given out.
float Game::drawLedger(float x, float y, float w) {
    // the trial the narrator has seen coming this year, and how ready the village is
    for (auto& t : sim.trials) {
        if (t.over || t.year != sim.yearN()) continue;
        y += sectionTitle(t.active ? "The trial is here" : "The year's trial", x, y);
        int in = t.start - sim.day();
        std::string when = t.active ? cat(Sim::trialName(t.kind), ", day ", sim.day() - t.start + 1, " of 10. We were ", int(t.ready * 100), "% ready.")
                                    : cat(Sim::trialName(t.kind), ", in ", in, in == 1 ? " day." : " days.");
        when[0] = char(std::toupper(when[0]));
        y += wrapped(F.bold, when, x, y, w, 14, t.active ? T.berry : T.text) + 4;
        if (!t.active)
            for (auto& n : sim.trialChecklist(t.kind)) {
                text(F.bold, n.met ? "+" : "-", x + 4, y, 14, n.met ? T.ok : T.berry);
                y += wrapped(F.body, n.text, x + 20, y, w - 20, 13.5f, n.met ? T.muted : T.text) + 2;
            }
        y += 10;
    }
    y += sectionTitle("Ledger", x, y);
    auto gaps = sim.ledger();
    bool night = sim.hour() < 7 || sim.hour() >= 18;
    if (gaps.empty()) y += wrapped(F.body, night ? "Evening. The Ledger picks up again in the morning, when the work starts." : "Nothing pressing. The village is managing on its own.", x, y, w, 14, T.muted) + 12;
    auto small = [&](float bx, float by, const std::string& label, int cost, bool ok) {
        std::string t = cat(label, " ", cost);
        float bw = measure(F.bold, t, 12).x + 18;
        bool r = button({bx, by, bw, 26}, t, ok && float(cost) <= sim.rapport, false, 12);
        return std::pair<bool, float>{r, bw};
    };
    for (auto& g : gaps) {
        DrawCircle(int(x + 4), int(y + 9), 4, T.ember);
        y += wrapped(F.bold, g.text, x + 16, y, w - 16, 14, T.text) + 2;
        y += wrapped(F.body, g.fix, x + 16, y, w - 16, 13, T.muted) + 6;
        for (auto& c : g.candidates) {
            Person* p = sim.person(c.pid);
            if (!p) continue;
            text(F.bold, p->name, x + 16, y + 1, 13, T.text);
            text(F.body, c.why, x + 16 + measure(F.bold, p->name, 13).x + 8, y + 2, 12, T.muted);
            y += 20;
            float bx = x + 16;
            auto [d, w1] = small(bx, y, "Today", c.cost, true);
            bx += w1 + 6;
            if (d) {
                Line l{g.job, cat(p->name, " decided to help."), -1, c.cost};
                for (auto& o : sim.linesFor(*p, true)) if (o.job == g.job) l = o;
                ledgerNote = p->name + ": " + sim.narrate(*p, l);
            }
            if (g.job != "rest") {
                auto [s, w2] = small(bx, y, "Season", sim.seasonCost(*p, g.job), true);
                bx += w2 + 6;
                if (s) ledgerNote = p->name + ": " + sim.askSeason(*p, g.job);
                auto [r, w3] = small(bx, y, cat("Make ", Sim::roleName(g.job)), sim.roleCost(*p, g.job), !p->duty);
                if (r) ledgerNote = p->name + ": " + sim.appoint(*p, g.job);
            }
            y += 32;
        }
        if (!g.job.empty()) {
            auto [e, we] = small(x + 16, y, "Ask everyone", sim.appealCost(g.job), true);
            if (e) ledgerNote = sim.appeal(g.job);
            y += 34;
        }
        y += 6;
    }
    if (!ledgerNote.empty()) {
        std::string n = ledgerNote;
        for (auto [k, v] : std::vector<std::pair<std::string, std::string>>{{"agreed", "agreed"}, {"haggled", "will, later"}, {"refused", "said no"}, {"unaffordable", "not enough rapport"}, {"pushed", "twice in a day, rolled their eyes"}, {"contrary", "is doing the opposite"}})
            if (auto at = n.find(k); at != std::string::npos) { n.replace(at, k.size(), v); break; }
        y += wrapped(F.body, n + (sim.lastSaid.empty() ? "" : " \"" + sim.lastSaid + "\""), x, y, w, 14, T.narrator) + 8;
    }
    // roles and seasons already given out
    bool any = false;
    for (auto* p : sim.members()) if (p->duty) any = true;
    if (any) {
        text(F.bold, "Roles and seasons", x, y, 13, T.muted);
        y += 20;
        for (auto* p : sim.members()) {
            if (!p->duty) continue;
            std::string s = p->duty->role ? cat(p->name, ", the ", Sim::roleName(p->duty->job), " (1 rapport a day)") : cat(p->name, ", ", Sim::roleName(p->duty->job), " until the season turns");
            text(F.body, s, x, y + 5, 13, T.text);
            if (button({x + w - 74, y, 74, 26}, p->duty->role ? "Revoke" : "Let off", true, false, 12)) sim.revoke(*p);
            y += 30;
        }
    }
    return y + 14;
}

// The build menu: every blueprint, what it costs, whether it is unlocked and
// who it needs. Placing one hands it to the placement screen; the settlers
// build it when they see fit.
void Game::drawBuild(Rectangle a) {
    beginScroll(a, sideScroll);
    float x = a.x, w = a.width - 10, y = a.y - sideScroll.y;
    y += sectionTitle("Blueprints", x, y);
    if (settlersPlace) {
        y += wrapped(F.body, "The settlers are choosing every building and its spot themselves. Switch to \"Building spots: you choose\" in the pause menu to draw up blueprints.", x, y, w, 14, T.muted) + 12;
        endScroll(a, sideScroll, y + sideScroll.y - a.y + 20);
        return;
    }
    y += wrapped(F.body, cat("Mark out a spot and the settlers build it when they see fit. Homes, the campfire, lean-tos and racks they build themselves. The village has cleared ", sim.villageLevel(), " of 5 levels of need; more blueprints unlock as it rises."), x, y, w, 14, T.muted) + 12;
    for (auto* d : sim.blueprints()) {
        std::string why;
        bool open = sim.unlocked(d->key, &why);
        int built = 0, waiting = 0;
        for (auto& b : sim.plan) if (b.key == d->key && !b.skip) { if (b.done) built++; else if (b.placed) waiting++; }
        std::string sub = cat(d->wood, " wood", d->stone ? cat(", ", d->stone, " stone") : std::string());
        if (built) sub += cat(", ", built, " built");
        if (waiting) sub += cat(", ", waiting, " marked out");
        std::string need = !open ? why : !d->gift.empty() && !sim.giftHolder(d->gift, d->min) ? cat("Needs a ", lower(giftInfo().at(d->gift).n), d->min > 1 ? cat(" of level ", d->min) : std::string(), "; it will wait for one.") : !sim.prereqOk(*d) ? cat("Needs a ", d->req == "child" ? std::string("child in the village") : lower(projectDef(d->req)->name), " first.") : "";
        float th = 22 + 18 + (need.empty() ? 0 : wrapped(F.body, need, 0, 0, w - 110, 12.5f, T.muted, false) + 2);
        Rectangle r{x, y, w, th + 12};
        DrawRectangleRounded(r, .12f, 6, T.panel2);
        text(F.bold, d->name, x + 12, y + 8, 15, open ? T.text : T.muted);
        text(F.body, sub, x + 12, y + 30, 12.5f, T.muted);
        if (!need.empty()) wrapped(F.body, need, x + 12, y + 48, w - 110, 12.5f, open ? T.ember : T.muted);
        if (button({x + w - 84, y + 10, 74, 30}, "Place", open && !sim.pendingPlacement, false, 13)) {
            if (sim.blueprint(d->key) < 0) say("There is nowhere it can go yet. Explore more of the island, or clear some ground.");
        }
        y += r.height + 6;
    }
    endScroll(a, sideScroll, y + sideScroll.y - a.y + 20);
}
