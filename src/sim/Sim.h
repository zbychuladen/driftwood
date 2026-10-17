// Driftwood simulation core.
// Pure C++ with no rendering or platform dependencies, so it can run headless
// in tests and be driven by any front end.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace dw {

constexpr int GW = 128, GH = 128;   // tiles; a tile is 2 m, so the island spans about 256 m
// The patch around camp the castaways know from the first morning, in tiles.
constexpr float START_REVEAL = 20;

enum Tile : uint8_t { DEEP = 0, SHALLOW = 1, SAND = 2, GRASS = 3, FOREST = 4, HILL = 5 };
enum Tier { BODY = 0, SAFETY = 1, BELONGING = 2, ESTEEM = 3, PURPOSE = 4 };

struct Person;

struct Action {
    enum Type { WALK, WORK } type = WORK;
    float x = 0, y = 0;                       // walk target (tiles)
    bool hasPath = false;
    std::vector<std::pair<float, float>> path;
    float dur = 0, t = 0;                     // work duration / elapsed (game minutes)
    std::string label, anim;
    std::function<void(Person&)> done, start;
    std::function<bool()> cancel;
};

struct Needs { float hunger = 20, thirst = 30, fatigue = 30, cold = 0, social = 20; };
struct Carry { std::string t; int n = 0; };
struct Bubble { std::string text; double until = 0; };
// What the narrator last said a settler would do: the job holds from `from`
// (later when they haggled) until `until`. Contrary: they are doing the opposite.
struct Narration { std::string job; double until = 0; bool contrary = false; double from = 0; };

// What a person is busy with, so front ends can show the link between them
// and the thing they are working on. fromStock: they fetch materials from the
// stockpile first. toStock: what they gather goes to the stockpile after.
struct Focus {
    enum Kind { NONE, PROJECT, OBJECT, POI, PERSON, STOCK, SPOT } kind = NONE;
    int id = -1;                              // project id, object index, poi index or person id
    float x = 0, y = 0;                       // SPOT only (tiles)
    bool fromStock = false, toStock = false;
};

struct Person {
    int id = 0;
    std::string name;
    uint32_t tunic = 0, skin = 0, hair = 0;
    int hairStyle = 0;
    float x = 0, y = 0;
    std::deque<Action> q;
    std::string kind = "idle", doing = "Looking around", status = "member";
    Focus focus;
    Needs needs;
    float mood = 55;
    std::vector<std::string> traits;
    std::map<std::string, float> sk;          // basic survival skills 1..10
    std::map<std::string, int> gifts;         // specialist talents 1..10
    int charisma = 5;
    float rapport = 40;                       // how willing they are to go along with the narrator, 0..100
    float pressure = 0;                       // narration they have had lately; fades by one a day
    double contraryUntil = 0;                 // contrary until then: narrate them and they do the opposite
    std::unordered_map<int, float> aff;       // affinity toward other people
    int partner = -1;
    bool child = false;
    int age = 25, bornDay = 0;
    int bed = -1;                             // project id
    bool hidden = false, sleeping = false;
    std::string sleepIn;
    float sick = 0, starve = 0;
    float phase = 0;
    bool moving = false;
    int face = 1;
    std::string anim;
    std::optional<Carry> carry;
    std::optional<Bubble> bubble;
    std::optional<Narration> told;
    bool dead = false, gone = false;
    std::vector<int> parents;
    int lastTold = -9, lastAsked = -9;        // days they were last narrated, and last asked the narrator something
    std::array<float, 5> tiers{70, 40, 35, 30, 10};
    std::array<float, 5> gates{1, 1, .4f, .1f, 0};
    int seeking = SAFETY;                     // lowest unmet tier, -1 when fulfilled
    std::string calling;
    float callingNeed = 40, esteemPts = 20;
    std::vector<std::string> titles;
    int goodDays = 0;
    bool inspired = false;
    std::function<void()> release;
    float trust = 0;
    int inventProg = 0, lastBaby = -99;
    bool weary = false;                       // tired of being narrated (rapport ran out once)
    bool has(const char* trait) const;
};

inline float affOf(const Person& p, int id) { auto it = p.aff.find(id); return it == p.aff.end() ? 20.f : it->second; }
inline void affAdd(Person& p, int id, float d) { p.aff[id] = std::max(0.f, std::min(100.f, affOf(p, id) + d)); }

struct Obj {
    int i = 0;
    std::string type;                         // tree, rock, bush, stump, totem
    int x = 0, y = 0;
    bool palm = false, conifer = false, sapling = false, dead = false;
    float size = 1, berries = 0, grow = 0;
    int stone = 6, res = -1, regrow = 0, v = 0;
};

struct ProjectDef {
    std::string key, name;
    int w = 1, h = 1, wood = 0, stone = 0, work = 100, beds = 0;
    float r0 = 3, r1 = 8;
    bool autoPlan = false, food = false;
    std::string gift, req, desc;
    int min = 0;
    std::vector<std::string> like, dislike;
};

struct Project : ProjectDef {
    int id = 0;
    float siteQuality = 1;                    // how good its spot is, 0.6..1.25; scales what it does
    bool sited = false;                       // placed by the player rather than the settlers
    float dw = 0, ds = 0, progress = 0, fuel = 0, growth = 0;
    bool done = false, placed = false, skip = false, voiced = false, waitNoted = false;
    bool holderAtFinish = true;
    std::set<int> workers;
    int x = 0, y = 0, dx = 0, dy = 0, lx = 0, ly = 0, wait = 0;
    std::string state;                        // garden/fields: empty, growing, ripe
};

// Where a building could go: whether it may, how good the spot is, and why.
struct SiteFactor { std::string id, text; float weight; };   // weight > 0 good, < 0 bad
struct SiteReport { bool legal = false; float quality = 1; std::vector<SiteFactor> factors; };
// What one settler thinks of a spot. The more of a specialist they are, the
// closer perceived is to the truth and the more specific what they say.
struct Opinion { int pid; float perceived, expertise; std::string say; };
struct PendingPlacement { int project = -1, sx = 0, sy = 0; };

struct Poi { std::string type; int x = 0, y = 0; bool found = false, joined = false, mentioned = false; int who = -1; };
struct Entry { int d; std::string season, text, cls; };
struct Fate { std::string id, title, desc; };
// A line the narrator can say about a settler: what they do next. job is what
// it sets them doing (rest, food, wood, build, calling, explore, hermit, peace,
// talk); target is the other person in a peace or talk line.
struct Line { std::string job, text; int target = -1; };
// A settler's question to the narrator, with three replies in a fixed order:
// honest, teasing, misleading. truth is what an honest reply says.
struct Reply { std::string kind, text; };
struct Question { int id = 0, pid = -1, about = -1; std::string topic, text; std::vector<Reply> replies; double asked = 0, expires = 0; bool truth = false; };
// A misleading reply that may yet come out.
struct Fib { int pid = -1; double due = 0; std::string reveal; };
struct VoteRow { int pid; bool yes; std::string why; };
struct Vote { std::string name; std::vector<VoteRow> rows; bool pass = false; int y = 0, n = 0; };
struct Wolf { float x, y, sx, sy; std::string state; int face = 1; float ph = 0; };
struct Agenda { int by; std::string gift, job; float str; };
struct Stock { float wood = 4, stone = 0, food = 16; };
struct Shore { int x, y, dx, dy; };

// Pure noise + rng helpers used by world generation.
struct Mulberry { uint32_t a; explicit Mulberry(uint32_t s) : a(s) {} float operator()(); };

class Sim {
public:
    // ---- world ----
    uint32_t seed = 0;
    std::vector<uint8_t> tt, fresh, sea, explored;
    std::vector<int> objAt, bOcc;
    std::vector<float> distSea;
    std::vector<Shore> pondShore, seaShore;
    int exploredVersion = 0;                  // bumps when fog changes (for renderers)

    // ---- state ----
    double tMin = 7 * 60;
    std::deque<Person> people;
    std::vector<Obj> objs;
    std::deque<Project> plan;
    Stock stock;
    std::deque<Entry> journal;
    int journalVersion = 0;
    std::string weather = "clear";
    std::optional<Wolf> wolf;
    int campX = 0, campY = 0, stockX = 0, stockY = 0, pondX = 0, pondY = 0, boatX = 0, boatY = 0;
    std::string villageName;
    float flash = 0;
    int lastHour = 7, lastDay = 1;
    std::set<std::string> firsts, usedNames, inventions;
    std::vector<Poi> pois;
    std::map<std::string, int> mods;          // season-limited modifiers: value = last day active
    std::map<std::string, double> boost;      // words-in-the-sand boosts: value = expiry minute
    int lastProposal = 0, lastSand = 0, newcomerDay = 0, lastRaid = 0, lastSick = 0, lastFeast = 0, totems = 0;
    std::optional<Vote> vote;
    std::vector<Fate> pendingFate;            // non-empty: the front end should pause and ask
    int nextProjId = 0, nextPid = 1, active = -1, fuelRes = -1, dockRes = -1;
    float toolWear = 20, pots = 0, cloth = 0, herbs = 0;
    bool ironTools = false, hasHerb = false;
    std::optional<Agenda> agenda;
    // A specialist building waiting for the player to choose its spot. Set only
    // when askPlacement is on (the game sets it; tests and headless runs leave it
    // off and the settlers choose). step() stands still while it is set.
    std::optional<PendingPlacement> pendingPlacement;
    bool askPlacement = false;

    // ---- api ----
    void newWorld(uint32_t seed);

    // ---- saving ----
    // The whole game as bytes, and back. Settlers' half-done actions are not
    // kept: after a load everyone picks their next task. load() leaves the sim
    // as it was and returns false, with a reason, for anything it cannot read.
    static constexpr int SAVE_VERSION = 4;   // 2: site quality and pending placement; 3: rapport, contrariness, the story circle; 4: lines, questions
    struct SaveInfo { std::string village; int day = 0, year = 0, season = 0, people = 0; uint32_t seed = 0; };
    std::string save() const;
    bool load(const std::string& data, std::string* error = nullptr);
    // Just the summary at the top of a save, for a list of save slots.
    static bool peek(const std::string& data, SaveInfo& out);
    void step(double minutes);                // advance simulation time (game minutes)
    std::vector<Person*> members();
    Person* person(int id);

    // time
    int day() const { return int(tMin / 1440) + 1; }
    double hour() const { double m = tMin - std::floor(tMin / 1440) * 1440; return m / 60; }
    int season() const { return ((day() - 1) / 10) % 4; }
    int yearN() const { return (day() - 1) / 40 + 1; }
    float temp() const;
    std::string clock() const;

    // geometry
    static int idx(int x, int y) { return y * GW + x; }
    static bool inb(int x, int y) { return x >= 0 && y >= 0 && x < GW && y < GH; }
    bool isWater(int x, int y) const { return !inb(x, y) || tt[idx(x, y)] < 2; }
    bool isExplored(float x, float y) const;

    // buildings
    Project* built(const std::string& key);
    std::vector<Project*> builtAll(const std::string& key);
    Project* fire() { return built("fire"); }
    bool fireLit() { auto f = fire(); return f && f->fuel > 0; }
    Project* projectById(int id);
    Project* currentProject();
    Project* activeProject();
    float projScore(Project& b, int i);
    bool prereqOk(const ProjectDef& b);
    Person* giftHolder(const std::string& g, int min);
    bool giftOk(const ProjectDef& b);
    float influence(Person& p);
    struct Champion { Person* p; float push; };
    std::optional<Champion> champion(Project& b);
    int totalBeds(bool plannedToo);
    float foodDays();
    float needOf(const std::string& key);

    // narrator (called by the player)
    // Two or three lines that fit a settler right now. Reading them never
    // changes the game.
    std::vector<Line> linesFor(const Person& p);
    // Says a line. The settler goes along ("agreed"), haggles ("haggled": later),
    // refuses ("refused"), is irritated at a second line in a day ("pushed") or is
    // contrary and does the opposite ("contrary"). lastReply holds the result and
    // lastSaid what they said back.
    std::string narrate(Person& p, const Line& line);
    std::string narrate(Person& p, const std::string& job, Person* target = nullptr);
    // Questions settlers ask the narrator. They wait a day, then lapse.
    std::vector<Question> questions;
    std::vector<Fib> fibs;
    int nextQuestion = 1;
    bool answer(int questionId, int reply);      // false if the question or reply is gone
    void ask(Person& p, const std::string& topic, int about = -1);   // tests and captures: put a question now
    std::string lastSaid;
    std::string writeSand(const std::string& text);
    std::vector<const ProjectDef*> proposalList();
    void propose(const std::string& key);
    void applyFate(const Fate& f);
    float avgRapport();
    // How much narrating a person takes before they turn contrary: easygoing
    // settlers put up with more, obstinate ones with less.
    static float tolerance(const Person& p);
    bool contrary(const Person& p) const { return p.contraryUntil > tMin; }
    std::string lastToldName, lastReply;

    // building sites
    static bool specialist(const ProjectDef& d) { return !d.gift.empty(); }
    SiteReport evaluateSite(const ProjectDef& d, int x, int y) const;
    std::vector<Opinion> opinions(const ProjectDef& d, int x, int y) const;
    std::optional<std::pair<int, int>> suggestSite(const ProjectDef& d);
    bool placeAt(int projectId, int x, int y);   // the player's choice; false if the spot is not allowed
    bool placeSuggested(int projectId);          // "let them choose"
    float siteQ(const std::string& key);         // the built one's site quality, 1 if there is none

    // descriptions for UI
    std::vector<std::string> lacking(Person& p);
    std::string giftText(const Person& p) const;
    static const char* charWord(int c);

    // rng (behavior)
    std::mt19937 gen{12345};
    // when and who asks the narrator things: its own stream, so questions never
    // move the behaviour generator; only the answers change the story
    std::mt19937 talkGen{4321};
    float rnd() { return std::uniform_real_distribution<float>(0.f, 1.f)(gen); }
    int ri(int n) { return n <= 0 ? 0 : int(rnd() * n) % n; }
    template <class T> const T& pick(const std::vector<T>& v) { return v[ri(int(v.size()))]; }

    void note(const std::string& text, const std::string& cls = "");

private:
    mutable std::vector<float> distFresh;     // tiles to the nearest fresh water (built on first use)
    bool siteLegal(const ProjectDef& d, int x, int y) const;
    void startProject(Project& b);
    void siteJudged(Project& b, bool finished);
    void talkHourly();                        // new questions, lapsed ones, fibs coming out

    // world gen
    void placePOI(const std::string& type, std::function<bool(int)> ok, float minD);
    std::vector<Shore> shoreTiles(bool freshOnly);
    Obj& addObj(Obj o);
    void removeObj(Obj& o);
    void reveal(float cx, float cy, float r);
    void discover(Poi& p);

    // people
    Person& makePerson(const std::string& name, uint32_t tunic, float x, float y, bool child = false);
    std::map<std::string, int> rollGifts(const std::vector<std::string>& traits);
    void announceGifts(Person& p);
    void birth(Person& a, Person& b, bool twin = false);
    void assignBeds();
    void die(Person& p);
    void bubble(Person& p, const std::string& text, double mins = 50);
    float computeTiers(Person& p);
    void awaken(Person& p);
    std::string listNames(const std::vector<Person*>& ps);
    std::string newName();

    // projects
    Project& mkProj(const ProjectDef& d, bool voiced = false);
    bool footprintOk(int x, int y, int w, int h);
    bool place(Project& b);
    bool claim(Project& b, int x, int y);
    std::pair<float, float> workSpot(Project& b);
    void finish(Project& b);
    float giftBuild(Person& p, Project& b);
    float giftNeed(const std::string& g);
    void updateAgenda();

    // jobs
    std::vector<std::pair<float, float>> findPath(float sx, float sy, float tx, float ty);
    Action walkA(float x, float y);
    Action workA(float dur, const std::string& label, const std::string& anim, std::function<void(Person&)> done = {});
    float skillF(Person& p, const std::string& k);
    void gain(Person& p, const std::string& k) { p.sk[k] = std::min(10.f, p.sk[k] + .04f); }
    void clearQ(Person& p);
    void setQ(Person& p, const std::string& kind, const std::string& doing, std::deque<Action> q, Focus f = {});
    bool boosted(const std::string& k) { auto it = boost.find(k); return it != boost.end() && it->second > tMin; }
    void plan_(Person& p);
    void workQ(Person& p);
    std::deque<Action> drinkQ(Person& p, Focus* where = nullptr);
    std::deque<Action> eatQ(Person& p);
    std::deque<Action> sleepQ(Person& p, bool sick);
    std::deque<Action> warmQ(Person& p);
    std::deque<Action> fuelQ(Person& p, Project& f);
    std::deque<Action> socialQ(Person& p);
    std::deque<Action> wanderQ(Person& p, bool play = false);
    std::deque<Action> hermitQ(Person& p);
    std::deque<Action> deliver(const std::string& label);
    void chopJob(Person& p);
    void mineJob(Person& p);
    void foodJob(Person& p);
    void buildJob(Person& p, Project& b);
    void farmJob(Person& p, Project& g, const std::string& job);
    void exploreJob(Person& p);
    void visitJob(Person& p, Poi& hp);
    void friendJob(Person& p);
    void craftJob(Person& p, const std::string& gift);
    void callingJob(Person& p);
    void invent(Person& p);
    std::optional<std::pair<int, int>> totemSpot();
    std::vector<std::pair<int, int>> frontier();
    std::string dirWord(float dx, float dy);
    std::string gardenJob(Project& g);
    Obj* nearestObj(const std::function<bool(const Obj&)>& f, float x, float y);
    struct BedSpot { float x, y; bool inside; Project* b; };
    BedSpot bedSpot(Person& p);

    // narrator internals
    std::vector<Fate> drawFates();
    void spawnNewcomers(int n, const std::string& how);
    void decideNewcomer(Person& p);
    void spawnFromCave(Poi& poi);

    // tick
    void updatePerson(Person& p, double dm);
    void hourly();
    void daily();
    void updateWolf(double dm);
};

// Shared tables
const std::vector<ProjectDef>& allProjects();
const ProjectDef* projectDef(const std::string& key);
const std::map<std::string, std::string>& traitNames();
const std::map<std::string, std::string>& skillNames();
struct GiftInfo { std::string n, v, job; };
const std::map<std::string, GiftInfo>& giftInfo();
struct CallingInfo { std::string n; };
const std::map<std::string, CallingInfo>& callingInfo();
struct Invention { std::string k, n, d; };
const std::vector<Invention>& inventionList();
extern const char* TIER_NAMES[5];
extern const char* SEASONS[4];

// Small string builder: cat("Day ", 3, " of ", name)
inline void catOne(std::string& s, const std::string& v) { s += v; }
inline void catOne(std::string& s, const char* v) { s += v; }
inline void catOne(std::string& s, char v) { s += v; }
inline void catOne(std::string& s, int v) { s += std::to_string(v); }
inline void catOne(std::string& s, long v) { s += std::to_string(v); }
inline void catOne(std::string& s, long long v) { s += std::to_string(v); }
inline void catOne(std::string& s, unsigned v) { s += std::to_string(v); }
inline void catOne(std::string& s, size_t v) { s += std::to_string(v); }
inline void catOne(std::string& s, float v) { s += std::to_string(int(std::lround(v))); }
inline void catOne(std::string& s, double v) { s += std::to_string(int(std::lround(v))); }
template <class... A> std::string cat(const A&... a) { std::string s; (catOne(s, a), ...); return s; }
std::string lower(std::string s);

inline float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

} // namespace dw
