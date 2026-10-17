// World generation, exploration, time, journal and lookup helpers.
#include <algorithm>
#include "Sim.h"

namespace dw {

namespace {
struct Noise {
    std::vector<float> p;
    explicit Noise(Mulberry& r) : p(256 * 256) { for (auto& v : p) v = r(); }
    float g(int a, int b) const { return p[(b & 255) * 256 + (a & 255)]; }
    float operator()(float x, float y) const {
        int xi = (int)std::floor(x), yi = (int)std::floor(y);
        float xf = x - xi, yf = y - yi, u = xf * xf * (3 - 2 * xf), v = yf * yf * (3 - 2 * yf);
        float a = g(xi, yi), b = g(xi + 1, yi), c = g(xi, yi + 1), d = g(xi + 1, yi + 1);
        return a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v;
    }
};
float fbm(const Noise& n, float x, float y) {
    float s = 0, a = .5f, f = 1, t = 0;
    for (int o = 0; o < 4; o++) { s += n(x * f, y * f) * a; t += a; a *= .5f; f *= 2; }
    return s / t;
}
const std::vector<std::string> NAMES = {"Mara","Tobin","Isla","Ren","Oskar","Wren","Juno","Elio","Saba","Teo","Nell","Ansel","Kaia","Bram","Ada","Finn","Noor","Iker","Vera","Lio","Hana","Cole","Pia","Arlo","Sol","Edda","Rafe","Mina","Otto","Lark","Zuri","Benno","Ilse","Kit","Rosa","Dov"};
const std::vector<uint32_t> TUNICS = {0xc8553d,0x3d7ec8,0xe0b13a,0x5a9a5b,0x8d5bb5,0xd47fa6,0x2fa39b,0xb07a3c,0x6a7fd0,0xc97a2c,0x4d8c7a,0xa04f6a};
} // namespace

float Sim::temp() const {
    static const float base[4] = {13, 22, 11, -1};
    return base[season()] + std::sin(float((hour() - 9) / 24 * 6.2831853)) * 5 + (weather == "storm" ? -4 : weather == "rain" ? -2 : 0);
}

std::string Sim::clock() const {
    double h = hour();
    int hh = int(h), mm = int((h - hh) * 6) * 10;
    char buf[8];
    snprintf(buf, sizeof buf, "%02d:%02d", hh, mm);
    return buf;
}

void Sim::note(const std::string& text, const std::string& cls) {
    journal.push_front({day(), SEASONS[season()], text, cls});
    if (journal.size() > 250) journal.pop_back();
    journalVersion++;
}

std::vector<Person*> Sim::members() {
    std::vector<Person*> v;
    for (auto& p : people) if (!p.dead && p.status == "member") v.push_back(&p);
    return v;
}
Person* Sim::person(int id) {
    for (auto& p : people) if (p.id == id) return &p;
    return nullptr;
}
bool Sim::isExplored(float x, float y) const {
    int ix = std::clamp(int(x), 0, GW - 1), iy = std::clamp(int(y), 0, GH - 1);
    return explored[idx(ix, iy)];
}

Project* Sim::built(const std::string& key) {
    for (auto& b : plan) if (b.key == key && b.done) return &b;
    return nullptr;
}
std::vector<Project*> Sim::builtAll(const std::string& key) {
    std::vector<Project*> v;
    for (auto& b : plan) if (b.key == key && b.done) v.push_back(&b);
    return v;
}
Project* Sim::projectById(int id) {
    for (auto& b : plan) if (b.id == id) return &b;
    return nullptr;
}

Obj& Sim::addObj(Obj o) {
    o.i = int(objs.size());
    o.res = -1;
    objs.push_back(o);
    objAt[idx(o.x, o.y)] = o.i;
    return objs.back();
}
void Sim::removeObj(Obj& o) {
    o.dead = true;
    if (objAt[idx(o.x, o.y)] == o.i) objAt[idx(o.x, o.y)] = -1;
}

std::vector<Shore> Sim::shoreTiles(bool freshOnly) {
    std::vector<Shore> out;
    const int D[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (int y = 0; y < GH; y++) for (int x = 0; x < GW; x++) {
        if (tt[idx(x, y)] < 2) continue;
        for (auto& d : D) {
            int nx = x + d[0], ny = y + d[1];
            if (inb(nx, ny) && tt[idx(nx, ny)] < 2 && (bool(fresh[idx(nx, ny)]) == freshOnly)) { out.push_back({x, y, d[0], d[1]}); break; }
        }
    }
    return out;
}

std::string Sim::listNames(const std::vector<Person*>& ps) {
    if (ps.empty()) return "someone";
    if (ps.size() == 1) return ps[0]->name;
    std::string s;
    for (size_t i = 0; i + 1 < ps.size(); i++) s += (i ? ", " : "") + ps[i]->name;
    return s + " and " + ps.back()->name;
}

std::string Sim::newName() {
    std::vector<std::string> free;
    for (auto& n : NAMES) if (!usedNames.count(n)) free.push_back(n);
    std::string n = free.empty() ? cat("Stranger ", nextPid) : pick(free);
    usedNames.insert(n);
    return n;
}

void Sim::placePOI(const std::string& type, std::function<bool(int)> ok, float minD) {
    Mulberry r(seed ^ (uint32_t)std::hash<std::string>{}(type));
    for (int k = 0; k < 3000; k++) {
        int x = 2 + int(r() * (GW - 4)), y = 2 + int(r() * (GH - 4)), i = idx(x, y);
        if (tt[i] < 2 || !ok(tt[i])) continue;
        if (std::hypot(float(x - campX), float(y - campY)) < minD || bOcc[i] != -1) continue;
        if (objAt[i] >= 0) removeObj(objs[objAt[i]]);
        if (type == "hermit" || type == "ruins")
            for (int yy = -1; yy <= 1; yy++) for (int xx = -1; xx <= 1; xx++)
                if (inb(x + xx, y + yy) && objAt[idx(x + xx, y + yy)] >= 0) removeObj(objs[objAt[idx(x + xx, y + yy)]]);
        bOcc[i] = -3;
        pois.push_back({type, x, y});
        return;
    }
}

void Sim::newWorld(uint32_t s) {
    *this = Sim();
    seed = s;
    gen.seed(s * 2654435761u + 7);
    const int N = GW * GH;
    for (int tries = 0;; tries++) {
        Mulberry R(seed + tries * 7919);
        Noise n1(R), n2(R), n3(R);
        tt.assign(N, 0); fresh.assign(N, 0); sea.assign(N, 0);
        float ox = R() * 50, oy = R() * 50;
        for (int y = 0; y < GH; y++) for (int x = 0; x < GW; x++) {
            float nx = float(x) / GW * 2 - 1, ny = float(y) / GH * 2 - 1, d = std::sqrt(nx * nx + ny * ny);
            float e = fbm(n1, x / 13.f + ox, y / 13.f + oy) + .56f - d * d;
            uint8_t t = e < .42f ? DEEP : e < .5f ? SHALLOW : e < .535f ? SAND : e > .93f ? HILL : fbm(n2, x / 8.f, y / 8.f) > .54f ? FOREST : GRASS;
            tt[idx(x, y)] = t;
        }
        // flood the sea from the map edges; enclosed water is fresh
        std::vector<std::pair<int, int>> st;
        for (int i = 0; i < GW; i++) { st.push_back({i, 0}); st.push_back({i, GH - 1}); st.push_back({0, i}); st.push_back({GW - 1, i}); }
        while (!st.empty()) {
            auto [x, y] = st.back(); st.pop_back();
            if (!inb(x, y)) continue;
            int i = idx(x, y);
            if (sea[i] || tt[i] >= 2) continue;
            sea[i] = 1;
            st.push_back({x + 1, y}); st.push_back({x - 1, y}); st.push_back({x, y + 1}); st.push_back({x, y - 1});
        }
        for (int i = 0; i < N; i++) if (tt[i] < 2 && !sea[i]) fresh[i] = 1;
        distSea.assign(N, 99);
        std::vector<int> q;
        for (int i = 0; i < N; i++) if (sea[i]) { distSea[i] = 0; q.push_back(i); }
        for (size_t h = 0; h < q.size(); h++) {
            int i = q[h], x = i % GW, y = i / GW;
            const int D[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            for (auto& dd : D) {
                int nx = x + dd[0], ny = y + dd[1];
                if (!inb(nx, ny)) continue;
                int j = idx(nx, ny);
                if (distSea[j] > distSea[i] + 1) { distSea[j] = distSea[i] + 1; q.push_back(j); }
            }
        }
        int land = 0;
        for (int i = 0; i < N; i++) if (tt[i] >= 2) land++;
        std::vector<std::pair<int, int>> cands;
        for (int y = 4; y < GH - 4; y++) for (int x = 4; x < GW - 4; x++) {
            int i = idx(x, y);
            if (tt[i] == GRASS && distSea[i] >= 5 && distSea[i] <= 7 && x + y > GW * .9f) cands.push_back({x, y});
        }
        if ((land < N * .3f || cands.empty()) && tries < 20) continue;
        auto c = cands.empty() ? std::pair<int, int>{GW / 2, GH / 2} : cands[int(R() * cands.size()) % cands.size()];
        campX = c.first; campY = c.second; stockX = campX - 3; stockY = campY + 1;
        // pond near camp
        int px = campX - 7, py = campY - 2;
        for (int k = 0; k < 400; k++) {
            float a = R() * 6.283f, r = 6 + R() * 4;
            int tx = int(std::round(campX + std::cos(a) * r)), ty = int(std::round(campY + std::sin(a) * r));
            if (inb(tx, ty) && tt[idx(tx, ty)] >= 3 && distSea[idx(tx, ty)] >= 5) { px = tx; py = ty; break; }
        }
        for (int y = -4; y <= 4; y++) for (int x = -4; x <= 4; x++) {
            int tx = px + x, ty = py + y;
            if (!inb(tx, ty)) continue;
            float d = std::hypot(float(x), y * 1.1f) - (fbm(n3, tx / 3.f, ty / 3.f) - .5f) * 2;
            if (d < 2.4f && tt[idx(tx, ty)] >= 2 && distSea[idx(tx, ty)] >= 3) { tt[idx(tx, ty)] = d < 1.3f ? DEEP : SHALLOW; fresh[idx(tx, ty)] = 1; }
        }
        pondX = px; pondY = py;
        objAt.assign(N, -1); bOcc.assign(N, -1); explored.assign(N, 0);
        for (int y = -1; y <= 1; y++) for (int x = -1; x <= 1; x++) bOcc[idx(campX + x, campY + y)] = -2;
        for (int x = 0; x < 2; x++) bOcc[idx(stockX + x, stockY)] = -2;
        for (int y = 0; y < GH; y++) for (int x = 0; x < GW; x++) {
            int i = idx(x, y), t = tt[i];
            float dc = std::hypot(float(x - campX), float(y - campY));
            if (t < 2 || bOcc[i] != -1) continue;
            Obj o; o.x = x; o.y = y;
            if (t == SAND) { if (R() < .05f && dc > 3) { o.type = "tree"; o.palm = true; o.size = .9f + R() * .3f; addObj(o); } continue; }
            if (dc < 4.5f) continue;
            if (t == HILL) {
                if (R() < .35f) { o.type = "rock"; o.stone = 8; o.size = .9f + R() * .5f; addObj(o); }
                else if (R() < .15f) { o.type = "tree"; o.conifer = true; o.size = .8f + R() * .3f; addObj(o); }
                continue;
            }
            if (R() < (t == FOREST ? .55f : .04f)) { o.type = "tree"; o.conifer = fbm(n3, x / 6.f + 30, y / 6.f) > .52f; o.size = .8f + R() * .45f; addObj(o); continue; }
            if (fbm(n3, x / 5.f + 50, y / 5.f + 50) > .68f && R() < .35f) { o.type = "rock"; o.stone = 6; o.size = .8f + R() * .5f; addObj(o); continue; }
            if (t == GRASS && R() < .035f) { o.type = "bush"; o.berries = 2 + int(R() * 3); addObj(o); }
        }
        auto ensure = [&](const std::string& type, int want, float minD) {
            int c2 = 0;
            for (auto& o : objs) if (o.type == type) c2++;
            for (int k = 0; k < 800 && c2 < want; k++) {
                int x = int(R() * GW), y = int(R() * GH), i = idx(x, y);
                float d = std::hypot(float(x - campX), float(y - campY));
                if (tt[i] >= 3 && objAt[i] < 0 && bOcc[i] == -1 && d > minD && d < 14) {
                    Obj o; o.type = type; o.x = x; o.y = y; o.size = .9f; o.berries = 3; addObj(o); c2++;
                }
            }
        };
        ensure("rock", 6, 5);
        ensure("bush", 8, 3);
        // the boat lies on the nearest sandy beach
        float bd = 1e9; boatX = campX + 4; boatY = campY;
        for (int y = 0; y < GH; y++) for (int x = 0; x < GW; x++) {
            int i = idx(x, y);
            if (tt[i] != SAND) continue;
            bool adj = false;
            const int D[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            for (auto& dd : D) if (inb(x + dd[0], y + dd[1]) && sea[idx(x + dd[0], y + dd[1])]) adj = true;
            float d = std::hypot(float(x - campX), float(y - campY));
            if (adj && d < bd) { bd = d; boatX = x; boatY = y; }
        }
        if (objAt[idx(boatX, boatY)] >= 0) removeObj(objs[objAt[idx(boatX, boatY)]]);
        placePOI("hermit", [](int t) { return t == FOREST; }, 18);
        placePOI("cave", [](int t) { return t == HILL || t == GRASS; }, 14);
        placePOI("ruins", [](int t) { return t == GRASS; }, 13);
        placePOI("wreck", [](int t) { return t == SAND; }, 15);
        break;
    }
    pondShore = shoreTiles(true);
    seaShore = shoreTiles(false);
    if (pondShore.empty()) pondShore = seaShore;
    for (auto& k : {"fire","leanto","rack","hut","garden","hut","well","store","forge","hut","dock","herbs","mine","longhouse","pottery","loomhouse","fields","tower"})
        mkProj(*projectDef(k));

    // the castaways
    std::vector<std::string> names = NAMES;
    std::shuffle(names.begin(), names.end(), gen);
    std::vector<uint32_t> tun = TUNICS;
    std::shuffle(tun.begin(), tun.end(), gen);
    for (int i = 0; i < 5; i++) {
        auto& p = makePerson(names[i], tun[i], boatX + .5f + (i % 3) * .4f - .4f, boatY + .5f + (i / 3) * .5f - .2f);
        p.status = "member";
        usedNames.insert(names[i]);
    }
    for (auto* p : members()) for (auto* q : members()) if (p != q) p->aff[q->id] = 15 + rnd() * 30;
    for (auto& hp : pois) if (hp.type == "hermit") {
        auto& h = makePerson(names[6], 0x7b6a52, hp.x + .5f, hp.y + 1.3f);
        usedNames.insert(names[6]);
        h.status = "hermit"; h.age = 54; h.traits = {"loner", "devout"};
        h.gifts = {{"herbalist", 7 + ri(3)}}; h.charisma = 3;
        hp.who = h.id;
    }
    reveal(campX, campY, START_REVEAL);
    reveal(boatX, boatY, START_REVEAL / 2);
    note(cat("The storm spat our little boat onto an unknown island. Five of us: ", listNames(members()), ". A hatchet, a knife, and a sack of seeds."), "big");
    {
        std::vector<std::string> know;
        for (auto* p : members()) if (!p->gifts.empty()) {
            std::string g;
            for (auto& [k, v] : p->gifts) g += (g.empty() ? "" : " and ") + giftInfo().at(k).v;
            know.push_back(p->name + " knows " + g);
        }
        std::vector<std::string> missing;
        for (auto g : {"farmer", "mason", "carpenter", "smith"}) {
            bool any = false;
            for (auto* p : members()) if (p->gifts.count(g)) any = true;
            if (!any) missing.push_back(lower(giftInfo().at(g).n));
        }
        std::string k = know.empty() ? "none of us has a real trade" : "";
        for (size_t i = 0; i < know.size(); i++) k += (i ? "; " : "") + know[i];
        std::string m;
        if (!missing.empty()) { m = " Nobody here is a "; for (size_t i = 0; i < missing.size(); i++) m += (i ? ", or a " : "") + missing[i]; m += "."; }
        note("We all know how to chop wood and lash a shelter together. Beyond that: " + k + "." + m);
    }
    note("Something strange: each of us thinks we heard a voice on the beach. Nobody wants to say it out loud.", "voice");
    for (auto* p : members()) computeTiers(*p);
}

void Sim::reveal(float cx, float cy, float r) {
    bool changed = false;
    float r2 = r * r;
    for (int y = int(std::floor(cy - r)); y <= cy + r; y++) for (int x = int(std::floor(cx - r)); x <= cx + r; x++) {
        if (!inb(x, y)) continue;
        if ((x - cx) * (x - cx) + (y - cy) * (y - cy) > r2) continue;
        if (!explored[idx(x, y)]) { explored[idx(x, y)] = 1; changed = true; }
    }
    if (!changed) return;
    exploredVersion++;
    for (auto& p : pois) if (!p.found && explored[idx(p.x, p.y)]) discover(p);
}

std::string Sim::dirWord(float dx, float dy) {
    float sx = dx - dy, sy = dx + dy;
    float a = std::atan2(sy, sx) * 180 / 3.14159265f;
    return a < -157.5f || a >= 157.5f ? "west" : a < -112.5f ? "northwest" : a < -67.5f ? "north" : a < -22.5f ? "northeast"
         : a < 22.5f ? "east" : a < 67.5f ? "southeast" : a < 112.5f ? "south" : "southwest";
}

} // namespace dw
