# Plan: the full game loop

This plan takes Driftwood from "a village that grows by itself while you
nudge it" to the full idea: rapport as the narrator's only currency, households
that rise in status, blueprints, a ledger of gaps, yearly trials, petitions, a
rival, endings, and a legacy between runs.

It replaces phases 4 to 7 of `narrator-and-placement.md`. Phases 1 to 3 of that
plan (placement, rapport and contrariness, lines and questions) stay and are
built on.

**Status:** agreed 2026-10-18. Phase 1 done (rapport as currency); phase 2 done (the Ledger); phase 3a done (households, homes rising and falling, status); phase 3b done (goods, food variety, the workshop); phase 4 done (status and work, notables, ships, resentment); phase 5 done (blueprints, unlocks, workmanship; paths, clearings, orchards, footbridges, decorations, names). phase 6 done (petitions, the year's trial foreseen, readiness). Next: phase 7, the rival.

---

## Where the idea meets the game today

### Already there, kept as is

| Idea | In the game now |
|---|---|
| Five castaways, an unknown island, fog of war | `newWorld`, `explored`, points of interest (hermit, ruins, cave, wreck) |
| Maslow's pyramid | `Person::tiers`, `gates`, `seeking`, the pyramid on the settler card |
| Callings, gifts, charisma | `calling`, `gifts`, `charisma`, `Agenda` (a charismatic specialist pulls the camp) |
| Rapport per person and village-wide | `Person::rapport`, `avgRapport()` |
| They listen, but have their own minds | narrated lines, refusals, haggling, contrariness |
| Settlers ask you things | `questions`, honest, teasing and misleading replies |
| Layout matters | site quality and named factors (`SimSites`) for specialist buildings |
| Fate cards each season | `drawFates` and `applyFate` |
| Sickness, winter, wolves | fever fate, seasons and cold, wolf raids |

### Conflicts, and how this plan settles them

1. **Rapport is a chance today, not a currency.** Now, rapport raises the odds that a line lands, and a success *earns* rapport. The idea turns this around: a request *costs* rapport, more when it goes against what they want, and good outcomes earn it back.
   *Settled:* every request gets a price (phase 1). Each line, nudge and appointment shows its cost before you choose. A refusal costs nothing but time, and `contrary` stays as the limit on pushing one person.

2. **Who plans the buildings.** Today the settlers plan almost everything themselves (`autoPlan`, `projScore`, the charisma agenda), and you only place specialist buildings and propose projects for a vote. The idea has you place blueprints while the settlers choose when and how well to build them.
   *Settled (decided 2026-10-18):* settlers still build their own homes and the small supplementary things by themselves: the campfire, lean-tos and huts, drying racks, totems. Everything else is a blueprint you place. Their wishes for anything bigger arrive as petitions ("we want a well"). Votes go away, and a settler's opinion of your blueprint feeds the rapport they gain or lose from it.

3. **Fate cards against chapters.** The old plan's phase 4 replaced fate cards with chapters. Your loop keeps fate cards each season.
   *Settled:* fate cards stay. Chapters are dropped. The good part of them (a settler whose story the journal follows) becomes the rival and the notables, who already carry a thread.

4. **Foreshadowing becomes visions and trials.** The old phase 5 had one-off foreshadowing. The idea has one escalating trial a year that you foresee and prepare for.
   *Settled:* trials replace that phase. Words in the sand stay as the cheap daily hint.

5. **"Describe a feature into being" against "never control anything directly".** The old phase 6 let the narrator create springs and groves.
   *Settled:* dropped. Terraforming becomes blueprints the settlers carry out (clearing, orchards, bridges).

6. **Mystics.** Your list has mystics among the callings. Phase 2 renamed that calling to the philosopher, so the narrator is never worshipped.
   *Settled (decided 2026-10-18):* it stays the philosopher. The "truth about who the Narrator is" (the Awakening ending) fits either way.

7. **Individuals against households.** Today everyone is a separate person with a bed. Couples share a bed and children grow up. There are no households, no home levels above the hut and longhouse, and no status.
   *Settled:* a household entity (partners, children, any lodgers) owns a home. Status belongs to the household. The individual needs pyramid stays and feeds it.

8. **One food number, a few goods.** `Stock` has food, wood and stone, plus pots, cloth and herbs as loose numbers. Status needs food variety and goods (blankets, furniture, spices, fine cloth).
   *Settled:* a goods table replaces the loose numbers (phase 3). Food gets kinds (fish, berries, grain, game, preserved).

9. **Labour.** Today every adult takes the most useful job, scored by need. The idea makes higher status less willing to do hard labour.
   *Settled:* job choice gains a willingness weight by status and job kind (phase 4). The ledger exists to make that visible and fixable.

10. **Population and map.** Today a village reaches about 12 to 20 people, which is too few for five status tiers and a working underclass.
    *Settled (decided 2026-10-18):* no cap. Newcomers come more often, households take lodgers, and the village grows as far as food, homes and land allow. That needs a performance pass early (pathfinding, `collect`, the 2-minute tick): the sim and the drawing are measured at 100 and 200 people (`--bench` for frames, a sim timing test for ticks), and the island grows with the population.

11. **Narrated lines** stay as the smallest nudge, "ask for a day" (decided 2026-10-18), now with a price.

12. **The narrator's question list against petitions.** Questions ask for an answer, petitions ask for something done.
    *Settled:* petitions are a second kind on the same "Waiting on you" list, with a deadline and a check that tells when they are met.

13. **Your Maslow note** (buildings tied to pyramid levels) fits here. Blueprints unlock as the village's settlers clear each level: belonging opens the longhouse and story circle, esteem opens decorations and the school, purpose opens monuments and specialist halls.

---

## Phases

Each phase lands as its own PR with tests and screenshots, stacked like before. Every phase that adds state adds it to `SimSave.cpp` and raises `SAVE_VERSION`.

### Phase 1: Rapport as currency

- **What a request costs.** `Sim::cost(person, request)` prices it as a base by size, times how far it is from what they would choose anyway, measured from their needs, calling, status (later), mood and skill. A request that matches their needs is cheap; one against them is expensive.
- **Two measures** (changed 2026-10-18): the narrator's **rapport** is a spendable meter in the top bar (0 to 100), and each settler's **buy-in** (shown in words, never a number) is how much they go along with the narrator. Buy-in sets the price and whether they agree, and the average buy-in is rapport's daily income.
- **Paying.** An accepted request takes its price from the narrator's rapport; a line you cannot afford cannot be said. A settler whose buy-in is under 2.5 times the price refuses (free); under 4 times, they haggle and do it later.
- **Earning.** Rapport comes back when outcomes land: a project the narrator placed pays off, a trial goes well, a petition is met, nobody dies in a season.
- **Losing.** Rapport drops when a plan backfires (a building on a bad site, a trial not prepared for), a petition lapses, or someone dies.
- **On screen.** Every line in the Narrate panel shows its price.
- **Tests.**
  - A matched request costs less than an opposed one.
  - Requests beyond someone's rapport are refused.
  - A season with no deaths and a met petition ends with more rapport than it started.

### Phase 2: The Narrator's Ledger

- **Gaps.** A rule list flags what is going wrong:
  - nobody on wood, food or water
  - a blueprint with no builder
  - sickness on the way and no herbalist
  - no one on the fire before winter
  - a household about to fall back a level (from phase 3)
- **Candidates.** Each gap lists people ranked by rapport cost, weighing status, trust, skill, mood and the gap their move would leave behind.
- **Four nudge sizes:**
  - ask for a day (today's lines)
  - ask for the season (a standing job, with an upkeep cost)
  - appoint to a role (woodcutter, fire keeper, builder, cook, teacher; holds until revoked; a refusal sours them)
  - appeal to the whole village (expensive; everyone shifts a little)
- **Longer-term fixes.** It suggests a sawpit, modest worker housing, inviting newcomers or teaching a child.
- **On screen.** A Ledger tab replaces the Narrator tab's top half.
- **Tests.**
  - Every gap rule fires on a hand-built state and clears once fixed.
  - Candidates are sorted by cost.
  - An appointed role holds until revoked.

### Phase 3: Households, homes and goods

- **Households.** A household holds partners, their children and lodgers. Newcomers arrive single or as small families.
- **Homes.** Five levels: lean-to, hut, cottage, house, manor. A home upgrades in place when its needs are met for a while. Recipes for the cottage, house and manor go in `Building.cpp`, each grown from the one before.
- **Status ladder.** Castaway, settler, villager, householder, notable.
  - Each step needs more food variety, goods, services within reach and beauty nearby.
  - A home falls back a level when a need lapses, after a warning in the ledger.
- **Goods.**
  - A goods table holds pots, blankets, furniture, spices, fine cloth and tools, each made by a gifted worker in its building (potter at the kiln, weaver at the loom house, carpenter at a new workshop).
  - Food gets kinds, and "food variety" counts the kinds eaten in the last few days.
  - Goods are used up over time.
- **Coverage and beauty.**
  - A service covers homes within a walking distance by path: well, bath, school, infirmary, story circle.
  - Beauty is the sum of nearby decorations and trees, minus nearby forge smoke and mines.
- **Tests.**
  - A home with its needs met rises; one with a lapsed need falls back.
  - Coverage follows paths, not straight lines.
  - Goods run down when nobody makes them.

### Phase 4: Status and work

- **Willingness to work.** Each status has a willingness for each job kind. A notable barely hauls stone, but leads, trades and commissions.
- **What notables do:**
  - lead a project (it goes faster)
  - commission monuments and the ship
  - trade with passing ships for spices and fine cloth
  - give artisans patronage (callings grow faster)
- **Common work.** A notable who agrees to common work moves everyone, but too much of it costs them status.
- **Inequality and resentment.** Inequality is the gap between the top and bottom households. Resentment grows with it in lower households, lowers their mood and their rapport with the narrator, and feeds the rival (phase 7).
- **Tests.**
  - An all-notable village runs out of food (the "starving in comfort" case).
  - A notable doing common work raises others' output and loses status.

### Phase 5: Blueprints and the village by hand

- **Blueprints.** You place every blueprint. This generalises phase 1's placement to every building, with the same site quality.
  - Settlers pick it up by need, labour and rapport.
  - How well it is built depends on the builder's skill and mood, and shows in the art and the effect.
- **Unlocks.** Blueprints unlock by the pyramid levels the village has cleared (your Maslow note) and by gifts present.
- **Paths wear in.** Paths form on the ground cache where people walk, and walking on a path is faster.
- **Adjacency and coverage overlays.** These show while you place a blueprint.
- **Terraforming blueprints:**
  - clear a grove
  - plant an orchard (food and beauty)
  - build a bridge (a path over water)
- **Decorations:** flowers, lanterns, benches, banners, totems. They add beauty, belonging and esteem.
- **Personal touches:**
  - choose a roof style and colour on homes (recipe options)
  - name buildings and streets
  - callings leave marks: a carver's totems, a musician's stage
- **Tests.**
  - Each blueprint kind is buildable, and a bridge joins two shores in pathfinding.
  - Build quality follows skill.
  - Paths form where people walk.

### Phase 6: Petitions, visions and trials

- **Petitions.** These come from households and from the settlers' own wish list (see conflict 2): a home, a feast, a luxury, a building. Each has a deadline and a check for when it is met. Meeting one earns rapport, letting it lapse loses some.
- **Visions.** Early each year, the narrator foresees that year's trial and its season.
  - Year 1: a hard winter.
  - Year 2: sickness.
  - Year 3: drought.
  - Year 4: a season of storms (lost roofs, a flooded store, a boat in danger).
  - Later years combine them.
- **Preparation is measured** (stores, wood, the herbalist, the watchtower, the well) and shown in the ledger. The trial's outcome scales with it.
- **Fate cards** stay each season and may tilt the trial.
- **No enemies yet** (decided 2026-10-18): trials are nature, sickness and scarcity. Wolves stay as they are; no raiders.
- **Tests.**
  - A petition's check passes once it is met, and lapses otherwise.
  - A prepared village loses less in its trial than an unprepared one, over several seeds.
  - Trials escalate by year.

### Phase 7: The rival

- **The rival.** A charismatic settler, usually a notable, with a vision of their own. This grows out of today's `Agenda`.
  - They have a rapport of their own with every settler, and it competes with yours.
  - They petition against your blueprints and win over the resentful.
- **Overruled.** If the narrator's village rapport stays below the rival's for a season, the rival takes over planning: blueprints and appointments stop working.
- **Winning back.** You can still recover, through trials met and petitions fulfilled.
- **Tests.**
  - Resentment raises the rival's support.
  - Being overruled blocks blueprints.
  - Recovery is possible.

### Phase 8: Endings and the run

- **Good endings:**
  - **Haven:** balanced status, high beauty and coverage, no deaths for a year, rapport high.
  - **Homeward:** notables commission the ship, labourers build it at the boatyard, and everyone sails.
  - **Awakening:** the ruins hold a secret found through exploration and the philosopher, revealing who the narrator is.
- **Bad endings:**
  - **Overruled:** the rival holds the story for a year.
  - **Lost:** everyone dead or gone.
  - **Divided:** resentment past a threshold splits the village; the resentful leave to found a camp of their own.
- **The yearly summary.** Each year ends with homes evolving, status shifts, the next trial's vision and the rival's standing, so the loop is felt.
- **The ending screen.** It reads the journal back as a short story.
- **Tests.** Each ending is reachable from a scripted setup, and each one triggers only its own screen.

### Phase 9: Legacy

- **The legacy file.** A profile file beside the saves (`src/game/Saves`) records endings reached and points earned. It lives outside the save so it carries across runs.
- **Unlocks:**
  - new islands (seed families with other shapes and points of interest)
  - starting conditions (a known gift, a crate of tools, a sixth castaway)
  - stronger narrator abilities (an extra nudge size, cheaper appeals, a second vision)
- **On screen.** The title screen shows the legacy and what is unlocked.
- **Tests.**
  - The profile round-trips.
  - Unlocks apply to a new run and never to a running save.

---

## Order and size

| Phase | Depends on | Rough size |
|---|---|---|
| 1. Rapport as currency | (phases 1 to 3 done) | medium |
| 2. The Ledger | 1 | large |
| 3. Households, homes, goods | 1 | very large |
| 4. Status and work | 3 | medium |
| 5. Blueprints and detailing | 3 | very large |
| 6. Petitions, visions, trials | 2, 3 | large |
| 7. The rival | 4, 6 | medium |
| 8. Endings | 5, 6, 7 | medium |
| 9. Legacy | 8 | medium |

Phases 1 and 2 change how it feels to play straight away. Phase 3 is the biggest and the riskiest: households, five home levels with art, and a goods economy. It could be split into homes and status (3a) and goods and coverage (3b).

## Decided (2026-10-18)

- Settlers build their own homes and the supplementary things (campfire, lean-tos, huts, racks, totems); everything else is your blueprint.
- Narrated lines stay, as "ask for a day".
- No enemies yet: no raiders.
- The calling stays the philosopher.
- No population cap.
- Legacy is mostly variety (new islands, castaways, starting situations) with only a little power.

## Open questions

None right now.

