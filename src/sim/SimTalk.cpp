// Talking with the narrator: the lines it can say about a settler and how they
// answer, and the questions settlers ask it in return.
#include <algorithm>
#include "Sim.h"

namespace dw {

namespace {
// A stable number for a settler and a day, to vary wording without touching the
// behaviour generator.
uint32_t mix(uint32_t a, uint32_t b) { uint32_t h = a * 2654435761u ^ (b + 0x9e3779b9u + (a << 6) + (a >> 2)); h ^= h >> 15; h *= 2246822519u; h ^= h >> 13; return h; }
const std::string& pickBy(uint32_t h, const std::vector<std::string>& v) { return v[h % v.size()]; }
} // namespace

float Sim::tolerance(const Person& p) { return p.has("easygoing") ? 5 : p.has("obstinate") ? 2 : 3; }

float Sim::rapportIncome() { return avgBuyIn() / 20; }

std::string Sim::likely(const Person& p, const Line& line) const {
    bool social = line.job == "peace" || line.job == "talk";
    if (float(line.cost) > p.buyIn / 2.5f) return "refused";
    if (!social && float(line.cost) > p.buyIn / 4) return "haggled";
    return "agreed";
}

const char* Sim::buyInWord(float b) { return b > 75 ? "wholeheartedly" : b > 55 ? "readily" : b > 35 ? "somewhat" : b > 18 ? "grudgingly" : "hardly at all"; }
void Sim::earn(float n) { rapport = std::clamp(rapport + n, 0.f, RAPPORT_MAX); }

int Sim::price(const Person& p, const std::string& job, float fit) {
    // a day's request, cheaper the more it is what they want anyway
    bool social = job == "peace" || job == "talk";
    float c = social ? 5.f : 8.f;
    c *= fit >= 2.4f ? .5f : fit >= 1.8f ? .75f : fit >= 1.2f ? 1.f : 1.4f;
    if (p.has("obstinate")) c *= 1.3f;
    if (p.has("easygoing")) c *= .8f;
    bool work = job == "food" || job == "wood" || job == "build" || job == "explore" || job == "hermit";
    if (work && p.needs.fatigue > 75) c *= 1.5f;
    c *= 1.4f - p.buyIn / 100 * .8f;   // those who hardly buy in cost more to ask
    return std::max(2, int(std::lround(c)));
}

std::vector<Line> Sim::linesFor(const Person& p, bool all) {
    struct Cand { float fit; Line l; };
    std::vector<Cand> c;
    uint32_t h = mix(uint32_t(p.id), uint32_t(day()));
    const std::string& n = p.name;
    if (p.child || p.status != "member") return {};
    // what they need, or would like, decides which lines fit best
    c.push_back({p.needs.fatigue > 55 || p.mood < 35 ? 3.f : .6f, {"rest", n + pickBy(h, {" took the afternoon off.", " decided to put their feet up for a while.", " lay down in the shade and let the day go by."})}});
    float fd = foodDays();
    c.push_back({fd < 5 ? 2.6f : 1.f, {"food", n + (p.has("angler") ? std::string(" decided the fish were biting today, and went to find out.") : pickBy(h >> 3, {" decided the camp could use more food, and went foraging.", " went looking for berries before anyone else could eat them."}))}});
    auto* f = fire();
    c.push_back({stock.wood < 20 || (f && f->fuel < 2) ? 2.4f : 1.f, {"wood", n + pickBy(h >> 5, {" decided today was the day to stock up on wood.", " picked up the hatchet and went to find some dry timber."})}});
    if (auto* pr = activeProject()) c.push_back({1.8f, {"build", cat(n, " decided today was the day to help with the ", lower(pr->name), ".")}});
    if (!p.calling.empty()) {
        static const std::map<std::string, std::string> verb = {{"carver", "sit down and carve"}, {"musician", "play for a while"}, {"storyteller", "work on a new story"}, {"explorer", "go and see what is out there"},
                                                                {"inventor", "tinker with an idea"}, {"healer", "gather remedies"}, {"teacher", "teach someone something"}, {"philosopher", "think things over by the water"}};
        c.push_back({p.callingNeed > 50 ? 2.8f : 1.2f, {"calling", cat(n, " finally made time to ", verb.count(p.calling) ? verb.at(p.calling) : "follow their calling", ".")}});
    }
    int land = 0, known = 0;
    for (int i = 0; i < GW * GH; i++) if (tt[i] >= 2) { land++; known += explored[i]; }
    if (land && known < land * .9f) c.push_back({p.has("curious") ? 2.2f : 1.1f, {"explore", n + pickBy(h >> 7, {" wondered what lay past the trees, and went to look.", " set off to see the far side of the island."})}});
    for (auto& hp : pois) if (hp.type == "hermit" && hp.found && !hp.joined) { c.push_back({1.4f, {"hermit", n + " packed some food and went to visit the hermit."}}); break; }
    // someone they are at odds with, and someone they could get to know
    const Person* low = nullptr; const Person* chat = nullptr;
    for (auto& q : people) {
        if (&q == &p || q.dead || q.child || q.status != "member") continue;
        if (!low || affOf(p, q.id) < affOf(p, low->id)) low = &q;
        if (!chat || std::abs(affOf(p, q.id) - 35) < std::abs(affOf(p, chat->id) - 35)) chat = &q;
    }
    if (low && affOf(p, low->id) < 25) c.push_back({2.f, {"peace", cat(n, " went to make up with ", low->name, "."), low->id}});
    if (chat && chat != low) c.push_back({p.needs.social > 50 ? 2.5f : 1.3f, {"talk", cat(n, " and ", chat->name, " got talking."), chat->id}});
    // the price follows how well it fits; nothing to narrate about what they are already doing;
    // the best three, ties broken the same way all day
    for (size_t i = 0; i < c.size(); i++) {
        c[i].l.cost = price(p, c[i].l.job, c[i].fit);
        c[i].l.fit = c[i].fit;
        if (c[i].l.job == p.kind || (c[i].l.job == "rest" && p.kind == "idle")) c[i].fit -= 3;
        c[i].fit += float(mix(h, uint32_t(i)) % 100) / 400.f;
    }
    std::stable_sort(c.begin(), c.end(), [](const Cand& a, const Cand& b) { return a.fit > b.fit; });
    std::vector<Line> out;
    for (size_t i = 0; i < c.size() && (all || out.size() < 3); i++) out.push_back(c[i].l);
    return out;
}

std::string Sim::narrate(Person& p, const std::string& job, Person* target) {
    Line l{job, cat(p.name, " decided to ", job, "."), target ? target->id : -1, price(p, job, 1)};
    for (auto& o : linesFor(p)) if (o.job == job && (!target || o.target == target->id)) l = o;
    return narrate(p, l);
}

std::string Sim::narrate(Person& p, const Line& line) {
    lastToldName = p.name;
    if (float(line.cost) > rapport) { lastSaid.clear(); return lastReply = "unaffordable"; }   // the narrator has not the rapport to ask
    const std::string& job = line.job;
    Person* target = line.target >= 0 ? person(line.target) : nullptr;
    int today = day();
    p.pressure += 1;
    auto said = [&](const std::string& s, double mins) { bubble(p, s, mins); lastSaid = s; };
    // narrated once too often: they do the opposite, or nothing at all
    if (contrary(p) || p.pressure >= tolerance(p)) {
        bool already = contrary(p);
        p.contraryUntil = std::max(p.contraryUntil, tMin + 36 * 60);
        p.buyIn = std::max(0.f, p.buyIn - 3);
        if ((job == "peace" || job == "talk") && target) {
            affAdd(p, target->id, -8);
            said("I'll talk to " + target->name + " when I feel like it", 80);
            note(cat(p.name, " heard the narrator suggest a word with ", target->name, ", and pointedly sat on the other side of the fire."), "narrator");
        } else {
            static const std::map<std::string, std::string> opposite = {{"rest", "wood"}};
            auto it = opposite.find(job);
            p.told = Narration{it == opposite.end() ? "rest" : it->second, tMin + 6 * 60, true, tMin};
            clearQ(p);
            p.carry.reset();
            said(already ? "No." : "I decide what I do today.", 70);
            note(already ? cat(p.name, " is still having none of it, and did the opposite again.")
                         : cat(p.name, " has been narrated once too often. \"I can decide things myself, thank you.\" They will do as they please for a while."), "narrator");
        }
        return lastReply = "contrary";
    }
    if (p.lastTold == today) {
        p.buyIn = std::max(0.f, p.buyIn - 6);
        said("Twice in one day?", 60);
        note(cat(p.name, " rolled their eyes. \"Twice in one day?\" They go along a little less willingly now."), "narrator");
        return lastReply = "pushed";
    }
    p.lastTold = today;
    bool social = (job == "peace" || job == "talk") && target;
    // the narrator pays from its rapport; whether they go along is their buy-in against the price
    std::string will = likely(p, line);
    if (will == "refused") {
        said(pickBy(mix(p.id, today), {"I did not decide that.", "Not today.", "Says who?", "I'd want a better reason than that."}), 60);
        note(cat("\"", line.text, "\" \"", lastSaid, "\" said ", p.name, ", and carried on as before."), "narrator");
        return lastReply = "refused";
    }
    earn(-float(line.cost));
    // asked for what they wanted anyway, they warm to the narrator; asked against the grain, they cool a little
    p.buyIn = std::clamp(p.buyIn + (line.cost <= 4 ? 1.f : line.cost >= 9 ? -1.f : 0.f), 0.f, 100.f);
    if (will == "haggled") {
        // only just enough: they will, in their own time
        p.told = Narration{job, tMin + 12 * 60, false, tMin + 3 * 60};
        said(pickBy(mix(p.id, today + 1), {"After lunch.", "Once I've finished this.", "In a bit."}), 70);
        note(cat(line.text, " \"", lastSaid, "\" ", p.name, " said, and meant it."), "narrator");
        return lastReply = "haggled";
    }
    if (social) {
        if (job == "peace") { affAdd(p, target->id, 22); affAdd(*target, p.id, 15); said(target->name + ", I'm sorry", 80); }
        else { affAdd(p, target->id, 12); affAdd(*target, p.id, 10); p.needs.social = std::max(0.f, p.needs.social - 25); target->needs.social = std::max(0.f, target->needs.social - 25); said("Have you got a minute?", 70); }
        note(cat(line.text, job == "peace" ? " They talked until the fire burned low." : " Neither of them noticed the time go by."), "narrator");
        return lastReply = "agreed";
    }
    p.told = Narration{job, tMin + 9 * 60, false, tMin};
    clearQ(p);
    p.carry.reset();
    said(pickBy(mix(p.id, today + 2), {"Fair enough.", "Why not.", "I was about to anyway."}), 70);
    note(cat(line.text, " And so they did."), "narrator");
    return lastReply = "agreed";
}

// ---------------------------------------------------------------- questions

void Sim::ask(Person& p, const std::string& topic, int about) {
    Question q;
    q.id = nextQuestion++;
    q.pid = p.id;
    q.topic = topic;
    q.asked = tMin;
    q.expires = tMin + 24 * 60;
    p.lastAsked = day();
    Person* o = about >= 0 ? person(about) : nullptr;
    if (topic == "friend" && o) {
        q.about = o->id;
        q.truth = affOf(*o, p.id) >= 40;
        q.text = cat("Does ", o->name, " like me, do you think?");
        q.replies = {{"honest", q.truth ? cat(o->name, " thinks well of you. Go and sit with them.") : cat(o->name, " hasn't warmed to you yet. Give it time.")},
                     {"teasing", "Ask them yourself. I only narrate."},
                     {"misleading", q.truth ? std::string("Not really, no.") : cat("Oh, ", o->name, " adores you.")}};
    } else if (topic == "winter") {
        q.truth = foodDays() >= 10;
        q.text = "Will we have enough food for the winter?";
        q.replies = {{"honest", q.truth ? "If you keep at it, yes." : "Not as things stand. Gather more while you can."},
                     {"teasing", "Only if somebody stops eating all the berries."},
                     {"misleading", q.truth ? "I wouldn't count on it." : "Plenty. Don't worry about it."}};
    } else if (topic == "skill") {
        std::string best = "chop";
        for (auto& [k, v] : p.sk) if (v > p.sk[best]) best = k;
        q.truth = p.sk[best] >= 5;
        q.about = -1;
        q.text = cat("Am I any good at ", skillNames().at(best), "?");
        q.replies = {{"honest", q.truth ? "Better than anyone here." : "Not yet. You will be."},
                     {"teasing", "Better than you are at asking questions."},
                     {"misleading", q.truth ? "You could be worse." : "The best on the island."}};
    } else {
        q.topic = "home";
        q.truth = built("boatyard") != nullptr;
        q.text = "Will we ever get off this island?";
        q.replies = {{"honest", q.truth ? "You could. Whether you will is another story." : "Not soon. Make a home here."},
                     {"teasing", "And miss all this?"},
                     {"misleading", q.truth ? "Never. Get used to it." : "Any day now. Keep an eye on the horizon."}};
    }
    bubble(p, "Narrator?", 90);
    questions.push_back(q);
}

bool Sim::answer(int questionId, int reply) {
    auto it = std::find_if(questions.begin(), questions.end(), [&](const Question& q) { return q.id == questionId; });
    if (it == questions.end() || reply < 0 || reply >= int(it->replies.size())) return false;
    Question q = *it;
    questions.erase(it);
    Person* p = person(q.pid);
    if (!p || p->dead) return false;
    const Reply& r = q.replies[reply];
    Person* o = q.about >= 0 ? person(q.about) : nullptr;
    note(cat(p->name, " asked the narrator, \"", q.text, "\" and was told, \"", r.text, "\""), "narrator");
    if (r.kind == "honest") {
        p->buyIn = std::min(100.f, p->buyIn + 4);
        if (q.topic == "friend" && o && q.truth) affAdd(*p, o->id, 5);
        if (q.topic == "winter" && !q.truth) boost["food"] = std::max(boost["food"], tMin + 24 * 60);
        if (q.topic == "skill" && q.truth) p->esteemPts += 5;
        earn(2);
        bubble(*p, "Thank you.", 50);
    } else if (r.kind == "teasing") {
        // a laugh makes them feel part of things; the sensitive take it to heart
        p->needs.social = std::max(0.f, p->needs.social - 15);
        p->mood = std::min(100.f, p->mood + 3);
        if (p->has("gloomy")) { p->buyIn = std::max(0.f, p->buyIn - 4); bubble(*p, "Oh. Right.", 50); }
        else { p->buyIn = std::min(100.f, p->buyIn + 1); bubble(*p, "Ha!", 50); }
    } else {
        // what they wanted to hear, for now
        p->buyIn = std::min(100.f, p->buyIn + 2);
        p->mood = std::min(100.f, p->mood + 5);
        std::string reveal = q.topic == "friend" && o ? (q.truth ? cat(p->name, " found out ", o->name, " likes them after all, no thanks to the narrator.") : cat(p->name, " found out ", o->name, " never said any such thing, and is cross with the narrator."))
                           : q.topic == "winter" ? cat(p->name, " counted the stores and saw the narrator had not told them the truth about winter.")
                           : q.topic == "skill" ? cat(p->name, " watched the others at work and knew the narrator had not been straight with them.")
                                                : cat(p->name, " worked out the narrator had been stringing them along about leaving.");
        fibs.push_back({p->id, tMin + (24 + std::uniform_int_distribution<int>(0, 48)(talkGen)) * 60.0, reveal});
        bubble(*p, "Really?", 50);
    }
    return true;
}

void Sim::talkHourly() {
    int h = int(hour());
    // a question nobody answered lapses after a day
    for (size_t i = 0; i < questions.size();) {
        if (questions[i].expires > tMin) { i++; continue; }
        if (auto* p = person(questions[i].pid)) { p->buyIn = std::max(0.f, p->buyIn - 1); if (!p->dead) bubble(*p, "Never mind, then.", 40); }
        questions.erase(questions.begin() + long(i));
    }
    // misleading answers come out, most of the time
    for (size_t i = 0; i < fibs.size();) {
        if (fibs[i].due > tMin) { i++; continue; }
        Fib f = fibs[i];
        fibs.erase(fibs.begin() + long(i));
        Person* p = person(f.pid);
        if (!p || p->dead || std::uniform_real_distribution<float>(0, 1)(talkGen) >= .75f) continue;
        p->buyIn = std::max(0.f, p->buyIn - 10);
        p->mood = std::max(0.f, p->mood - 6);
        earn(-5);
        note(f.reveal, "narrator");
    }
    // now and then, in daylight, someone asks the narrator something
    if (h < 9 || h >= 19 || questions.size() >= 2) return;
    if (std::uniform_real_distribution<float>(0, 1)(talkGen) >= 1.f / 24) return;
    std::vector<Person*> who;
    for (auto* p : members()) if (!p->child && p->sick <= 0 && !contrary(*p) && day() - p->lastAsked >= 2) {
        bool waiting = false;
        for (auto& q : questions) waiting |= q.pid == p->id;
        if (!waiting) who.push_back(p);
    }
    if (who.empty()) return;
    Person* p = who[std::uniform_int_distribution<size_t>(0, who.size() - 1)(talkGen)];
    std::vector<std::string> topics = {"skill", "home"};
    if (season() <= 2) topics.push_back("winter");
    Person* other = nullptr;
    for (auto* q : members()) if (q != p && !q->child && (!other || std::abs(affOf(*p, q->id) - 45) < std::abs(affOf(*p, other->id) - 45))) other = q;
    if (other) { topics.push_back("friend"); topics.push_back("friend"); }
    auto t = topics[std::uniform_int_distribution<size_t>(0, topics.size() - 1)(talkGen)];
    ask(*p, t, t == "friend" && other ? other->id : -1);
}

} // namespace dw
