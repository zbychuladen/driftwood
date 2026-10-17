// The clock: per-step person updates, hourly mood/social logic and daily events.
#include <algorithm>
#include "Sim.h"

namespace dw {

void Sim::step(double minutes) {
    while (minutes > 0) {
        if (pendingPlacement) return;   // the narrator is choosing a spot
        double dm = std::min(2.0, minutes);
        minutes -= dm;
        tMin += dm;
        if (auto* f = fire(); f && f->fuel > 0) f->fuel = std::max(0.f, f->fuel - float(dm / 90 * (weather == "storm" ? 1.6 : 1)));
        for (size_t i = 0; i < people.size(); i++) updatePerson(people[i], dm);
        for (auto& p : people) if (p.pressure > 0) p.pressure = std::max(0.f, p.pressure - float(dm / 1440));
        updateWolf(dm);
        int h = int(hour());
        if (h != lastHour) { lastHour = h; hourly(); }
        int d = day();
        if (d != lastDay) { lastDay = d; daily(); }
        if (!pendingFate.empty()) return;   // let the front end ask the player
    }
}

void Sim::updatePerson(Person& p, double dmd) {
    if (p.dead) return;
    float dm = float(dmd), hrs = dm / 60;
    auto& n = p.needs;
    Action* a = p.q.empty() ? nullptr : &p.q.front();
    if (p.status == "member") {
        bool asleep = a && a->anim == "sleep" && p.sleeping;
        n.hunger = std::min(100.f, n.hunger + hrs * (p.child ? 2.6f : 3.1f));
        n.thirst = std::min(100.f, n.thirst + hrs * (asleep ? 2.f : 4.8f) * (inventions.count("skins") ? .8f : 1));
        if (!p.calling.empty()) p.callingNeed = std::min(100.f, p.callingNeed + hrs * 1.1f);
        if (asleep) {
            float r = p.sleepIn == "longhouse" ? 16.f : p.sleepIn == "hut" ? 15.f : p.sleepIn == "leanto" ? 11.f : 7.f;
            n.fatigue = std::max(0.f, n.fatigue - hrs * r);
        } else n.fatigue = std::min(100.f, n.fatigue + hrs * (p.sick > 0 ? 2.f : 4.3f));
        n.social = std::min(100.f, n.social + hrs * (p.has("social") ? 3.f : p.has("loner") ? 1.f : 2.f));
        float t = temp();
        auto* f = fire();
        bool nearFire = fireLit() && std::hypot(p.x - (f->x + .5f), p.y - (f->y + .5f)) < 3.2f;
        bool inside = p.hidden && asleep;
        if (nearFire || inside || t >= 12) n.cold = std::max(0.f, n.cold - hrs * (nearFire ? 35.f : inside ? 20.f : 12.f) - (built("bath") ? hrs * 5 : 0));
        else n.cold = std::min(100.f, n.cold + hrs * (12 - t) * .55f * (p.has("tough") ? .55f : 1) * (p.sleepIn == "leanto" && asleep ? .5f : 1)
                                  * (mods.count("wolves") ? .8f : 1) * (1 - std::min(.5f, cloth * .03f)));
        if (p.sick > 0) p.sick -= hrs * (built("herbs") ? 1 + .6f * siteQ("herbs") : 1) * (hasHerb ? 1.4f : 1) * (built("infirmary") ? 1 + .5f * siteQ("infirmary") : 1);
        if (n.hunger >= 100) { p.starve += hrs; if (p.starve > 80) { die(p); return; } } else p.starve = 0;
        static const std::set<std::string> needKinds = {"drink", "eat", "sleep", "rest"};
        if (a && !needKinds.count(p.kind) && (n.thirst > 88 || (n.hunger > 85 && stock.food >= 1) || n.fatigue > 97)) { clearQ(p); p.carry.reset(); plan_(p); return; }
    }
    if (!a) { plan_(p); return; }
    if (a->cancel && a->cancel()) { clearQ(p); plan_(p); return; }
    if (a->type == Action::WALK) {
        if (!a->hasPath) { a->path = findPath(p.x, p.y, a->x, a->y); a->path.push_back({a->x, a->y}); a->hasPath = true; }
        float sp = .075f * dm * (p.child ? .85f : 1) * (p.sick > 0 ? .6f : 1) * (weather == "storm" ? .8f : 1);
        size_t k = 0;
        while (sp > 0 && k < a->path.size()) {
            auto [tx, ty] = a->path[k];
            float dx = tx - p.x, dy = ty - p.y, d = std::hypot(dx, dy), sx = dx - dy;
            if (std::abs(sx) > .02f) p.face = sx > 0 ? 1 : -1;
            if (d <= sp) { p.x = tx; p.y = ty; sp -= d; k++; }
            else { p.x += dx / d * sp; p.y += dy / d * sp; sp = 0; }
        }
        a->path.erase(a->path.begin(), a->path.begin() + k);
        p.moving = true; p.phase += dm * .55f; p.anim.clear();
        if (p.status == "member" && std::fmod(tMin, 30.0) < dm) reveal(p.x, p.y, 4.5f);
        if (a->path.empty()) { p.q.pop_front(); p.moving = false; }
        return;
    }
    p.moving = false;
    if (a->t == 0 && a->start) a->start(p);
    a->t += dm; p.anim = a->anim; p.phase += dm * .3f;
    if (a->t >= a->dur) {
        auto done = a->done;           // copy: the queue may be modified inside done()
        p.q.pop_front();
        p.anim.clear();
        if (done) done(p);
        if (p.q.empty()) p.release = nullptr;
    }
}

void Sim::hourly() {
    int h = int(hour());
    talkHourly();
    auto alive = members();
    if (fire() && (h >= 19 || h < 1)) {
        std::vector<Person*> at;
        for (auto* p : alive) if (p->kind == "social" && p->anim == "sit") at.push_back(p);
        for (auto* a : at) for (auto* b : at) if (a != b) {
            float k = (a->has("social") ? 1.5f : 1) * (a->has("loner") ? .5f : 1) * (mods.count("love") ? 2.f : 1) * (boosted("peace") ? 1.5f : 1);
            affAdd(*a, b->id, 2.2f * k);
        }
        Person* mus = nullptr; Person* sto = nullptr;
        for (auto* p : at) { if (p->calling == "musician") mus = p; if (p->calling == "storyteller") sto = p; }
        if (mus) {
            if (rnd() < .6f) bubble(*mus, "~ music ~", 55);
            mus->callingNeed = std::max(0.f, mus->callingNeed - 15);
            for (auto* a : at) if (a != mus) { a->mood = std::min(100.f, a->mood + 1.5f); a->needs.social = std::max(0.f, a->needs.social - 4); }
        }
        if (sto) {
            sto->callingNeed = std::max(0.f, sto->callingNeed - 12);
            for (auto* a : at) if (a != sto) affAdd(*a, sto->id, 1);
            static const std::vector<std::string> L = {"...and then the wave hit", "once, long ago...", "listen closely now"};
            if (rnd() < .3f) bubble(*sto, pick(L), 50);
        }
        static const std::vector<std::string> chat = {"ha!", "remember when...", "listen to this", "look, stars", "did you hear it too?", "I miss bread", "the sea is loud tonight"};
        if (!mus && !sto && at.size() >= 2 && rnd() < .35f) bubble(*pick(at), pick(chat), 45);
    }
    for (auto* p : alive) {
        auto& n = p->needs;
        float m = computeTiers(*p);
        if (built("circle")) m += 2;
        if (built("bath")) m += 5 * siteQ("bath");
        if (fireLit() && (h >= 18 || h < 6)) m += 3;
        if (p->has("cheerful")) m += 8;
        if (p->has("gloomy")) m -= 8;
        if (weather == "rain") m -= 3;
        if (weather == "storm") m -= 6;
        if (inventions.count("lamp") && (h >= 18 || h < 6)) m += 2;
        if (boosted("joy")) m += 6;
        if (p->inspired) m += 4;
        p->inspired = !p->calling.empty() && p->tiers[PURPOSE] > 70 && p->gates[4] > .6f;
        p->mood = clampf(lerpf(p->mood, m, .15f), 0, 100);
        if (p->mood < 22 && !p->sleeping && !p->child && rnd() < .025f) {
            std::vector<Person*> o;
            for (auto* q : alive) if (q != p && !q->sleeping && std::hypot(q->x - p->x, q->y - p->y) < 5) o.push_back(q);
            if (!o.empty()) {
                auto* q = pick(o);
                affAdd(*p, q->id, -12); affAdd(*q, p->id, -8);
                bubble(*p, "leave me alone!");
                note(cat(p->name, " snapped at ", q->name, ". Everyone is tired and hungry."));
            }
        }
        if (n.cold > 80 && p->sick <= 0 && rnd() < (built("herbs") ? .01f : .02f)) {
            lastSick = day();
            p->sick = herbs > 0 ? 15.f : 30.f;
            if (herbs > 0) herbs--;
            clearQ(*p);
            note(cat(p->name, " came down with a chill and a fever."), "sad");
        }
    }
    if (weather == "storm" && rnd() < .25f) flash = 1;
    bool wolfSeason = season() == 3 || (season() == 2 && (day() - 1) % 10 > 6) || mods.count("wolves");
    if (wolfSeason && (h == 23 || h == 2) && !wolf && stock.food > 4 && rnd() < (mods.count("wolves") ? .3f : .12f)) {
        auto fr = frontier();
        float sx = campX - 10.f, sy = campY + 0.f;
        if (!fr.empty()) { auto f = pick(fr); sx = f.first + .5f; sy = f.second + .5f; }
        wolf = Wolf{sx, sy, sx, sy, "in"};
        if (built("tower") || (mods.count("guard") && mods["guard"] >= day())) {
            wolf->state = "scared";
            if (!firsts.count("towerWolf") || rnd() < .15f) { firsts.insert("towerWolf"); note("The watch spotted a wolf at the tree line and drove it off with shouts and a torch."); }
        }
    }
}

void Sim::daily() {
    int d = day(), s = season(), prevS = ((d - 2) / 10) % 4;
    auto m = members();
    for (auto* p : m) if (p->contraryUntil > 0 && !contrary(*p)) { p->contraryUntil = 0; note(cat(p->name, " has stopped bristling at the narrator, and will hear a line again.")); }
    // a well-placed store or smokehouse keeps food better; a damp or out-of-the-way one worse
    float rate = (built("smoke") ? .006f * (2 - siteQ("smoke")) : built("store") ? .01f * (2 - siteQ("store")) : built("rack") ? .035f : .07f) * (1 - std::min(.6f, pots * .04f));
    stock.food -= std::floor(stock.food * rate);
    float grow = mods.count("bounty") && mods["bounty"] >= d ? 2.f : 1.f;
    for (auto& o : objs) {
        if (o.dead) continue;
        if (o.type == "bush" && s != 3) o.berries = std::min(5.f, o.berries + (s == 2 ? .6f : 1.1f) * grow);
        if (o.type == "stump" && d >= o.regrow) { o.type = "tree"; o.sapling = true; o.grow = 0; }
        if (o.type == "tree" && o.sapling) { o.grow += .2f; if (o.grow >= 1) o.sapling = false; }
    }
    for (auto key : {"garden", "fields"}) {
        auto* g = built(key);
        if (!g || g->state != "growing") continue;
        g->growth += (s <= 1 ? .19f : s == 2 ? .12f : 0) * grow * g->siteQuality;
        if (s == 3) { g->state = "empty"; g->growth = 0; note("Frost blackened what was left in the fields.", "sad"); }
        if (g->growth >= 1) g->state = "ripe";
    }
    bool wasStorm = weather == "storm";
    float r = rnd();
    if (mods.count("calm") && mods["calm"] >= d) weather = s == 3 && r < .4f ? "snow" : "clear";
    else if (s == 3) weather = r < .45f ? "snow" : r < .55f ? "storm" : "clear";
    else if (s == 2) weather = r < .3f ? "rain" : r < .45f ? "storm" : "clear";
    else if (s == 0) weather = r < .3f ? "rain" : "clear";
    else weather = r < .1f ? "rain" : r < .15f ? "storm" : "clear";
    static const std::vector<std::string> storms = {"A storm rolled in off the sea. We huddled and waited.", "Thunder all day. The fish are hiding.", "Wind tore at the roofs. Everyone held their breath."};
    if (weather == "storm" && rnd() < .5f) note(pick(storms));
    if (wasStorm && rnd() < .3f && d - newcomerDay > 4) { newcomerDay = d; spawnNewcomers(1 + (rnd() < .35f ? 1 : 0), "wreck"); }
    auto* sig = built("signal"); auto* lh = built("lighthouse");
    if ((sig || lh) && d - newcomerDay > 5 && rnd() < (lh ? .2f : .09f)) { newcomerDay = d; spawnNewcomers(lh ? 2 + ri(2) : 1 + ri(2), lh ? "light" : "signal"); }
    if (built("circle")) for (auto* p : m) p->rapport = std::min(100.f, p->rapport + .6f);
    if (s != prevS && d > 1) {
        std::string spring = "Spring returned. Green shoots everywhere.";
        if (yearN() > 1) spring += cat(" We have survived ", yearN() - 1, yearN() > 2 ? " years" : " year", " on this island.");
        const std::string lines[4] = {spring, "Summer. Long warm days and calm seas.", "Autumn. The leaves are turning gold and red. We must fill the stores before winter.", "Winter came. Snow on the hills and frost on the pond. No more berries until spring."};
        note(lines[s], "big");
    }
    if ((s != prevS && d > 1) || d == 3) pendingFate = drawFates();
    float days = foodDays();
    if (s >= 2 && days < 4 && d % 2 == 0) note(cat("Only ", int(stock.food), " portions of food left. Everyone is anxious."), "sad");
    hasHerb = false;
    for (auto* q : m) if (!q->child && q->gifts.count("herbalist")) hasHerb = true;
    pots = std::max(0.f, pots - .15f);
    cloth = std::max(0.f, cloth - .1f);
    updateAgenda();
    for (auto* p : m) {
        p->esteemPts = std::max(0.f, p->esteemPts - 1.5f);
        if (!p->child && p->calling.empty()) {
            if (p->gates[3] > .6f) p->goodDays++; else p->goodDays = std::max(0, p->goodDays - 1);
            if (p->goodDays >= 3) awaken(*p);
        }
        for (auto& [k, v] : p->sk)
            if (v >= 7 && std::find(p->titles.begin(), p->titles.end(), k) == p->titles.end()) {
                p->titles.push_back(k);
                p->esteemPts = std::min(100.f, p->esteemPts + 20);
                note(cat("The others have started calling ", p->name, " our master of ", skillNames().at(k), ". They pretended not to be pleased."), "big");
            }
    }
    if (days > 12 && d - lastFeast >= 8 && s != 3 && m.size() >= 3 && stock.food > m.size() * 3) {
        lastFeast = d;
        stock.food -= m.size() * 2;
        for (auto* p : m) { p->mood = std::min(100.f, p->mood + 12); p->needs.social = 0; for (auto* q : m) if (q != p) affAdd(*p, q->id, 3); }
        static const std::vector<std::string> F = {"With the stores full, we held a feast. Roast fish, berry mash, and dancing on the sand until the fire died.", "A feast tonight! Someone drummed on a log and even the shyest of us danced.", "We feasted to give thanks for full stores. For one night nobody worried about anything."};
        note(pick(F), "big");
    }
    auto kin = [](Person* a, Person* b) {
        for (int x : a->parents) { if (x == b->id) return true; for (int y : b->parents) if (x == y) return true; }
        for (int y : b->parents) if (y == a->id) return true;
        return false;
    };
    std::vector<Person*> free;
    for (auto* p : m) if (!p->child && p->partner < 0) free.push_back(p);
    for (auto* a : free) for (auto* b : free) {
        if (a->id >= b->id || a->partner >= 0 || b->partner >= 0 || kin(a, b)) continue;
        if (affOf(*a, b->id) > 78 && affOf(*b, a->id) > 78 && rnd() < .3f) {
            a->partner = b->id; b->partner = a->id;
            note(cat(a->name, " and ", b->name, " have been inseparable lately. Tonight they announced they are together."), "big");
        }
    }
    int kids = 0;
    for (auto* c : m) if (c->child) kids++;
    for (auto* a : m) {
        if (a->child || a->partner < 0 || a->id > a->partner) continue;
        auto* b = person(a->partner);
        if (!b || b->dead) continue;
        if (a->mood > 50 && b->mood > 50 && days > 3 && totalBeds(false) > int(m.size()) && kids < 4 && d - a->lastBaby > 18 && rnd() < .05f) { a->lastBaby = d; birth(*a, *b); kids++; }
    }
    int grownAt = built("school") ? int(std::lround(40 - 15 * siteQ("school"))) : 40;
    for (auto* c : m) if (c->child && d - c->bornDay >= grownAt) {
        c->child = false; c->age = 16; c->gifts = rollGifts(c->traits);
        note(cat(c->name, " is old enough to help with the work now."), "big");
        announceGifts(*c);
    }
    bool pendingHut = false;
    for (auto& b : plan) if (b.key == "hut" && !b.done && !b.skip) pendingHut = true;
    if (totalBeds(true) < int(m.size()) && !pendingHut) mkProj(*projectDef("hut"));
    bool haveCircle = false;
    for (auto& b : plan) if (b.key == "circle") haveCircle = true;
    if (avgRapport() > 75 && !haveCircle) { mkProj(*projectDef("circle"), true); note("They talk back to the narrator now, and laugh about it. Tonight they decided to make a story circle, to tell the island's tales out loud.", "narrator"); }
    for (auto* p : m) if (p->rapport < 12 && !p->weary) { p->weary = true; note(cat(p->name, " is tired of being narrated. \"Tell someone else's story for a while.\"")); }
    bool allDone = true;
    for (auto& b : plan) if (!b.done && !b.skip && giftOk(b) && prereqOk(b)) allDone = false;
    if (allDone && !firsts.count("complete")) { firsts.insert("complete"); note(cat("Everything we can build right now is built. ", villageName.empty() ? "The camp" : villageName, " is a real village. Perhaps the Voice has new ideas."), "big"); }
}

void Sim::updateWolf(double dmd) {
    if (!wolf) return;
    float dm = float(dmd), sp = .1f * dm;
    auto& w = *wolf;
    w.ph += dm * .6f;
    if (w.state == "scared" || w.state == "out") {
        float dx = w.sx - w.x, dy = w.sy - w.y, d = std::hypot(dx, dy);
        if (d < .5f) { wolf.reset(); return; }
        w.x += dx / d * sp * 1.3f; w.y += dy / d * sp * 1.3f; w.face = dx - dy > 0 ? 1 : -1;
        return;
    }
    float tx = stockX + 1.f, ty = stockY + .5f, dx = tx - w.x, dy = ty - w.y, d = std::hypot(dx, dy);
    w.face = dx - dy > 0 ? 1 : -1;
    for (auto* p : members()) if (!p->sleeping && !p->child && std::hypot(p->x - w.x, p->y - w.y) < 4) {
        w.state = "scared";
        p->esteemPts = std::min(100.f, p->esteemPts + 10);
        bubble(*p, "shoo! get out!");
        note(cat(p->name, " chased a wolf away from camp, waving a burning branch."));
        return;
    }
    if (d < .6f) {
        int n = std::min(int(stock.food), 4 + ri(4));
        if (n > 0) { stock.food -= n; lastRaid = day(); note(cat("A wolf crept into camp in the night and dragged off ", n, " portions of food."), "sad"); }
        w.state = "out";
        return;
    }
    w.x += dx / d * sp; w.y += dy / d * sp;
}

} // namespace dw
