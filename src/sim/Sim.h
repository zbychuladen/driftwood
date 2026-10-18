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
// Work a settler agreed to take on for longer than a day: for the season
// (until is when it ends) or as an appointed role (role, until 0: until revoked).
struct Duty { std::string job; double until = 0; bool role = false; };

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
    std::map<std::string, float> giftXp;      // practice towards the next level of each gift
    int charisma = 5;
    float buyIn = 40;                         // how much they buy into the narrator, 0..100: sets prices and whether they go along
                    // how willing they are to go along with the narrator, 0..100
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
    std::optional<Duty> duty;
    bool dead = false, gone = false;
    std::vector<int> parents;
    int lastTold = -9, lastAsked = -9;
    float resentment = 0;                     // at how much better others live, 0..100
    float follow = 0;                         // how far they follow the rival rather than the narrator, 0..100
    int lastStatus = 0;                       // their household's status, as of this morning (for prices)        // days they were last narrated, and last asked the narrator something
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
    bool weary = false;                       // tired of being narrated (their buy-in ran out once)
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
    bool upgrade = false;                     // a home level reached only by improving a home (cottage, house, manor)
    bool commissioned = false;                // raised only when a notable commissions it (a monument)
    bool byHand = false;                      // only ever drawn up by the narrator: decorations, terraforming
    std::string gift, req, desc;
    int min = 0;
    std::vector<std::string> like, dislike;
};

struct Project : ProjectDef {
    int id = 0;
    float siteQuality = 1;                    // how good its spot is, 0.6..1.25; scales what it does
    float craft = 1, craftW = 0;              // workmanship, 0.75..1.25 (how well it was built), and the work it is averaged over
    std::string customName;                   // a name the narrator gave it
    int style = 0;                            // which of its looks (roof and colours), changed by the narrator
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
struct PendingPlacement { int project = -1, sx = 0, sy = 0; bool chosen = false; };   // chosen: from the build menu, so Esc cancels it

struct Poi { std::string type; int x = 0, y = 0; bool found = false, joined = false, mentioned = false; int who = -1; };
struct Entry { int d; std::string season, text, cls; };
struct Fate { std::string id, title, desc; };
// A line the narrator can say about a settler: what they do next. job is what
// it sets them doing (rest, food, wood, build, calling, explore, hermit, peace,
// talk); target is the other person in a peace or talk line.
struct Line { std::string job, text; int target = -1, cost = 8; float fit = 1; };   // cost: the narrator's rapport it takes if they go along
// A settler's question to the narrator, with three replies in a fixed order:
// honest, teasing, misleading. truth is what an honest reply says.
struct Reply { std::string kind, text; };
struct Question { int id = 0, pid = -1, about = -1; std::string topic, text; std::vector<Reply> replies; double asked = 0, expires = 0; bool truth = false; };
// A misleading reply that may yet come out.
struct Fib { int pid = -1; double due = 0; std::string reveal; };
// The Narrator's Ledger: something going wrong, who could fix it and at what
// price (cheapest first), and a longer-term fix. job is what a candidate would
// be asked to do; empty when no one can simply be asked (a missing herbalist).
struct Candidate { int pid = -1; int cost = 0; std::string why; };
struct Gap { std::string id, text, job, fix; std::vector<Candidate> candidates; std::string key; };   // key: id plus what it is about, stable while it lasts
// A household: a couple and their children, or one person on their own. Its
// home is a project id; rising and falling count the days its needs have been
// met for the next level, or lapsed for its own.
struct Household { int id = 0; std::vector<int> members; int home = -1; int rising = 0, falling = 0; bool noRoom = false; float toil = 0; };   // toil: a notable household's recent common work
struct HomeNeed { std::string id, text; bool met = false; float have = -1, need = 0; };   // have/need: how close, for the countable ones (-1: a yes or no)
struct HomeCost { int wood = 0, stone = 0; };
// A household's petition to the narrator: a building, a better home, a feast or
// a luxury, by a day. hh is the household, pid who asked.
struct Petition { int id = 0, hh = -1, pid = -1; std::string kind, key, text; int due = 0; };
// A trial foreseen for a year: when it starts, how ready the village was then,
// and whether it is under way or over.
struct Trial { std::string kind; int year = 1, start = 0; float ready = 0; bool active = false, over = false; };
// A year in review: homes by level, people, births and deaths, petitions met
// and lapsed, the trial and how ready the village was, the rival, the buy-in.
struct YearSummary { int year = 1; int homes[5] = {0, 0, 0, 0, 0}; int people = 0, births = 0, deaths = 0, met = 0, lapsed = 0; std::string trial; float ready = 0, rival = -1, buyIn = 0; };
struct VoteRow { int pid; bool yes; std::string why; };
struct Vote { std::string name; std::vector<VoteRow> rows; bool pass = false; int y = 0, n = 0; };
struct Wolf { float x, y, sx, sy; std::string state; int face = 1; float ph = 0; };
struct Agenda { int by; std::string gift, job; float str; };
struct Stock { float wood = 4, stone = 0, food = 16; };
struct Shore { int x, y, dx, dy; };

// Pure noise + rng helpers used by world generation.
struct Mulberry { uint32_t a; explicit Mulberry(uint32_t s) : a(s) {} float operator()(); };

// How a run begins: which kind of island (standard, lush, rocky, misty) and
// which starting situation (ordinary, winter, sixth, tools, trusted). Chosen
// before a new game from what the legacy has unlocked; kept with the save.
struct StartOptions { std::string island = "standard", start = "ordinary"; };

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
    int lostThisSeason = 0;                   // deaths since the season turned
    int lastProposal = 0, lastSand = 0, newcomerDay = 0, lastRaid = 0, lastSick = 0, lastFeast = 0, totems = 0;
    std::optional<Vote> vote;
    std::vector<Fate> pendingFate;            // non-empty: the front end should pause and ask
    int nextProjId = 0, nextPid = 1, active = -1, fuelRes = -1, dockRes = -1;
    float toolWear = 20;
    // Goods in the village, each made by a gifted worker and used up over time:
    // pots, cloth (blankets), finecloth, furniture, spices, herbs (remedies).
    std::map<std::string, float> goods;
    float good(const std::string& k) const { auto it = goods.find(k); return it == goods.end() ? 0.f : it->second; }
    void addGood(const std::string& k, float n, float cap = 20) { goods[k] = std::clamp(good(k) + n, 0.f, cap); }
    // The kinds of food brought in, with the last day each was: berries, fish,
    // greens, grain, smoked. Variety is how many came in over the last five days.
    std::map<std::string, int> foodSeen;
    // The goods there are, in order, with what to call them and what makes them.
    struct GoodInfo { const char* key; const char* name; const char* building; const char* gift; };
    static const std::vector<GoodInfo>& goodList();
    int foodVariety() const { int n = 0; for (auto& [k, d] : foodSeen) n += d >= day() - 5; return n; }
    bool ironTools = false, hasHerb = false;
    std::optional<Agenda> agenda;
    // A specialist building waiting for the player to choose its spot. Set only
    // when askPlacement is on (the game sets it; tests and headless runs leave it
    // off and the settlers choose). step() stands still while it is set.
    std::optional<PendingPlacement> pendingPlacement;
    bool askPlacement = false;

    // ---- api ----
    void newWorld(uint32_t seed, const StartOptions& start = {});
    StartOptions startOpts;                   // how this run began

    // ---- saving ----
    // The whole game as bytes, and back. Settlers' half-done actions are not
    // kept: after a load everyone picks their next task. load() leaves the sim
    // as it was and returns false, with a reason, for anything it cannot read.
    static constexpr int SAVE_VERSION = 17;   // 2: site quality and pending placement; 3: rapport, contrariness, the story circle; 4: lines, questions; 5: deaths this season; 6: the narrator's rapport and buy-in; 7: duties and roles; 8: households; 9: gift practice; 10: goods and food kinds; 11: status and work; 12: blueprints and workmanship; 13: paths, names, looks; 14: petitions and trials; 15: the rival; 16: endings, years, chronicle; 17: start options
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
    // A walking route over land, round buildings (except docks, crop plots and the
    // story circle); the building you start in and the one you are going to are
    // let through. Tile centres, the last step to the goal itself left off.
    std::vector<std::pair<float, float>> findPath(float sx, float sy, float tx, float ty);
    bool reachable(int x, int y);             // can be walked to from camp

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
    std::vector<Line> linesFor(const Person& p, bool all = false);   // all: every line that fits, not just the best three
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

    // The Narrator's Ledger. Reading it never changes the game. Work gaps show in
    // working hours only (7 to 18), when who is doing what means something.
    std::vector<Gap> ledger();
    // The four sizes of nudge, smallest first: a day's line is narrate(); the
    // others cost more and last longer. Each returns "agreed", "refused" or
    // "unaffordable" (appeal: how many went along, as "agreed N of M").
    int seasonCost(const Person& p, const std::string& job);
    int roleCost(const Person& p, const std::string& job);
    int appealCost(const std::string& job);
    std::string askSeason(Person& p, const std::string& job);
    std::string appoint(Person& p, const std::string& job);
    void revoke(Person& p);
    std::string appeal(const std::string& job);
    static const char* roleName(const std::string& job);   // woodcutter, forager, builder, explorer

    // Households and homes. Status is the level of a household's home: 0 none of
    // their own (castaway), 1 hut (settler), 2 cottage (villager), 3 house
    // (householder), 4 manor (notable).
    std::deque<Household> households;
    int nextHousehold = 1;
    static constexpr int RISE_DAYS = 3, FALL_DAYS = 5;
    static int homeLevel(const std::string& key);
    static const char* statusName(int level);
    static const char* levelKey(int level);
    Household* household(int id);
    Household* householdOf(int pid);
    int status(const Household& h);
    std::string householdName(const Household& h);
    std::vector<HomeNeed> homeNeeds(const Household& h, int level);   // what that level asks of their home
    static HomeCost upgradeCost(int level);
    int reach(const std::string& key, int x, int y);   // walking tiles to the nearest built key (over 14: out of reach)
    float beauty(int x, int y);

    // Status and work (SimStatus.cpp).
    int statusOf(const Person& p);
    // How willing someone of a status is to take a job, 0..1: hard labour less so the higher they are.
    static float willing(int status, const std::string& job);
    bool notableAt(const std::string& job, const Person* besides = nullptr);
    float alongside(const Person& p, const std::string& skill);   // 1.2 working beside a notable
    int topStatus();
    int nextShip = 0;                            // the day a ship next calls, while a beacon stands

    // Petitions and trials (SimTrials.cpp).
    std::vector<Petition> petitions;
    int nextPetition = 1;
    bool petitionMet(const Petition& p);
    std::string petitionNeed(const Petition& p);     // what would meet it, in words
    std::vector<Trial> trials;
    static std::vector<std::string> trialKinds(int year);   // a hard winter, sickness, drought, storms; two at once from year 5
    static int trialSeason(const std::string& kind);
    static std::string trialName(const std::string& kind);
    std::vector<HomeNeed> trialChecklist(const std::string& kind);   // what readiness for it means
    float readiness(const std::string& kind);                         // 0..1, the share of the checklist met
    float trialHard(const std::string& kind);                         // how hard a running trial bites, 0 when none

    // The rival (SimRival.cpp).
    int rival = -1;                              // the settler competing with the narrator, -1 none yet
    int rivalAhead = 0;                          // days in a row their following has beaten the narrator's buy-in
    int overruledSince = 0, overruledDays = 0;   // the day they took over the planning (0 not now), and days overruled before
    float rivalStanding();                       // the village's average following of the rival
    bool overruled() const;                      // the rival plans: blueprints and appointments do not work
    void rivalCools(float n, const Household* h = nullptr);   // followers drift back (all, or one household)

    // Endings and the run (SimEndings.cpp). ending is empty until the run ends:
    // haven, homeward, awakening (good) or overruled, lost, divided.
    std::string ending;
    int endedDay = 0;
    std::vector<YearSummary> years;
    int yBirths = 0, yDeaths = 0, yMet = 0, yLapsed = 0, lastDeathDay = 0, dividedDays = 0, awakeningDays = 0;
    std::vector<Entry> chronicle;                 // the story so far: every journal entry that matters, kept whole
    static const char* endingTitle(const std::string& e);
    static const char* endingLine(const std::string& e);
    static bool goodEnding(const std::string& e);
    void endWith(const std::string& e);
    std::string writeSand(const std::string& text);
    std::vector<const ProjectDef*> proposalList();
    void propose(const std::string& key);
    void applyFate(const Fate& f);
    float avgBuyIn();
    // The narrator's rapport: what every request is paid from. It comes in each
    // day from how much the settlers buy in, and when the narrator's plans work
    // out; it goes on requests and when plans backfire.
    static constexpr float RAPPORT_MAX = 100;
    float rapport = 30;
    float rapportIncome();
    // How a settler would answer a line now: "agreed", "haggled" or "refused"
    // (contrariness and a second line in a day aside). narrate() uses the same rule.
    std::string likely(const Person& p, const Line& line) const;
    static const char* buyInWord(float b);       // "readily", "grudgingly"...                       // a day's income, from the average buy-in
    void earn(float n);                          // add (or, negative, lose) rapport, kept within 0..RAPPORT_MAX
    // What a day's request costs: cheap when it fits what they want (fit is how
    // well, as linesFor scores it), dear when it does not or they hardly buy in.
    static int price(const Person& p, const std::string& job, float fit);
    static constexpr float BUILD_PACE = 1.25f;   // building goes 20% quicker than it first did
    // How long gifted work takes at a level, as a share of a beginner's time:
    // about 1.1 at level 1, 0.65 at level 10.
    static float giftPace(int level) { return 1.15f - std::clamp(level, 1, 10) * .05f; }
    static float practiceFor(int level) { return 4.f + level * 2.f; }   // work to rise from this level
    // How much narrating a person takes before they turn contrary: easygoing
    // settlers put up with more, obstinate ones with less.
    static float tolerance(const Person& p);
    bool contrary(const Person& p) const { return p.contraryUntil > tMin; }
    std::string lastToldName, lastReply;

    // building sites
    static bool specialist(const ProjectDef& d) { return !d.gift.empty(); }
    // self: the id of a project already standing there, judged in its own place
    SiteReport evaluateSite(const ProjectDef& d, int x, int y, int self = -1) const;
    std::vector<Opinion> opinions(const ProjectDef& d, int x, int y, int self = -1) const;
    std::optional<std::pair<int, int>> suggestSite(const ProjectDef& d);
    bool placeAt(int projectId, int x, int y);   // the player's choice; false if the spot is not allowed
    bool placeSuggested(int projectId);          // "let them choose"
    float siteQ(const std::string& key);         // the built one's site quality times its workmanship, 1 if there is none

    // Blueprints. With the narrator placing (askPlacement), settlers build only
    // their homes and small things themselves; everything else waits for a
    // blueprint from the build menu, and settlers build it when they see fit.
    static bool settlersBuild(const ProjectDef& d);   // campfire, lean-to, rack, hut
    // The highest level of the needs pyramid the village has cleared, 0..5:
    // blueprints unlock as it rises (blueprintLevel).
    int villageLevel();
    static int blueprintLevel(const std::string& key);
    bool unlocked(const std::string& key, std::string* why = nullptr);
    std::vector<const ProjectDef*> blueprints();         // what the build menu offers, in order
    int blueprint(const std::string& key);              // start placing one; the project id, or -1
    void cancelBlueprint();                              // Esc while placing from the menu
    std::vector<Project*> wishes();                      // blueprints the settlers would build next, if placed
    static const char* craftWord(float c);

    // The village by hand (SimVillage.cpp): paths wear in where people walk and
    // make walking quicker; clearings, orchards and bridges; names and looks.
    std::vector<uint8_t> wear;                       // per tile, 0..250
    static constexpr int PATH = 60;                  // worn enough to be a path
    bool onPath(int x, int y) const { return inb(x, y) && wear.size() == size_t(GW * GH) && wear[idx(x, y)] >= PATH; }
    void rotatePlacement();                          // a bridge being placed turns 90 degrees
    bool rename(int projectId, const std::string& name);
    bool restyle(int projectId);
    std::string nameOf(const Project& b) const { return b.customName.empty() ? b.name : b.customName; }

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
    void talkHourly();
    void dutiesDaily();                       // seasons ending, roles' upkeep
    void villageDaily();                      // paths fade where nobody walks
    void petitionsDaily();
    void trialsDaily();
    void rivalDaily();
    void endingsDaily();
    void finishByHand(Project& b);            // a clearing felled, an orchard planted
    // Specialists get better with practice: each piece of gifted work adds to it,
    // and enough of it raises the gift a level (to 10).
    void practise(Person& p, const std::string& gift);
    void updateHouseholds();
    const std::vector<uint8_t>& walkGrid();
    bool pathFailed = false;                  // the last findPath found no way
    std::vector<uint8_t> reachMap;            // tiles that can be walked to from camp
    long reachSig = -2;
    std::vector<uint8_t> walkCache;
    long walkSig = -1;
    void homesDaily();                        // homes rise and fall
    void statusDaily();                       // patronage, ships, monuments, resentment
    void statusHourly();                      // notables' common work
    std::string leadJob(Person& p, Project& b);
    bool setHomeLevel(Project& b, int level);
    struct ReachMap { int built = -1; std::vector<uint8_t> dist; };
    std::map<std::string, ReachMap> reachCache;                        // new questions, lapsed ones, fibs coming out

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
    struct BedSpot { float x, y; bool inside; Project* b; float lieX = -1, lieY = -1; };   // lie: where they settle once there, if not where they walked to
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
