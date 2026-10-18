// Saving and loading the whole simulation as bytes.
//
// One function per type lists its fields, and the same list both writes and
// reads (the archive decides which), so the two can never drift apart. Numbers
// are stored in the machine's own layout, little-endian on every platform the
// game ships for. A settler's queued actions hold code and are not stored: on
// load everyone simply picks their next task.
#include <cstring>
#include <sstream>
#include "sim/Sim.h"

namespace dw {

namespace {

const char MAGIC[] = "DRIFTWOOD SAVE\n";

struct Writer {
    static constexpr bool reading = false;
    std::string out;
    bool ok = true;
    template <class T> void pod(T& v) { out.append(reinterpret_cast<const char*>(&v), sizeof v); }
};

struct Reader {
    static constexpr bool reading = true;
    const std::string& in;
    size_t at = 0;
    bool ok = true;
    explicit Reader(const std::string& s) : in(s) {}
    template <class T> void pod(T& v) {
        if (!ok || at + sizeof v > in.size()) { ok = false; v = T{}; return; }
        std::memcpy(&v, in.data() + at, sizeof v);
        at += sizeof v;
    }
    size_t left() const { return in.size() - at; }
};

// No container in a real game comes near this; a bigger count means a damaged file.
const uint32_t MAX_COUNT = 20'000'000;

// ---- declarations first, so containers of any of these can find them
template <class A> void io(A& a, Shore& v);
template <class A> void io(A& a, Needs& v);
template <class A> void io(A& a, Carry& v);
template <class A> void io(A& a, Bubble& v);
template <class A> void io(A& a, Narration& v);
template <class A> void io(A& a, Focus& v);
template <class A> void io(A& a, Person& v);
template <class A> void io(A& a, Obj& v);
template <class A> void io(A& a, Project& v);
template <class A> void io(A& a, Poi& v);
template <class A> void io(A& a, Entry& v);
template <class A> void io(A& a, Fate& v);
template <class A> void io(A& a, VoteRow& v);
template <class A> void io(A& a, Vote& v);
template <class A> void io(A& a, Wolf& v);
template <class A> void io(A& a, Agenda& v);
template <class A> void io(A& a, Stock& v);
template <class A> void io(A& a, PendingPlacement& v);
template <class A> void io(A& a, Reply& v);
template <class A> void io(A& a, Question& v);
template <class A> void io(A& a, Fib& v);
template <class A> void io(A& a, Duty& v);
template <class A> void io(A& a, Household& v);

// ---- plain values
template <class A> void io(A& a, int& v) { a.pod(v); }
template <class A> void io(A& a, uint32_t& v) { a.pod(v); }
template <class A> void io(A& a, uint8_t& v) { a.pod(v); }
template <class A> void io(A& a, float& v) { a.pod(v); }
template <class A> void io(A& a, double& v) { a.pod(v); }
template <class A> void io(A& a, bool& v) { uint8_t b = v; a.pod(b); v = b != 0; }
template <class A> void io(A& a, std::string& s) {
    uint32_t n = uint32_t(s.size());
    a.pod(n);
    if constexpr (A::reading) {
        if (!a.ok || n > a.left()) { a.ok = false; return; }
        s.assign(a.in.data() + a.at, n);
        a.at += n;
    } else a.out.append(s);
}

// ---- containers
template <class A> bool count(A& a, uint32_t& n) {
    a.pod(n);
    if (A::reading && (!a.ok || n > MAX_COUNT)) { a.ok = false; return false; }
    return true;
}
template <class A, class T> void io(A& a, std::vector<T>& v) {
    uint32_t n = uint32_t(v.size());
    if (!count(a, n)) return;
    if constexpr (A::reading) v.assign(n, T{});
    for (auto& e : v) { io(a, e); if (!a.ok) return; }
}
template <class A, class T> void io(A& a, std::deque<T>& v) {
    uint32_t n = uint32_t(v.size());
    if (!count(a, n)) return;
    if constexpr (A::reading) v.assign(n, T{});
    for (auto& e : v) { io(a, e); if (!a.ok) return; }
}
template <class A, class T, size_t N> void io(A& a, std::array<T, N>& v) { for (auto& e : v) io(a, e); }
template <class A, class T> void io(A& a, std::set<T>& v) {
    uint32_t n = uint32_t(v.size());
    if (!count(a, n)) return;
    if constexpr (A::reading) {
        v.clear();
        for (uint32_t i = 0; i < n && a.ok; i++) { T e{}; io(a, e); v.insert(e); }
    } else for (auto e : v) io(a, e);
}
template <class A, class K, class V> void io(A& a, std::map<K, V>& m) {
    uint32_t n = uint32_t(m.size());
    if (!count(a, n)) return;
    if constexpr (A::reading) {
        m.clear();
        for (uint32_t i = 0; i < n && a.ok; i++) { K k{}; V v{}; io(a, k); io(a, v); m[k] = v; }
    } else for (auto& [k, v] : m) { K kk = k; io(a, kk); io(a, v); }
}
// written in key order, so the same game always makes the same bytes
template <class A, class K, class V> void io(A& a, std::unordered_map<K, V>& m) {
    std::map<K, V> sorted(m.begin(), m.end());
    io(a, sorted);
    if constexpr (A::reading) m = std::unordered_map<K, V>(sorted.begin(), sorted.end());
}
template <class A, class T> void io(A& a, std::optional<T>& v) {
    bool has = v.has_value();
    io(a, has);
    if constexpr (A::reading) { if (has) { v.emplace(); io(a, *v); } else v.reset(); }
    else if (has) io(a, *v);
}

// ---- the game's types
template <class A> void io(A& a, Shore& v) { io(a, v.x); io(a, v.y); io(a, v.dx); io(a, v.dy); }
template <class A> void io(A& a, Needs& v) { io(a, v.hunger); io(a, v.thirst); io(a, v.fatigue); io(a, v.cold); io(a, v.social); }
template <class A> void io(A& a, Carry& v) { io(a, v.t); io(a, v.n); }
template <class A> void io(A& a, Bubble& v) { io(a, v.text); io(a, v.until); }
template <class A> void io(A& a, Narration& v) { io(a, v.job); io(a, v.until); io(a, v.contrary); io(a, v.from); }
template <class A> void io(A& a, Reply& v) { io(a, v.kind); io(a, v.text); }
template <class A> void io(A& a, Question& v) {
    io(a, v.id); io(a, v.pid); io(a, v.about); io(a, v.topic); io(a, v.text); io(a, v.replies); io(a, v.asked); io(a, v.expires); io(a, v.truth);
}
template <class A> void io(A& a, Fib& v) { io(a, v.pid); io(a, v.due); io(a, v.reveal); }
template <class A> void io(A& a, Duty& v) { io(a, v.job); io(a, v.until); io(a, v.role); }
template <class A> void io(A& a, Household& v) { io(a, v.id); io(a, v.members); io(a, v.home); io(a, v.rising); io(a, v.falling); io(a, v.noRoom); io(a, v.toil); }
template <class A> void io(A& a, Focus& v) {
    int k = int(v.kind);
    io(a, k);
    v.kind = Focus::Kind(std::clamp(k, 0, int(Focus::SPOT)));
    io(a, v.id); io(a, v.x); io(a, v.y); io(a, v.fromStock); io(a, v.toStock);
}
template <class A> void io(A& a, Person& p) {
    io(a, p.id); io(a, p.name); io(a, p.tunic); io(a, p.skin); io(a, p.hair); io(a, p.hairStyle); io(a, p.x); io(a, p.y);
    io(a, p.kind); io(a, p.doing); io(a, p.status); io(a, p.focus); io(a, p.needs); io(a, p.mood); io(a, p.traits);
    io(a, p.sk); io(a, p.gifts); io(a, p.giftXp); io(a, p.charisma); io(a, p.buyIn); io(a, p.aff); io(a, p.partner); io(a, p.child);
    io(a, p.age); io(a, p.bornDay); io(a, p.bed); io(a, p.hidden); io(a, p.sleeping); io(a, p.sleepIn); io(a, p.sick);
    io(a, p.starve); io(a, p.phase); io(a, p.moving); io(a, p.face); io(a, p.anim); io(a, p.carry); io(a, p.bubble);
    io(a, p.told); io(a, p.dead); io(a, p.gone); io(a, p.parents); io(a, p.lastTold); io(a, p.lastAsked); io(a, p.duty); io(a, p.resentment); io(a, p.lastStatus); io(a, p.tiers); io(a, p.gates);
    io(a, p.seeking); io(a, p.calling); io(a, p.callingNeed); io(a, p.esteemPts); io(a, p.titles); io(a, p.goodDays);
    io(a, p.inspired); io(a, p.trust); io(a, p.inventProg); io(a, p.lastBaby); io(a, p.weary); io(a, p.pressure); io(a, p.contraryUntil);
}
template <class A> void io(A& a, Obj& o) {
    io(a, o.i); io(a, o.type); io(a, o.x); io(a, o.y); io(a, o.palm); io(a, o.conifer); io(a, o.sapling); io(a, o.dead);
    io(a, o.size); io(a, o.berries); io(a, o.grow); io(a, o.stone); io(a, o.res); io(a, o.regrow); io(a, o.v);
}
template <class A> void io(A& a, Project& b) {
    io(a, b.key); io(a, b.name); io(a, b.w); io(a, b.h); io(a, b.wood); io(a, b.stone); io(a, b.work); io(a, b.beds);
    io(a, b.r0); io(a, b.r1); io(a, b.autoPlan); io(a, b.food); io(a, b.gift); io(a, b.req); io(a, b.desc); io(a, b.min);
    io(a, b.like); io(a, b.dislike);
    io(a, b.id); io(a, b.dw); io(a, b.ds); io(a, b.progress); io(a, b.fuel); io(a, b.growth); io(a, b.done); io(a, b.placed);
    io(a, b.skip); io(a, b.voiced); io(a, b.waitNoted); io(a, b.holderAtFinish); io(a, b.workers); io(a, b.x); io(a, b.y);
    io(a, b.dx); io(a, b.dy); io(a, b.lx); io(a, b.ly); io(a, b.wait); io(a, b.state); io(a, b.siteQuality); io(a, b.sited);
}
template <class A> void io(A& a, Poi& v) { io(a, v.type); io(a, v.x); io(a, v.y); io(a, v.found); io(a, v.joined); io(a, v.mentioned); io(a, v.who); }
template <class A> void io(A& a, Entry& v) { io(a, v.d); io(a, v.season); io(a, v.text); io(a, v.cls); }
template <class A> void io(A& a, Fate& v) { io(a, v.id); io(a, v.title); io(a, v.desc); }
template <class A> void io(A& a, VoteRow& v) { io(a, v.pid); io(a, v.yes); io(a, v.why); }
template <class A> void io(A& a, Vote& v) { io(a, v.name); io(a, v.rows); io(a, v.pass); io(a, v.y); io(a, v.n); }
template <class A> void io(A& a, Wolf& v) { io(a, v.x); io(a, v.y); io(a, v.sx); io(a, v.sy); io(a, v.state); io(a, v.face); io(a, v.ph); }
template <class A> void io(A& a, Agenda& v) { io(a, v.by); io(a, v.gift); io(a, v.job); io(a, v.str); }
template <class A> void io(A& a, Stock& v) { io(a, v.wood); io(a, v.stone); io(a, v.food); }
template <class A> void io(A& a, PendingPlacement& v) { io(a, v.project); io(a, v.sx); io(a, v.sy); }

template <class A> void io(A& a, Sim::SaveInfo& v) { io(a, v.village); io(a, v.day); io(a, v.year); io(a, v.season); io(a, v.people); io(a, v.seed); }

// Everything the game is, in one list.
template <class A> void body(A& a, Sim& s) {
    io(a, s.seed); io(a, s.tt); io(a, s.fresh); io(a, s.sea); io(a, s.explored); io(a, s.objAt); io(a, s.bOcc);
    io(a, s.distSea); io(a, s.pondShore); io(a, s.seaShore); io(a, s.exploredVersion);
    io(a, s.tMin); io(a, s.people); io(a, s.objs); io(a, s.plan); io(a, s.stock); io(a, s.journal); io(a, s.journalVersion);
    io(a, s.weather); io(a, s.wolf); io(a, s.campX); io(a, s.campY); io(a, s.stockX); io(a, s.stockY); io(a, s.pondX);
    io(a, s.pondY); io(a, s.boatX); io(a, s.boatY); io(a, s.villageName); io(a, s.flash); io(a, s.lastHour); io(a, s.lastDay);
    io(a, s.firsts); io(a, s.usedNames); io(a, s.inventions); io(a, s.pois); io(a, s.mods); io(a, s.boost);
    io(a, s.lastProposal); io(a, s.lastSand); io(a, s.newcomerDay); io(a, s.lastRaid); io(a, s.lastSick); io(a, s.lastFeast);
    io(a, s.totems); io(a, s.vote); io(a, s.pendingFate); io(a, s.nextProjId); io(a, s.nextPid); io(a, s.active);
    io(a, s.fuelRes); io(a, s.dockRes); io(a, s.toolWear); io(a, s.goods); io(a, s.foodSeen); io(a, s.ironTools);
    io(a, s.hasHerb); io(a, s.agenda); io(a, s.lastToldName); io(a, s.lastReply); io(a, s.pendingPlacement);
    io(a, s.questions); io(a, s.fibs); io(a, s.nextQuestion); io(a, s.lastSaid); io(a, s.lostThisSeason); io(a, s.rapport); io(a, s.households); io(a, s.nextHousehold); io(a, s.nextShip);
    // the behaviour random generator, so a loaded game goes on the same way every time
    std::string rng;
    if constexpr (!A::reading) { std::ostringstream o; o << s.gen; rng = o.str(); }
    io(a, rng);
    if constexpr (A::reading) { std::istringstream i(rng); i >> s.gen; if (i.fail()) a.ok = false; }
    std::string talk;
    if constexpr (!A::reading) { std::ostringstream o; o << s.talkGen; talk = o.str(); }
    io(a, talk);
    if constexpr (A::reading) { std::istringstream i(talk); i >> s.talkGen; if (i.fail()) a.ok = false; }
}

Sim::SaveInfo infoOf(const Sim& s) {
    int people = 0;
    for (auto& p : s.people) if (!p.dead && p.status == "member") people++;
    return {s.villageName, s.day(), s.yearN(), s.season(), people, s.seed};
}

// The header: magic, version, the slot list's summary.
template <class A> bool header(A& a, int& version, Sim::SaveInfo& info) {
    std::string magic = MAGIC;
    io(a, magic);
    if (A::reading && magic != MAGIC) return false;
    io(a, version);
    io(a, info);
    return a.ok;
}

} // namespace

std::string Sim::save() const {
    Writer w;
    int version = SAVE_VERSION;
    SaveInfo info = infoOf(*this);
    header(w, version, info);
    body(w, const_cast<Sim&>(*this));   // the writer only reads the fields
    return w.out;
}

bool Sim::peek(const std::string& data, SaveInfo& out) {
    Reader r(data);
    int version = 0;
    return header(r, version, out) && version == SAVE_VERSION;
}

bool Sim::load(const std::string& data, std::string* error) {
    auto fail = [&](const char* why) { if (error) *error = why; return false; };
    Reader r(data);
    int version = 0;
    SaveInfo info;
    if (!header(r, version, info)) return fail("This is not a Driftwood save, or it is damaged.");
    if (version != SAVE_VERSION) return fail(version > SAVE_VERSION ? "This save comes from a newer version of Driftwood." : "This save comes from an older version of Driftwood.");
    Sim s;
    body(r, s);
    if (!r.ok) return fail("The save is damaged or cut short.");
    if (r.left() != 0) return fail("The save has unexpected data at the end.");
    // the world must be the size this game is built for
    const size_t N = size_t(GW) * GH;
    if (s.tt.size() != N || s.explored.size() != N || s.objAt.size() != N || s.bOcc.size() != N || s.distSea.size() != N)
        return fail("The save is for an island of a different size.");
    // half-done actions and reservations held code; everyone starts their next task fresh
    for (auto& p : s.people) {
        p.q.clear();
        p.release = nullptr;
        p.focus = {};
        p.anim.clear();
        p.moving = false;
        if (!p.dead && p.status == "member") { p.kind = "idle"; p.doing = "Getting their bearings"; }
    }
    for (auto& o : s.objs) o.res = -1;
    s.fuelRes = s.dockRes = -1;
    *this = std::move(s);
    return true;
}

} // namespace dw
