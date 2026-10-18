// Static tables: project catalogue, traits, gifts, callings, inventions.
#include <algorithm>
#include <cctype>
#include "Sim.h"

namespace dw {

const char* TIER_NAMES[5] = {"Body", "Safety", "Belonging", "Esteem", "Purpose"};
const char* SEASONS[4] = {"Spring", "Summer", "Autumn", "Winter"};

std::string lower(std::string s) {
    for (auto& c : s) c = char(std::tolower((unsigned char)c));
    return s;
}

static ProjectDef P(std::string key, std::string name, int w, int h, int wood, int stone, int work) {
    ProjectDef d; d.key = key; d.name = name; d.w = w; d.h = h; d.wood = wood; d.stone = stone; d.work = work; return d;
}

const std::vector<ProjectDef>& allProjects() {
    static std::vector<ProjectDef> v = [] {
        std::vector<ProjectDef> v;
        auto add = [&](ProjectDef d, float r0, float r1, bool autoPlan, std::string gift = "", int min = 0, std::string req = "") {
            d.r0 = r0; d.r1 = r1; d.autoPlan = autoPlan; d.gift = gift; d.min = min; d.req = req; v.push_back(d); return &v.back();
        };
        // simple structures anyone can build
        add(P("fire", "Campfire", 1, 1, 8, 0, 120), 0, 0, true);
        { auto d = P("leanto", "Lean-to shelter", 3, 1, 22, 0, 300); d.beds = 5; add(d, 2.5f, 4, true); }
        add(P("rack", "Drying rack", 1, 1, 11, 0, 170), 2.5f, 4.5f, true);
        { auto d = P("hut", "Round hut", 2, 2, 34, 8, 540); d.beds = 2; add(d, 3.5f, 7, true); }
        // better homes, reached only by improving a hut (SimHomes)
        { auto d = P("cottage", "Cottage", 2, 2, 20, 8, 1); d.beds = 3; d.upgrade = true; add(d, 0, 0, false); }
        { auto d = P("house", "House", 2, 2, 30, 20, 1); d.beds = 4; d.upgrade = true; add(d, 0, 0, false); }
        { auto d = P("manor", "Manor", 3, 3, 50, 40, 1); d.beds = 6; d.upgrade = true; add(d, 0, 0, false); }
        // drawn up only by the narrator: shaping the land, and small things for beauty (SimVillage)
        auto hand = [&](ProjectDef d, std::string desc) { d.byHand = true; d.desc = desc; add(d, 2, 9, false); };
        hand(P("clearing", "Clear a grove", 3, 3, 0, 0, 320), "Fell the trees on a patch of ground, for the timber and the open space.");
        hand(P("orchard", "Orchard", 3, 3, 12, 0, 420), "Fruit trees in rows: fruit in summer and autumn, and a pleasant place to walk.");
        hand(P("bridge", "Footbridge", 1, 5, 30, 4, 420), "Planks across a stretch of water, so the far side is a walk instead of a swim.");
        hand(P("flowers", "Flower bed", 1, 1, 0, 2, 90), "Wild flowers dug in by a doorstep. The homes around look cared for.");
        hand(P("lantern", "Lantern post", 1, 1, 4, 2, 120), "A lamp on a post, lit at dusk. The village looks warm at night.");
        hand(P("bench", "Bench", 1, 1, 6, 0, 110), "Somewhere to sit and talk. A little more pleasant to live near.");
        hand(P("banner", "Banner", 1, 1, 5, 0, 120), "A banner in the village colours, for all to see.");
        // raised when a notable commissions it (SimStatus)
        { auto d = P("ship", "Ship", 3, 2, 120, 30, 2400); d.commissioned = true; d.desc = "A ship big enough for everyone, paid for by the notables and built by all. When it is done, they can sail home."; add(d, 0, 0, false, "sailor", 5, "boatyard"); }
        { auto d = P("monument", "Monument", 1, 1, 10, 30, 600); d.commissioned = true; d.desc = "A carved stone raised by a notable household, for everyone to see. The village is more beautiful for it."; add(d, 2, 6, false); }
        add(P("dock", "Fishing dock", 1, 3, 31, 0, 580), 0, 0, true);
        add(P("tower", "Watchtower", 1, 1, 48, 11, 810), 6, 11, true);
        // specialist projects
        add(P("garden", "Garden plot", 3, 2, 8, 0, 370), 4, 8, true, "farmer", 3);
        add(P("fields", "Grain field", 4, 3, 20, 0, 700), 6, 11, true, "farmer", 5, "garden");
        add(P("well", "Stone well", 1, 1, 8, 25, 510), 2, 5, true, "mason", 3);
        add(P("store", "Storehouse", 2, 2, 42, 17, 710), 3.5f, 7, true, "carpenter", 3);
        { auto d = P("longhouse", "Longhouse", 4, 2, 78, 31, 1500); d.beds = 6; add(d, 5.5f, 9.5f, true, "carpenter", 5); }
        add(P("forge", "Forge", 2, 2, 16, 30, 620), 3.5f, 7.5f, true, "smith", 3);
        add(P("mine", "Stone mine", 1, 1, 30, 0, 600), 0, 0, true, "smith", 4);
        add(P("herbs", "Herb garden", 2, 1, 6, 4, 260), 3, 7, true, "herbalist", 3);
        add(P("pottery", "Pottery kiln", 2, 2, 14, 20, 480), 4, 8, true, "potter", 3);
        add(P("loomhouse", "Loom house", 2, 2, 30, 6, 480), 4, 8, true, "weaver", 3);
        add(P("cookhouse", "Cookhouse", 2, 2, 30, 12, 560), 2.5f, 6, true, "cook", 3);
        add(P("workshop", "Carpenter's workshop", 2, 2, 34, 6, 520), 3, 7, true, "carpenter", 3);
        // proposals (narrator)
        auto o = [&](ProjectDef d, std::string desc, std::vector<std::string> like, std::vector<std::string> dislike, float r0, float r1, std::string gift = "", int min = 0, std::string req = "") {
            d.desc = desc; d.like = like; d.dislike = dislike; return add(d, r0, r1, false, gift, min, req);
        };
        o(P("signal", "Signal fire", 1, 1, 30, 6, 400), "A great pyre on high ground. Passing ships may see it and bring newcomers.", {"social", "curious"}, {"loner"}, 0, 0);
        o(P("smoke", "Smokehouse", 2, 1, 26, 10, 480), "Smoked fish barely spoils, and catches go further.", {"angler", "hardworking"}, {}, 3, 7)->food = true;
        o(P("circle", "Story circle", 2, 2, 12, 24, 520), "Log seats round a fire, where people gather to tell tales. Buy-in and spirits rise every day.", {"easygoing"}, {"obstinate"}, 5, 10);
        o(P("boatyard", "Boatyard", 2, 2, 40, 0, 620), "A small sailing boat. Better fishing, and they can chart the whole coastline.", {"angler", "curious"}, {}, 0, 0, "sailor", 4, "dock");
        o(P("lighthouse", "Lighthouse", 1, 1, 40, 40, 900), "A beacon that draws ships from far away. More newcomers.", {"social"}, {}, 0, 0, "mason", 6, "signal");
        o(P("school", "Schoolhouse", 2, 2, 36, 10, 600), "Children learn faster and grow up sooner.", {"cheerful", "social"}, {}, 4, 8, "carpenter", 4, "child");
        o(P("bath", "Hot spring bath", 2, 2, 20, 20, 560), "A steaming pool. Everyone is happier and shakes off the cold.", {"cheerful"}, {}, 5, 10, "mason", 4);
        o(P("infirmary", "Infirmary", 2, 2, 30, 14, 520), "Beds and remedies. The sick recover much faster.", {"easygoing", "social"}, {}, 4, 8, "herbalist", 5, "herbs");
        return v;
    }();
    return v;
}

const ProjectDef* projectDef(const std::string& key) {
    for (auto& d : allProjects()) if (d.key == key) return &d;
    return nullptr;
}

const std::map<std::string, std::string>& traitNames() {
    static std::map<std::string, std::string> m = {
        {"hardworking", "hardworking"}, {"cheerful", "cheerful"}, {"social", "sociable"}, {"loner", "a loner"},
        {"tough", "tough in the cold"}, {"greenthumb", "good with plants"}, {"angler", "a born angler"}, {"strong", "strong"},
        {"gloomy", "prone to gloom"}, {"easygoing", "easygoing"}, {"obstinate", "stubborn"}, {"curious", "curious"},
        {"tireless", "tireless"}, {"nightowl", "a night owl"}};
    return m;
}

const std::map<std::string, std::string>& skillNames() {
    static std::map<std::string, std::string> m = {
        {"chop", "woodcutting"}, {"mine", "stonework"}, {"forage", "foraging"}, {"fish", "fishing"}, {"build", "building"}, {"farm", "farming"}};
    return m;
}

const std::map<std::string, GiftInfo>& giftInfo() {
    static std::map<std::string, GiftInfo> m = {
        {"farmer", {"Farmer", "farming", "farm"}}, {"smith", {"Smith", "smithing", "stone"}}, {"mason", {"Mason", "masonry", "stone"}},
        {"carpenter", {"Carpenter", "carpentry", "wood"}}, {"herbalist", {"Herbalist", "herb lore", "food"}}, {"sailor", {"Sailor", "seafaring", "food"}},
        {"potter", {"Potter", "pottery", "stone"}}, {"weaver", {"Weaver", "weaving", "food"}},
        {"cook", {"Cook", "cooking", "food"}}};
    return m;
}

const std::map<std::string, CallingInfo>& callingInfo() {
    static std::map<std::string, CallingInfo> m = {
        {"carver", {"Carver"}}, {"musician", {"Musician"}}, {"storyteller", {"Storyteller"}}, {"explorer", {"Explorer"}},
        {"inventor", {"Inventor"}}, {"healer", {"Healer"}}, {"teacher", {"Teacher"}}, {"philosopher", {"Philosopher"}}};
    return m;
}

const std::vector<Invention>& inventionList() {
    static std::vector<Invention> v = {
        {"trap", "a woven fish trap", "Every fishing trip brings back more."},
        {"barrow", "a wheelbarrow", "Haulers can carry twice as much to the building sites."},
        {"skins", "water skins", "People can carry water and get thirsty more slowly."},
        {"sledge", "a log sledge", "Stone haulers drag bigger loads."},
        {"lamp", "fish-oil lamps", "Evenings are brighter and spirits higher."},
        {"compass", "a lodestone compass", "Explorers cover far more ground."}};
    return v;
}

bool Person::has(const char* t) const { return std::find(traits.begin(), traits.end(), t) != traits.end(); }

float Mulberry::operator()() {
    a += 0x6D2B79F5u;
    uint32_t t = a;
    t = (t ^ (t >> 15)) * (1u | t);
    t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t;
    return float((t ^ (t >> 14)) >> 8) / float(1u << 24);
}

} // namespace dw
