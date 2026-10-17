# Plan: the narrator, and placing buildings

Two goals:

1. Make the player the island's **narrator**. Everyone knows the narrator is there and just accepts it, like the weather. Nobody worships it. Settlers go along with it, argue with it, tease it and ask it things.
2. When the settlers decide to build a **specialist building**, **the player places it**. The settlers judge the spot, and a good or bad spot changes how well the building works.

**Status:** phase 1 done (placement, site quality, cookhouse). Next: phase 2.

Each phase below lands as its own PR into `main`, with its own tests and screenshots, like the graphics work.

## How it fits the code

- **Player actions stay sim methods.** Placing, narrating and replying are public `Sim` methods, and the HUD only calls them (CLAUDE.md rule). Tests can script a whole game by calling the same methods.
- **Pauses work like the fate cards.** The fate cards already pause the game: the sim fills `pendingFate`, the front end shows the cards, and the choice goes back through `applyFate`. Placement, settlers' questions and chapter choices follow the same pattern.
- **New state goes in the save.** Rapport, chapters, pending decisions and site quality are all saved state. Each phase that adds state adds it to `SimSave.cpp` and raises `SAVE_VERSION`.
- **The sim stays deterministic.** A player's decision is an input, like a fate choice.
- **Headless runs pick automatically.** Tests and `--screenshot` captures auto-choose when a decision comes up (the suggested site, the first reply). The survival tests keep running unattended, and a separate test covers each choice.

---

## Phase 1: Placement of specialist buildings

**Which buildings.** Every project with a required gift: garden, fields, well, store, longhouse, forge, mine, herb garden, kiln, loom house, plus the narrator-proposed boatyard, lighthouse, school, bath and infirmary. The campfire, lean-to, huts, rack, dock and tower are still placed by the sim.

### 1a. Site evaluation (sim, no graphics)

`SiteReport Sim::evaluateSite(const ProjectDef&, int x, int y) const` returns:

- `legal` (fits, on land, explored, not overlapping)
- `quality` from 0.6 (bad) to 1.25 (excellent)
- a list of `Factor { name, weight, text }`, each a reason a settler could say out loud, like "far from fresh water" or "right beside the forge's smoke"

Factors are measured from the map. Each building type has its own weighted list:

| Building | Good | Bad |
|---|---|---|
| Garden, fields | grass, fresh water within 6 tiles, near camp | forest or sand, beside the forge, far to walk |
| Well | close to fresh water (an easy dig), near the huts | down by the sea (brackish), far from where people sleep |
| Store | between the stockpile, the fields and the dock | far from all of them, low ground by the shore |
| Forge | near the mine or rocks, downwind of the homes | beside huts, the school or the infirmary (smoke and noise) |
| Kiln | near fresh water (clay), near wood | in the middle of the homes |
| Herb garden, infirmary | near water, quiet, near the huts | beside the forge |
| School | near the homes, quiet | beside the forge or the mine |
| Loom house | near the fields | |
| Bath | near the pond or a hill spring | |
| Longhouse | near the fire and the well | far from water |
| Boatyard, lighthouse, mine | already location-bound: shore, coast or hill | the same checks the sim uses today, now scored |

Two factors apply to every building: walking distance from camp (along paths, not as the crow flies) and trees that would have to be felled (a grove lost).

The sim's own picker becomes "the best of a sample of legal sites by `evaluateSite`". That keeps the automatic behaviour, and the suggestion the player sees, consistent with what the settlers think.

### 1b. The settlers' judgement

Everyone's opinion of a spot is `perceived = quality + bias`.

- **Bias shrinks with expertise:** the building's gift level counts most, then general building skill.
- **A specialist sees the real factors.** A smith at level 7 judging a forge site is nearly exact, and names the worst factor: "A forge this far from the stone? We'll walk all day."
- **A layperson's bias is seeded noise plus their personality.** Optimists like most spots, skeptics most don't, and their reasons are vague ("Looks fine to me").
- **What they say** comes from a small table per factor, in the journal's voice: plain, warm sentences, no em dashes.

### 1c. Pausing for the player

- **The sim.** When a specialist project is chosen and needs a site, the sim sets `pendingPlacement = {projectId, suggested x/y}` and stops stepping, like `pendingFate`.
  - `Sim::placeAt(id, x, y)` places it, checked against `evaluateSite().legal`.
  - `Sim::placeSuggested(id)` takes the suggestion: the "Let them choose" button.
- **On screen.** The game pauses, and a panel names the building and who championed it.
  - The building's footprint follows the pointer.
  - Tiles under it tint green to red by quality, and the top three factors show beside it.
  - Up to three settlers' reactions show live: always the specialist, plus whoever cares most.
  - The suggested site is outlined.
  - Click to place, Esc to let them choose.
- **A setting** lets the player have the settlers place everything themselves.

### 1d. Effects of a good or bad spot

- **Quality changes what the building does.** Each building's effect, an on/off `built("...")` check today, becomes a function of its site quality. `Sim::siteQuality(key)` returns 1 if the building is missing (no effect) and the stored quality once it's built.
  - store: spoilage rate × (2 − quality)
  - smokehouse: catch bonus × quality
  - well: thirst recovered per drink × quality
  - forge: tool repair × quality
  - herb garden, infirmary: recovery speed × quality
  - school: years to grow up, scaled by quality
  - bath: mood bonus × quality
  - garden, fields: growth rate and yield × quality
  - kiln, loom house: output × quality
- **Distance works by itself.** A far well or store already costs walking time through the sim's paths.
- **Moods.** When building finishes, each settler who judged the spot bad (perceived below 0.85) loses mood once, scaled by their expertise. The specialist who was overruled also loses some rapport with the narrator (Phase 2). A good spot gives a small boost.
- **The journal remembers.** "The smith still grumbles about the forge by the huts" on bad days, for the first season.
- **Your canteen example.** Add a **cookhouse**: a specialist building (the cook's gift, a new gift entry) where people take meals instead of at the fire. How much hunger a meal satisfies scales with its site quality. Being near the store and the fields is good; next to the forge, or a long walk from where people work, is bad. A badly placed cookhouse means hungrier people, exactly as you described.

### 1e. Tests

- **`evaluateSite`**, on hand-built maps:
  - a forge beside rocks beats one beside a hut
  - a well by fresh water beats one by the sea
  - illegal spots (water, overlap, unexplored) are refused
  - every factor names itself in plain words
- **Judgement.** Over 200 sampled sites, the specialist's error is smaller than a layperson's, and the higher the level, the smaller the error. Expertise zero is allowed to be confidently wrong.
- **Effects.** In paired runs from the same seed, a store at quality 1.2 spoils less food than at 0.7 over 30 days. The same holds for the well (thirst), the forge (tool wear) and the cookhouse (hunger).
- **The pause.** A pending placement stops `step()`. `placeAt` with a bad spot is refused, a good one works. The game resumes, and the choice survives a save and load.
- **The automatic picker** never chooses a site worse than the median of its own sample.
- **Visual review.** A `--placement` capture shows the placement screen, with screenshots of a good spot and a bad one.

---

## Phase 2: Re-theme the narrator, and add rapport

- **Rename and re-word.**
  - Belief becomes **rapport with the narrator**, per settler (0–100): how willing they are to go along.
  - "Shrine to the Voice" becomes **the story circle**, where people gather by the fire to tell tales, and it boosts rapport and belonging.
  - "Lost faith" becomes **"tired of being narrated"**.
  - Every journal line about the Voice, whispers or prayers is rewritten so the narrator is simply part of life, never divine.
  - Mechanically, belief already drives whether a whisper works. It keeps that role under its new name, so behaviour stays the same and the tests carry over.
- **Being pushed.** Narrating the same person too often within a few days builds **contrariness**. They do the opposite, or nothing, until it fades. Personality sets the limit: stubborn people tolerate less, easygoing people more.
- **Save:** rapport and contrariness are added.
- **Tests:**
  - the re-theme changes no outcome: with the same seed and inputs, the history matches the old build except for text
  - pushing one person three times in a day makes them contrary, and it fades
  - easygoing settlers tolerate more pushing than stubborn ones

## Phase 3: Narrating lines, and settlers talking back (the core verb)

- **Narrate a line.**
  - Click a settler and pick from 2–3 lines that fit their situation and wishes. For example: "Lark decided today was the day to fix the boat" (a task), "Kit and Juno got talking" (a friendship), or "Ada took the afternoon off" (rest).
  - The settler goes along, haggles ("After lunch") or refuses ("I did not decide that"), depending on rapport, contrariness and personality. Their reply shows as a speech bubble and in the journal.
  - This replaces whispers, keeping what whispers do underneath.
- **Settlers ask you things.** Now and then a settler asks the narrator a question about the weather, a friend, the future or a choice in front of them. A small card offers 2–3 replies:
  - **honest** builds rapport
  - **teasing** builds belonging, and costs rapport with sensitive people
  - **misleading** has consequences if they find out
  Unanswered questions expire after a day.
- **A pending-decisions list.** A small list in the corner holds questions, placements and the chapter choice, and pausing is opt-in per kind.
- **Tests:** replies change rapport in the expected direction, and a misleading answer that's found out costs more than it gained. Every line offered fits the settler (no "fix the boat" when there's no boat). Questions expire, and pending decisions survive a save.

## Phase 4: Chapters

- **Chapters replace the fate cards.** Each season, the player picks the next chapter's title from three, for example "The Long Winter", "Strangers on the Shore" or "The Quarrel". The title decides which events and pressures that season draws from. Existing fates become chapter content.
- **A protagonist.** Each chapter has one, chosen by the player or suggested. That settler's wish becomes the chapter's goal, the journal leans toward them, and the chapter ends with how their thread came out.
- **Tests:** each title changes the mix of events it promises (over many seeds). A chapter's goal is reachable. Chapters survive a save.

## Phase 5: Shaping tools

- **Foreshadowing** (a few per chapter): announce an event, and it happens within two days. Settlers who heard it prepare: stacking wood before a storm, staying in at night when wolves are foretold.
- **Plot hooks:** a crate ashore, a message in a bottle, a sail on the horizon, a stray dog, a map fragment. Each starts a small thread with an outcome.
- **Twists and "Luckily...":** add a complication when things go smoothly, and soften a disaster a limited number of times per chapter.
- **Wishes as story beats:** every settler has a wish, and the narrator decides when its thread comes together.
- **Tests:** a foretold event happens on time, and settlers who heard it prepare. Hooks resolve. Luck runs out.

## Phase 6: The map and the camera as narration

- **Describe a feature into being** (a few per chapter): click a spot and add a spring, a hidden grove or a fishing cove. Only where it makes sense (no spring in the sea), and it's recorded in the journal.
- **Names:** the village, landmarks and newborns, chosen by the player. Names have small effects, such as "the Whispering Woods" being avoided.
- **Scene control:** "Meanwhile, at the dock" (camera) and "Three days later" (skip to the next decision).

## Phase 7: Meta and finish

- **"That's not how it happened"**: once per chapter, rewind to just before the last event (through the save system) and let it play out differently.
- **Narrator tone** (warm, wry or ominous): changes the journal's wording and the mix of events.
- **The finished book**: at the end, the journal becomes a readable, exportable story with chapters, protagonists and an ending.
- **Throughout:** needs icons over settlers, a line to whatever your narration changed, and journal entries that say "because the narrator said so".

---

## Order and size

| Phase | Depends on | Rough size |
|---|---|---|
| 1. Placement and site quality (with cookhouse) | nothing | large |
| 2. Re-theme and rapport | nothing | medium |
| 3. Narrating lines and questions | 2 | large |
| 4. Chapters | 2 | medium |
| 5. Foreshadowing, hooks, twists, wishes | 3, 4 | large |
| 6. Map and camera narration | 3 | medium |
| 7. Rewind, tone, the book | 4 | medium |

Phases 1 and 2 don't touch each other's code and could run in parallel. Phase 1 adds the most to play right away.

## Open questions

- **Which buildings get placed by the player:** only specialist ones, as written, or huts too? Huts would mean many more pauses.
- **Should a bad spot be blocked when the specialist is very sure,** or always allowed with consequences? As written, it's always allowed. Overruling an expert is a story beat, not an error.
- **Cookhouse:** a new building as planned, or should the campfire's meal effect depend on where the camp is instead? The plan adds the cookhouse.
