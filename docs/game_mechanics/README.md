# Cell Royale — Game Mechanics

A complete reference for everything you can do in the game. Covers both **VS AI**
(single-player) and **Royale → Local** (LAN multiplayer). The **Global** mode is a
placeholder; it'll match this document when it ships.

---

## 1. Game modes

| Mode | How to start | What it does |
|---|---|---|
| **VS AI** | Main Menu → `VS AI` (or press Enter / Space) | Single-player sandbox. Authoritative sim runs locally; bots fill the world (host-controlled via the `bots` console command). |
| **Royale → Local → Host** | Main Menu → `ROYALE` → `LOCAL` → `HOST` → wait for joiners → `START GAME` | Pressing `HOST` opens the UDP listener (port 7456) immediately and starts a LAN announcer so peers see you in their browser. Peers join *into the lobby*, exchange names, and appear in your player list. Hitting `START GAME` launches the match for everyone at the same instant. |
| **Royale → Local → Join** | Main Menu → `ROYALE` → `LOCAL` → `JOIN` → pick a discovered host (or enter `host:port`) → `JOIN` | Connects to the host and lands in the host's lobby. You see the player list and wait while the host gathers everyone. The match starts automatically when the host hits `START GAME`. |
| **Royale → Global** | Main Menu → `ROYALE` → `GLOBAL` | Placeholder. Matchmaking-backed online play, planned for Phase 10. |

Same-machine loopback is supported: start a Host instance, then start a Join instance
and connect to `127.0.0.1:7456`. Multiplayer modes don't auto-pause on focus loss
(necessary so the host keeps ticking and the client keeps pumping packets).

---

## 2. Input

### Desktop (mouse + keyboard)

| Action | Default binding | Notes |
|---|---|---|
| Move | Mouse cursor | Your cell seeks toward the cursor. Anti-jitter clamps the seek speed so the cell stops cleanly when the cursor sits on it. |
| Split | `Space` | Splits each eligible cell in half, launching the new piece in the cursor direction. Costs no mass (the original splits 50/50). Max 16 cells per player. |
| Eject mass | `W` | Drops a small food pellet in the cursor direction. Used to feed viruses, push enemies, or barter mass. |
| Dash | `Shift` | Short burst of 3× speed with brief invuln. 4s cooldown. |
| Mass Blast (Q) | `Q` | Spends 20% of your biggest cell's mass to push every enemy cell + nearby food radially outward. 6s cooldown. Min cast mass = 300. Blast radius scales with caster size. |
| Pause / Menu | `Esc` (in match) | **Single-player**: freezes the sim AND shows the overlay with RESUME / MAIN MENU buttons. **Multiplayer**: opens the same overlay but the sim KEEPS ticking (host can't freeze peers; client can't freeze the host). Buttons read RESUME / END MATCH (host) or RESUME / DISCONNECT (client). |
| Hold-to-move mode | Console command `set_hold_to_move 0|1` | When on, your cell only moves while the left mouse button is held. Off by default. |
| Dev console | `` ` `` (backtick / tilde) | Opens a text input for commands. See §10. Hidden in release builds (CR_ENABLE_DEV_TOOLS=OFF). |
| Skip Death Cam | Any gameplay key | The 1.5s killer-focus cam can be skipped early. |
| Quit | `Esc` (in menus) | Returns to the previous screen or quits from the main menu. |

### Touch (mobile / touch-forced builds)

| Action | Gesture |
|---|---|
| Move | Left virtual stick / drag |
| Split | Right virtual button |
| Eject mass | Right virtual button (long press) |
| Dash | Tap a dedicated dash button (HUD-corner) |
| Mass Blast | Tap a dedicated Q button |
| Swap thumbs | `set_invert_thumbs 1` console |
| Force touch UI on desktop | `force_touch 1` console |

Touch detection auto-switches based on the platform; the desktop build can be
forced into the touch layout via `force_touch 1`.

---

## 3. Player abilities

### Split (`Space`)

- Splits every eligible cell in half. Each new piece launches in the **cursor
  direction** at high velocity, decaying naturally over ~2 seconds.
- Minimum mass to split: **200**. Cells under that are too small to halve.
- Maximum simultaneous cells per player: **16**.
- Recombine delay: **12 seconds** before two same-owner cells can merge back
  together by overlap.
- Tactical uses: leap onto smaller prey from outside their seek radius; escape a
  bigger cell by halving yourself and sprinting in two directions; eat a virus
  with controlled fragmentation.

### Eject mass (`W`)

- Drops an 18-mass food pellet in the cursor direction, fired at high velocity.
- Minimum mass to eject: **150**. Caps so you can't reduce yourself below ~50.
- Tactical uses:
  - Feed a virus until it pops (it explodes any cell it touches at 200+ mass).
  - Trade mass with another player (gift food).
  - Slip through a tight gap by shrinking briefly.

### Dash (`Shift`)

- 0.4s sprint at 3× normal speed.
- 0.15s of invulnerability at the start (the "i-frames" window).
- Spends 8% of current mass.
- Cooldown: 4 seconds. Visible as a ring around your cell.
- Tactical uses: close a gap onto fleeing prey; escape a predator's lunge; reach
  a pickup before someone else.

### Mass Blast (Q)

- Radial shockwave that pushes every enemy cell + nearby food outward.
- Source: your **largest** cell. Costs **20% of its mass**.
- Minimum cast mass: **300** (HUD shows "NEED MASS" when below).
- Blast radius and push strength scale with the source cell's size — a giant
  player gets a giant shockwave.
- Doesn't push:
  - Your own cells.
  - Cells hiding inside a black hole.
  - Cells with an active Shield pickup.
- Cooldown: 6 seconds. Live cooldown bar in the top-right HUD ramps red → orange
  → bright yellow as it refills.
- Tactical uses: peel a predator off you; clear space for an eat; combo into a
  split-and-chase.

---

## 4. Combat

### Eating

- The bigger cell absorbs the smaller one on overlap.
- **Mass ratio required**: bigger must be at least **1.25×** the smaller.
- **Overlap required**: at least **40%** of the smaller cell's radius.
- Result: predator gains 100% of prey's mass; prey dies (DeathEvent fires).

### Crit

- 5% chance per absorb, plus a guaranteed crit when overlap is near-perfect
  (≥ 95%).
- Awards **1.5×** mass.
- Visual: chroma fringe + sharp sting + screen shake (player only).

### Near-miss

- Either:
  - You nearly ate a prey that escaped (`prey-side`) — bright gold sparks.
  - You nearly *got* eaten (`hunter-side`) — red vignette + "CLOSE ONE" text.
- Triggered when the would-be absorb fell *just* short of the overlap threshold.

### Combo

- Eating any cell or food increments your combo counter.
- Decay: 2.5 seconds without an eat resets the counter to 0.
- Visible as `x2`, `x3`, ... in the top-right HUD; the number gets bigger as
  combos grow.

### Death

- Your cell dies the instant it's absorbed (no shield), eaten by the comet, or
  forcibly destroyed by a virus split chain.
- Per-cell death: if you have multiple cells (after a split), losing one piece
  doesn't end the run. The run ends only when **all** of your cells are gone.
- Death cam: 1.5s slow-zoom on the killer. Skippable.
- Summary panel: final mass (peak across the run), best combo, time alive, XP
  earned, mission progress. PLAY AGAIN or MAIN MENU.

---

## 5. Power-ups

Three pickup types spawn sparsely across the map (~12 at a time). Consumed on
overlap with any cell.

| Pickup | Icon | Effect | Duration |
|---|---|---|---|
| **Shield** | Cyan globe | Can't be absorbed. Comet won't kill you. | 4s |
| **Magnet** | Orange globe | Nearby food (within ~800 units) drifts toward your cell. Strength ramps with distance. | 5s |
| **Stealth** | Purple globe | Bot AI ignores you as a target. (Doesn't hide you from human players.) | 4s |

Each effect has an aura ring around the cell while active.

---

## 6. World features

### Food

- Steady-state: ~3600 food pellets across a 16k × 16k map.
- **Tiers** (rolled per pellet):

  | Mass | Color | % Chance | Notes |
  |---|---|---|---|
  | 1 | green | 70% | common |
  | 3 | lime | 20% | uncommon |
  | 6 | cyan | 8% | rare, pulsing halo |
  | 12 | gold | 2% | epic, strong halo |
  | 36 | purple-red | 0.5% | **legendary**, pulsating |

### Viruses

- 60 green spike-balls scattered across the map.
- A cell ≥ 200 mass that touches a virus **splits into 8 pieces** and gets
  scattered outward. Brutal punishment for over-greedy growth.
- Cells below 200 mass pass through harmlessly.
- Viruses drift slightly when ejected mass is fed into them; eventually they
  pop.

### Black holes (5 per match)

- **Pull radius** (220 units): cells within this range accelerate toward the
  center.
- **Event horizon** (70 units): crossing this triggers **hiding** — you're
  pinned to the BH center, invisible to predators, can't eat or be eaten.
- **Stamina**: starts at 1.0, drains at 1/9 per second while hiding.
  Refills at 1/15 per second while in the open world.
- **Exit**:
  - Stamina hits 0 → auto-ejected.
  - You aim the cursor outside the BH disc → ejected toward the cursor.
  - Bots auto-eject on stamina drain only.
- Smooth shrink-in / grow-out animation (0.35s).
- Min separation between any two BHs: 4000 units.

### Crashing comet (world event)

- Periodic. First spawn at ~45s, then every ~75s ± 30% jitter.
- **Telegraph** (3s warning): glowing path line on the world + minimap, alarm
  audio + "COMET INCOMING" HUD banner.
- **Active**: fire-blazing sphere sweeps across the map at 900 u/s. Touches a
  cell within ~1575 units → instant kill. Visualised with a fire shader (head)
  plus a smooth shader-driven trail and fiery embers radiating from the body.
- **Immunity**: cells hiding inside a black hole, and cells with an active
  Shield pickup, survive a comet pass.
- Despawns once it exits the world. The kill is logged as **COMET** in the
  killfeed.

### Comet shower (rare world event)

A formation of comets — one big main + 4–9 smaller satellites — sweeps across
the map together. Rarer than single comets (default first at 90s, then every
~180s), but when it lands the whole map is a kill grid for a few seconds.

- **Composition**: 1 **main** comet at radius 550 (smaller than the
  single-comet's 1575 — the formation should read as a swarm, not one giant
  with sprinkles) + N **satellites** where N ∈ [3, 6] (so 4–7 comets total).
  Satellite radius rolls uniformly in [175, 350].
- **Color variants**: main is the original **Orange** fire palette.
  Satellites are split 50/50 between **Red** (crimson body, near-white core)
  and **Blue** (cobalt body, white-blue core). Telegraph lines + minimap dots
  use matching palettes so the formation is legible from any view.
- **Spread**: all comets share the same world-spanning velocity direction
  (the formation flies in formation). Satellites scatter ±1800 units
  perpendicular to the main path and −1500..+600 longitudinally, so they
  arrive in a staggered wave rather than a wall.
- **Telegraph + kill rules**: identical to the single comet — 3s telegraph
  per comet, instant kill on contact for cells without Shield / Black-hole
  hide. Each comet emits its own `CometEvent::Telegraph / Active /
  Despawn`.
- **Minimap**: rainbow fan of coloured telegraph lines during the warning,
  one coloured dot per active comet.
- **Bot AI**: bots compute a *combined* dodge vector across every threat
  (active or telegraphed) instead of locking onto the first one — so they
  steer for the gap rather than diving sideways into the next comet over.
  In genuine sandwich situations (parallel threats from opposite sides)
  they fall back to dodging the closest active threat.
- **Tuning**: `[comet_shower]` section — `event_interval_sec`,
  `first_after_sec`, `main_radius`, `satellite_min/max`,
  `satellite_min/max_radius`, `spread_perp`, `spread_along`.

### Tidal current bands (ambient terrain)

- **What they are**: 2 horizontal river-style bands (default
  `tidal_band_count=2`) stretching the full world width. The upper band
  flows **left → right**; the lower band flows **right → left** -- like
  opposing ocean currents at ~33% and ~66% world height. With more bands
  (set in tuning) the layout generalises to evenly-spaced strips with
  alternating direction.
- **Visuals**: a soft cyan strip with smooth feathered top/bottom edges,
  a streaming particle field flowing along the direction, and a row of
  bright cyan chevrons marking the flow at ~1200 px intervals. Particles
  + chevrons scroll with the flow so the direction reads even at high
  zoom-in.
- **Effect**: any cell whose y-coordinate falls inside the band gets
  **drifted** in the band's direction each tick (translated directly,
  bypassing the seek/velocity pipeline). Smoothstep falloff (full at the
  centre line, 0 at the rim) so cells entering / leaving don't snap into /
  out of the flow. Drift scales as `strength / (mass / start_mass)^0.25`
  (4th-root attenuation). The gentle curve keeps medium-mass cells in the
  current's grip, roughly matching their own speed loss with size:
  - 100-mass cell → 360 px/sec drift (vs base_speed 280). Swept along
    faster than it can swim.
  - 400-mass cell → ~255 px/sec drift. Still pushed faster than it can
    swim against (cellSpeed ~164 at that mass).
  - 1600-mass cell → ~180 px/sec. Still faster than cellSpeed (~121).
  - 5000-mass cell → ~135 px/sec. Comparable to its own swim speed.
  - 10 000-mass mega-cell → ~114 px/sec. Noticeable but no longer
    overwhelming -- a big fish can still cross the river.
  Mass-based, never lethal. The player's seek still works on top -- aim
  the cursor north and the cell moves north while drifting east, exactly
  like a river current. To stay stationary inside a band, aim the cursor
  "upstream" (and small cells may not be able to hold position at all).
- **Tactics**: ride the upper band east to chase a fleeing target;
  ride the lower band west to flank the spawn line. The bands cross the
  entire world so a small cell can use them as a free highway between
  zones, while a mega-cell barely cares -- balancing the late game.
- **Where they sit**: with `band_count=2` (default) the bands are placed
  at world-y ≈ 33% and 67% with `band_height=650` (so each band is
  ~1300 px thick out of a 16k-tall world -- like rivers, not oceans).
  The spawn area at world centre sits in the current-free corridor
  between them; cells need to move ±~2000 px from spawn to enter a band.
- **Minimap**: thin horizontal cyan strips spanning the full minimap
  width with a small direction tick at the centre.
- **Tuning**: `[currents]` `band_count` (default 2; 0 disables) /
  `band_height` (default 1400 = vertical half-reach) / `band_strength`
  (default 130 = base px/sec).

### Wormhole pairs

- **What they are**: 2 pairs (4 endpoints; `wormhole_pair_count=2`) of
  linked teleport portals. Each endpoint is paired with exactly one other --
  step into one and you instantly exit at the partner with your momentum
  preserved.
- **Visuals**: spinning indigo vortex with rotating spokes, a bright violet
  singularity core, and two diametric tendril pips. The two endpoints don't
  draw a connecting line in the world (would clutter the play area) but the
  minimap shows the pair link with a thin purple line so you can plan
  teleports from a glance.
- **Cooldown**: per-cell 3s cooldown after a teleport (`wormhole_cooldown_sec`).
  Stops loop-back when the partner's radius is right next to whatever you
  were running from. Hiding cells (in a black hole) and cells mid-BH
  animation are skipped.
- **Tactics**: bait predators toward an endpoint, then warp out. Surprise-
  flank from the partner side. Combine with Dash for an explosive entry.
- **Tuning**: `[wormholes]` pair_count / radius / cooldown_sec /
  pair_min_distance / min_separation. Set pair_count = 0 to disable.

### Mass geysers (periodic food objective)

- **What they are**: 5 stationary points (`geyser_count=5`) that erupt food
  on a timer. Each cycles **Idle → Telegraph → Erupt → Idle** independently;
  the first geyser fires at `geyser_first_after_sec=15`, subsequent eruptions
  are spaced `geyser_interval_sec=28` ± 30% jitter. Geysers are spawned with
  a half-interval stagger so they don't all erupt simultaneously.
- **Telegraph** (3s warning): a growing ring around the geyser shifts cyan →
  orange as the eruption approaches; the ring's alpha pulses faster the
  closer it gets. A minimap pip flashes amber.
- **Eruption**: 14–22 food pellets (random per eruption) spawn in a radial
  sunburst with outward velocity (520 u/s). Mass roll is biased toward rare
  drops (commons get a 50% re-roll), so eruptions feel meaningfully better
  than ambient food. The pellets decay outward via the normal food motion
  step.
- **Tactics**: a hot zone for fights. Camp the rim during telegraph; blast
  competitors away with Q just before eruption; dash in to scoop the
  centre. Bots are aware (see Bot AI below) so you'll have company.
- **Minimap**: small teal pip when idle, amber pip during telegraph,
  bright burst dot during eruption.
- **Tuning**: `[geysers]` count / radius / interval_sec / interval_jitter /
  telegraph_sec / first_after_sec / food_count_min/max / food_eject_speed /
  food_spread / min_separation.

### Bot AI (VS AI / SinglePlayer)

Six personalities with distinct behaviours. Each gets a letter in the
killfeed / leaderboard so you can tell them apart at a glance.

| Letter | Name | Niche |
|---|---|---|
| `G` | Greedy | Food monarch. Huge food vision, strongest value weighting. Rarely chases cells. |
| `C` | Cautious | Survival specialist. Long flee range, panic dashes. Never blasts; never elite-spawns. |
| `H` | Hunter | Apex predator. Lead-aim chase, sticky lock-on (4s), dash telegraph, mid blast usage. |
| `h` | Hoarder | Corner camper. Wide bite radius, splits aggressively when prey ventures into the corner. |
| `R` | Reckless | Chaos lord. Locks onto the player, high split + blast usage, no dash telegraph. |
| `A` | **Apex** | Late-game terror. Elite-only, spawns when the player reaches ≥ 5000 mass. Huge (110–160% of player mass), sticky chase, 95% blast aggression, locks onto the player hard. |

**Elite scaling** — as the player grows, a rising share of new bot spawns
become "elites" with mass scaled to your current peak. Elite chance ramps
to 70% by mid-game. Elites can be up to 140% of your peak mass (Apex up to
160%), so big-mass players face individual bots that can eat them.

**Pack flank** — every Hunter and Apex bot is spawned with a random flank
angle. When they're all chasing the player, each approaches from a
slightly different perpendicular offset (scaled by distance), so a group
ends up surrounding you instead of stacking on the same chase line.

**Bot Q-blast (smart use)** — only Hunter / Apex / Reckless personalities
ever blast (Greedy / Hoarder / Cautious never do). The fire heuristic
keeps them from spamming the 6-second cooldown:

- **Chase blast**: only fires when the prey is the *human player* AND
  they're in the sweet range (30%–65% of blast radius) — pure
  disruption against your split-escape — OR when **3+** enemy cells
  cluster within blast radius (multi-hit value justifies the cost).
- **Panic blast**: only fires when the threat is **close** (< 55% of
  blast radius) AND **meaningfully bigger** (≥ 1.4× the bot's mass),
  so spending 20% mass actually saves them.

Bots use the same cooldown (6 s) and cost (20% of source mass) as the
player.

**Geyser hunting** — when a bot would otherwise wander randomly
(no prey, no nearby food it cares about), it scans for the nearest
geyser within ~4500 px that's actively telegraphing or erupting.
Telegraph-state geysers get a 1.2× weight vs erupt-state 0.7× (bots
prefer to be **in position** when the food appears rather than chase
pellets after the fact). This creates natural contested zones around
upcoming eruptions without any hand-written "cluster around an
objective" behaviour.

**Comet dodge** — every bot iterates **every active + telegraphed
comet** and checks whether it sits in any of their forward kill lanes
(perpendicular distance < 1.6× the comet's radius, ahead of the comet
by ≤ 7 radii for active comets, full path range for telegraphed
ones). Each threatening comet contributes a perpendicular unit-vector
toward its closer escape side; the dodge target is `self + 1200 ×
normalize(sum of those unit vectors)`. So one threat = perpendicular
sprint, parallel threats from the same side = amplified perpendicular,
threats from opposite sides cancel and the bot falls back to dodging
the closest active threat individually. EMA smoothing is bypassed
during the dodge so the swerve registers immediately. This logic is
what keeps bots alive during a 10-comet shower instead of having them
all dive sideways into the next comet over.

### Mutations (per-match world trait)

Every match rolls one **Mutation** at start — a randomized world trait
that modifies the active Tuning before the simulation ticks. Goal: no
two matches feel the same. The mutation is deterministic per seed (same
seed → same mutation), so replays + the determinism test keep working.

In multiplayer the host rolls and the kind is shipped to every joining
client in the `WelcomeMsg` (wire version bumped to 3) — both sides
apply the same Tuning modifications so client-side prediction stays
consistent with host physics.

**HUD reveal** — a centered "MUTATION: \<NAME\>" banner with the
flavor-text tagline fades in over 0.4 s, holds for 4.5 s, fades out
over 1.1 s. After fade, a tiny `[Name]` badge in the top-left stays
visible for the rest of the match so the player remembers what's
warping the world.

| Mutation         | Effect                                                                                 |
|------------------|----------------------------------------------------------------------------------------|
| **Feast**        | `food_target × 2`, `pickup_target × 2`. Easy growth, fast escalation.                  |
| **Bloom**        | `food_target × 2` (no pickup change). Gentler-than-Feast food bump.                    |
| **Famine**       | `food_target / 3` (floor 50), `pickup_target / 2`. Scavenger hunt.                     |
| **Outbreak**     | `virus_count × 3`. Map is a minefield -- splits are dangerous.                         |
| **Speed Demon**  | `base_speed × 1.5`. Twitch reflex match.                                               |
| **Heavy**        | `base_speed × 0.6`, `speed_falloff × 0.5`. Slower base but mass barely slows you.      |
| **Comet Storm**  | `comet_event_interval_sec × 0.25`, `comet_first_after_sec × 0.33`. Constant threat.    |
| **Blackhole Fever** | `blackhole_count × 3`, `blackhole_min_separation × 0.5`. Swiss cheese map.           |
| **Geyser Rush**  | `geyser_count × 3`, `geyser_interval_sec × 0.33`, separation halved. Food fountains.   |
| **Pickup Frenzy**| `pickup_target × 4`. Shield / magnet / stealth uptime feels permanent.                 |
| **Tidal Surge**  | `tidal_band_strength × 2.5`. Currents push faster than cells can move.                 |
| **Glass Cannon** | Cheaper splits / blasts / dashes (mass thresholds × 0.6, cost × 0.7, cooldowns × 0.5). |

**None** is also a valid kind — used for forward-compat (a newer host
sends an unknown index) and as the test/dev opt-out. Vanilla SP matches
launched outside `runMatch` (e.g. headless determinism test) never see
a mutation; only `runMatch` rolls one.

**Tuning**: no `tuning.ini` keys yet — mutations modify the loaded
tuning in place. To force-disable the system for a debugging session,
comment out the `applyMutation(tuning, active_mutation)` call in
`runMatch`. To force a specific mutation, replace the `pickRandomMutation`
call with the desired `MutationKind` value.

---

## 7. HUD reference

| Element | Position | What it shows |
|---|---|---|
| Leaderboard | Top-left corner | Top 15 players by total mass (summed across all of their cells). Each row: rank, cell-coloured dot, **display name** when known (player's chosen handle from Settings, or peers' names received via the welcome handshake), otherwise letter+id (e.g. `P1`, `H7`), mass. Your row is highlighted in gold. If you're outside the top 15, a separate "your rank" row is appended below with the same gold highlight. Builds from the latest snapshot so it works identically in SinglePlayer / LocalHost / LocalClient. |
| Match timer | Top-center | `mm:ss` countdown of the match's remaining time. Hidden when `match_duration_sec == 0` (open-ended SP / sandbox). Pulses red in the last 10 seconds to signal the end is close. Built from `Snapshot::match_time_left_sec` so it works uniformly across modes. |
| Combo counter | Top-right (under Q-bar) | Pulsing `xN` when ≥ 2. Bigger / dimmer with chain length. |
| Killfeed | Top-right, below combo | Last 5 inter-player kills. Player-involved kills get a gold accent. Comet kills label predator as **COMET** in hot orange. |
| Q (Mass Blast) cooldown bar | Top-right corner | 220×18 px. Red → orange → bright yellow as it refills. "BLAST" overlay when ready; "NEED MASS" overlay when biggest cell is below 300. Subtle pulsing glow when castable. |
| Minimap | Bottom-right corner | 200 px square. Shows black holes, all live cells (size scales with mass), comet path + position, tidal current bands (thin cyan horizontal strips with a direction tick), wormhole pairs (purple endpoint dots with a thin link line between them), geysers (teal pip / amber telegraph / burst flash), and the camera frustum. Your cells get a white outline; elites get a red outline. |
| Black hole stamina bar | Bottom-center (only while hiding) | Drains as your hide-time runs out. Hue shifts toward red when nearly empty. |
| Debug stats | Bottom-left | FPS, current sim tick, watched cell mass + pos, zoom, dt multiplier. Tagged `[PAUSED]` when paused. |
| Near-miss / crit flashes | Full-screen | Brief red / gold tint on close-encounter events. |
| Comet warning banner | Top-center | "!! COMET INCOMING !!" with fade in/out + dark backdrop strip. |
| Mutation banner | Top-center (match start) | "MUTATION: \<NAME\>" + flavor-text reveal. Fades in over 0.4s, holds 4.5s, fades out over 1.1s. |
| Mutation badge | Top-left (post-reveal) | Tiny `[Name]` reminder of the active world trait. Stays for the rest of the match. |
| Summary panel | Center (on death) | Final mass, best combo, time alive, XP, missions. PLAY AGAIN / MAIN MENU. |

---

## 8. Progression

- **XP per run**: scales with peak mass + missions completed during the run.
- **Levels**: quadratic curve (L2 = 100 XP, L3 = 400 XP, L4 = 900, ..., L10 =
  8100). Each level takes more XP than the last.
- **Cosmetic unlock**: a small `*` flair on your name prefix at level 20+.
- **Best mass / best combo**: lifetime stats; visible on the main menu's stats
  panel.
- **Daily missions**: rolled once per real-world day. Examples: "eat 50 food
  this run", "reach 1000 mass", "land 3 crits", "survive 2 minutes". Progress
  persists; rewards are XP.

---

## 9. Multiplayer (Royale → Local)

### Architecture

- **Authoritative sim** lives on the host.
- **Snapshots** (every sim tick) and **events** (per gameplay moment) are
  broadcast over UDP via ENet.
- **Clients** do NOT tick their own sim; they render received snapshots and
  forward their inputs back to the host.

### Network layout

| Channel | Direction | Payload |
|---|---|---|
| 0 | Client → Host | Commands (Move / Split / Eject / Dash / Blast / Respawn) |
| 1 | Host → Clients | Snapshots (world state) |
| 2 | Host → Clients | Game events (absorbs, deaths, blasts, comet phases, ...) |
| 3 | both | Control: `Welcome` (host→peer, with host name), `ClientHello` (peer→host, with peer name), `PeerInfo` (host→all peers, PlayerId + name). Discriminated by leading type byte. |

All channels are reliable. Default port: **UDP 7456**.

### Lobby flow

The lobby holds the socket open *before* the match starts so peers can join,
exchange names, and see the player list while waiting. The host clicks
`START GAME` when everyone is in.

1. **Host hits `HOST`** in the picker.
   - UDP socket opens on port 7456 immediately.
   - LAN announcer starts broadcasting on the discovery port range
     (47457–47459) so peers on the same network see the host in their
     `JOIN` browser within ~1 second.
   - Host's `LocalLobby` transitions to `HostWaiting` sub-state; the screen
     splits into two columns: **Match Settings** on the left (the host can
     tweak duration / max players / bot count while waiting) and **Players**
     on the right (live roster, updates as peers join).

2. **Peer hits `JOIN`** in the picker and either picks a discovered host or
   types a direct `host:port` address.
   - Peer's `LocalLobby` transitions to `JoinWaiting` sub-state.
   - `NetworkTransport::connect()` is called; UDP socket opens.
   - Peer waits on the "waiting for host..." screen until the host's
     welcome arrives.

3. **Each peer's CONNECT event** lands in the host's lobby loop, which:
   - Allocates the next free `PlayerId` (slot 1 stays the host's).
   - Adds the peer to `peer_to_player` but **does not spawn a cell yet**
     (the match hasn't started — there's no sim ticking).
   - Ships a **lobby Welcome** `WelcomeMsg{player_id, cell_id=INVALID,
     host_name}` so the joiner knows their slot + the host's display name.
   - Catches the new peer up on every already-known peer's name via
     `PeerInfoMsg`s.

4. **Peer receives the lobby Welcome** (recognised by `cell_id == INVALID`):
   - Records the host's name + own player slot.
   - Sends `ClientHelloMsg{name}` upstream so the host learns its name.
   - Updates the lobby player list panel (host + self + any other already-
     joined peers from PeerInfo messages).

5. **Host receives `ClientHelloMsg`**, registers the peer's name in
   `known_peer_names`, and broadcasts `PeerInfoMsg{pid, name}` to **all**
   connected peers (so everyone's lobby panel stays in sync).

6. **Host hits `START GAME`** (only the host has this button in `HostWaiting`).
   - `runWindow` hands the live `NetworkTransport`, `peer_to_player`, and
     `known_peer_names` down to `runMatch` (no reconnect — peers stay
     connected).
   - `runMatch` iterates the pre-joined peer map, spawns a cell for each at
     a clear position, and ships a **match Welcome** `WelcomeMsg{player_id,
     cell_id=valid_id, host_name}` per peer.
   - The host begins ticking the sim + broadcasting snapshots.

7. **Each peer's lobby loop** sees the match Welcome (recognised by
   `cell_id != INVALID`) and transitions out of `JoinWaiting` into
   `AppPhase::Match`. The first snapshot lands within ~1 frame and rendering
   begins.

Backing out of `HostWaiting` (`CANCEL` button or `Esc`) disconnects the host
socket and stops the LAN announcer — peers will see their connection drop and
return to the picker. Backing out of `JoinWaiting` (`LEAVE LOBBY` or `Esc`)
disconnects the peer; the host sees their slot disappear via
`pollDepartedPeer`.

### Match settings (host-only, in HostWaiting)

The HostWaiting screen shows a settings panel on the left side. All values are
preset chips — click any chip to select it. Changes apply when the host hits
`START GAME`. Settings persist across HostWaiting visits within the session
(reset on `LocalLobby::reset`, which fires when leaving the lobby entirely).

| Setting | Presets | Notes |
|---|---|---|
| **Match duration** | `1m` / `3m` / `5m` (default) / `10m` / `ENDLESS` | Endless disables the timer so the match runs until everyone leaves. Otherwise the host's sim emits a `MatchEndEvent` when the clock hits zero. |
| **Max players** | `2` / `4` / `8` (default) / `16` | Hard cap on host + peers. Connections beyond the cap are accepted by ENet then immediately disconnected with a `[net] late-joiner rejected` / `[lobby] rejecting peer` log line. Enforced both during the lobby (`pollNewPeer` reject) and during the match (late-joiner gate). |
| **Bots** | `OFF` (default) / `5` / `10` / `25` | Number of AI cells to spawn at match start. Overrides the Royale default of 0. The host can also use `bots N` in the dev console mid-match to retarget. |

`MatchSettings` lives on `LocalLobby`. `runMatch` reads them from
`MatchNetworkConfig.{match_duration_sec, max_players, bot_count}` and writes
the corresponding `Tuning` fields on entry; the original values are restored
at the single return point so a follow-up SP / VS-AI match doesn't inherit
the Royale overrides.

### Rendering on the client

- The host broadcasts a snapshot at 30 Hz. The client interpolates between the
  previous and most-recent snapshot using an alpha that sweeps 0 → 1 between
  them (driven by wall-clock time, since the client doesn't tick its own sim).
  Cells move smoothly at the render rate, not snap-by-snap.
- The camera follows the **interpolated** cell position (`Interpolator::cellPos
  (id, alpha)`), so the rendered cell stays locked to screen centre rather than
  trailing one tick behind.
- **Client-side prediction** for your own cell: the watched cell advances toward
  the latest move target every frame using the same seek + speed math the host
  runs. The host's snapshot is the authoritative truth — drift < 80 px lerps
  toward it (25% per frame, near-invisible drift washout); drift ≥ 80 px snaps
  hard (a split / virus / blast just rearranged things). The renderer + camera
  both see the predicted position, so cursor moves translate to visible motion
  with zero round-trip lag.
- Other cells (bots, peers) remain server-authoritative + interpolated; only
  the local player's own watched cell is predicted.

### Respawn flow

- **Host's own death** spawns a fresh cell locally (direct sim mutation).
- **Client's death** routes through a `RespawnCmd`: client sends it up after
  the Summary panel countdown (or after PLAY AGAIN), host's sim runs
  `doRespawn` deterministically, the new cell appears in the next snapshot,
  and the client's camera follow re-acquires it. The client's HUD transitions
  back to Playing once any cell owned by its slot is visible in the snapshot.
- Idempotent: a duplicate `RespawnCmd` while the player still has cells is a
  no-op.

### Pause semantics

- **Single-player**: Esc opens the pause overlay AND freezes the sim
  (`effectiveDtMultiplier` returns 0). Buttons: RESUME / MAIN MENU.
- **Multiplayer**: Esc opens the same overlay but the sim KEEPS ticking
  -- pausing the host would stop snapshots for everyone; pausing a
  client just hides its local view. Buttons read RESUME / END MATCH
  (host) or RESUME / DISCONNECT (client). The overlay also displays a
  subtitle reminding the player that the world is still alive.
- The `pause` dev console command works in both modes (toggles the
  overlay; SP also freezes the sim).

### Disconnect flow

- **Player-initiated**: clicking DISCONNECT / END MATCH on the pause
  overlay sets the return-to-menu flag. After cleanup the host or
  client returns to the LocalLobby (Picker sub-state).
- **Host-initiated tear-down**: when a host clicks END MATCH (or
  triggers `kick PID` / window-closes), ENet sends DISCONNECT to all
  peers. Each client's `NetworkTransport::hostDisconnected()` flips
  true, the match loop detects it, and the client returns to the
  lobby with a `[net] host disconnected -- returning to lobby` log.

### Disconnect cleanup

- When a peer closes their window (or gets kicked), ENet fires a DISCONNECT
  event on the host. The host maps the ENet peer back to its PlayerId via an
  internal `peer_to_player` table, removes every cell owned by that player
  from the world, and frees the PlayerId slot. Other peers see the leaving
  cells vanish in the next snapshot.
- The host can also issue a graceful kick from the dev console:
  `kick <PlayerId>`. Behaviour is identical to a peer that left on their own.
- Refuses to kick PlayerId 1 (the host's own slot) and any PID not currently
  bound to a peer.

### Match end + winner

- **Timer**: every Royale match runs on a configurable clock
  (`match_duration_sec`, default 300 = 5 min in MP, 0 / disabled in SP).
  Snapshots carry `match_time_left_sec` so every client renders the same
  countdown in the top-center HUD strip.
- **Winner**: when the timer hits zero, the host's sim emits a single
  `MatchEndEvent` carrying the highest-total-mass player at that instant.
  Ties go to whichever owner the cells vector lists first (deterministic
  via the sim's iteration order).
- **Overlay**: clients enter `GamePhase::MatchEnd`, the winner card pops
  up (winner name + final mass + "returning in N..." countdown). The
  player's own name highlights green if they won.
- **Return**: after 6 seconds (or on click / Space / Enter) the client
  triggers a return-to-menu. SP lands in the main menu; MP lands in the
  LocalLobby. The host's runMatch teardown sends DISCONNECT to all
  peers; clients that haven't auto-returned yet bounce on the
  host-disconnect path.

### Player names

- **Source**: set in Settings → IDENTITY → "Player name" (max 16 ASCII chars).
  Persisted across runs in `save.bin` (v5+ field).
- **Display fallback**: when a player has no name set (or peers haven't shared
  theirs yet), the HUD shows the legacy `<letter><id>` label — e.g. `P1` for
  the human player, `H7` for a Hunter bot. Killfeed / leaderboard / cell
  nameplate all use the same fallback uniformly.
- **Multiplayer name sync**: handshake on CHAN_CONTROL with three message
  types (discriminated by a leading type byte):
  - `Welcome` (host → joining peer): carries the host's display name so
    the new peer immediately sees who's hosting.
  - `ClientHello` (peer → host): sent right after the peer consumes the
    Welcome. Carries the peer's display name.
  - `PeerInfo` (host → all peers): host broadcasts this when it learns a
    new peer's name. Also sent for every existing peer to a fresh joiner
    so they don't have to wait for other peers to re-announce.
- **Bots**: never have names; always render via the `<letter><id>` fallback.
- **Renderer global**: cell nameplates above each cell read from a
  process-wide `s_player_names` table that Client mirrors into when its own
  `setPlayerName` is called.

### LAN discovery

- Host: while a match is running in LocalHost mode, a side UDP socket
  broadcasts a tiny "I'm hosting on port N" announce packet every ~1 second.
  Sent to **both** the LAN broadcast address (`255.255.255.255`) AND the
  loopback (`127.0.0.1`) so same-machine two-instance testing works on macOS
  (where 255.255.255.255 doesn't loop back).
- Client: the JOIN screen opens a listener the moment you enter it. Each
  received announce shows up as a clickable row (click → connects to that
  host's game port). Entries older than 5 seconds drop off automatically.
- Ports: the host broadcasts to the range `udp/47457 .. udp/47459`. The
  client tries to bind `47457` first; if that's taken by a background daemon
  (`EADDRINUSE`) it falls back to `47458`, then `47459`. With three slots,
  it's unlikely all three are busy. If all are: discovery silently fails and
  manual host:port entry still works.
- The announce packet is 39 bytes — magic `CRDS` + version + game port + a
  32-byte display name. Loss tolerant: the next 1 s broadcast repopulates.

### Known limitations (current state, will fix)

- Lobby-time bind isn't implemented. The UDP socket opens at START, not in the
  HOSTING screen. JOIN can only discover hosts whose match has already
  started.

---

## 10. Dev console

Opens with `` ` `` (backtick / tilde). Available in dev builds
(`CR_ENABLE_DEV_TOOLS=ON`, which is the default).

### Host-only commands

These mutate the authoritative sim. They're refused on a multiplayer client
with a `host-only` log line.

| Command | What it does |
|---|---|
| `bots N` | Set the bot target count to N. Despawns excess bots immediately; the director respawns up to N over a few seconds. `bots 0` clears the AI completely; `bots 50` brings a full swarm. |
| `tp X Y` | Teleport every cell you own to world coordinates (X, Y). Coords clamp into the playfield. |
| `set_mass N` | Set your watched cell's mass to N. |
| `god` | Toggle invuln on your watched cell (the cell can't be absorbed). |
| `comet` | Force-spawn a crashing-comet event on the next sim tick. The spawn position + direction remain RNG-driven. |
| `shower` | Force-spawn a comet-shower event (1 main + 3..6 satellites in red/blue) on the next sim tick. |
| `spawn_food N` | Drop N random-tier food at random positions. |
| `seed_food N [mass]` | Drop N food. Optional second arg pins the mass tier to one of `1`, `3`, `6`, `12`, `36`. |
| `kick PID` | **LocalHost only.** Gracefully disconnect the peer that owns `PID`, then despawn all of their cells. Refuses to kick `PID = 1` (the host) or any PID with no matching peer. |

### Client-friendly commands

Work in any mode, including a multiplayer client (they affect the local client
only).

| Command | What it does |
|---|---|
| `slowmo F` | Set the dt multiplier. `slowmo 1` = normal speed; `slowmo 0.25` = quarter-speed (cinematic / debug). `slowmo 2` = double-speed. |
| `pause` | Toggle the pause overlay. In SP also freezes the sim; in MP the sim keeps ticking and the overlay is purely a menu. |
| `reload_tuning` | Re-read `tuning.ini` from disk and push it into the sim + menu copies. |
| `replay_save FILE` | Write the in-memory replay tape to `FILE`. Records seed + initial cells + every command since match start. |
| `replay_load FILE` | Stub. Use `--replay-load FILE` on the CLI instead for now. |
| `set_hold_to_move 0|1` | When `1`, your cell only moves while the left mouse button is held. |
| `set_invert_thumbs 0|1` | Swap left / right virtual sticks (touch builds). |
| `force_touch 0|1` | Force the touch UI layout on a desktop build. |
| `vol_master F` | Master volume (0..1). |
| `vol_sfx F` | SFX volume (0..1). |
| `vol_music F` | Music volume (0..1). |
| `music_on` / `music_off` / `mute` | Music toggle / kill master. |
| `clear` | Clear the console output. |
| `help` | Print all commands. |

---

## 11. Tuning knobs (`tuning.ini`)

The full set is documented inline in `tuning.ini` itself. High-impact knobs:

| Section | Key | Default | Effect |
|---|---|---|---|
| `[cell]` | `base_speed` | 280 | Cell speed at radius 30. Larger cells move slower. |
| `[cell]` | `start_mass` | 100 | Spawn mass. |
| `[absorb]` | `mass_ratio_required` | 1.25 | Predator/prey ratio for an eat. Lower = more aggressive eats. |
| `[split]` | `min_mass_to_split` | 200 | Smallest cell that can split. |
| `[split]` | `launch_velocity` | 700 | Split-launch speed. |
| `[blast]` | `radius` | 600 | Blast reach at min cast mass. Scales with caster size. |
| `[blast]` | `cooldown_sec` | 6 | Q recharge time (longer than dash so bots can't spam, and player blasts feel meaningful). |
| `[dash]` | `cooldown_sec` | 4 | Dash recharge time. |
| `[world]` | `width` / `height` | 16000 / 16000 | Playfield. |
| `[bots]` | `target_count` | 50 | VS AI default. Royale modes (LocalHost / LocalClient) override to 0 at match start; the host opts in with `bots N`. |
| `[match]` | `duration_sec` | 0 | Total match length in seconds. 0 = unlimited (SP / sandbox). Royale modes override to 300 (5 min) when starting a match if the value is 0. When the timer hits zero the sim emits `MatchEndEvent` and the client shows the winner overlay before returning to lobby/menu. |
| `[blackholes]` | `count` | 5 | Black holes per match. |
| `[blackholes]` | `stamina_drain_sec` | 9 | Time from full → empty hide stamina. |
| `[comet]` | `event_interval_sec` | 75 | Mean time between comet events. |
| `[comet]` | `radius` | 1575 | Comet kill radius + visual size. |
| `[comet]` | `first_after_sec` | 45 | Delay before the first comet of a match. |
| `[comet_shower]` | `event_interval_sec` | 180 | Mean time between comet-shower events (separate cadence from single comets). |
| `[comet_shower]` | `first_after_sec` | 90 | Delay before the first shower of a match. |
| `[comet_shower]` | `main_radius` | 550 | Main (Orange) comet's kill radius during a shower. |
| `[comet_shower]` | `satellite_min` / `satellite_max` | 3 / 6 | Inclusive range for the satellite count (4..7 comets total counting the main). |
| `[comet_shower]` | `satellite_min_radius` / `satellite_max_radius` | 175 / 350 | Bounds on satellite kill radius. |
| `[comet_shower]` | `spread_perp` | 1800 | Perpendicular scatter of satellites around the main's path (each side). |
| `[comet_shower]` | `spread_along` | 1500 | Longitudinal scatter along the velocity axis (earlier / later landing than the main). |
| `[currents]` | `band_count` | 2 | Horizontal tidal-current bands stretching across the world. 0 disables the feature. |
| `[currents]` | `band_height` | 650 | Vertical half-reach of each band (total band height = 2× this). |
| `[currents]` | `band_strength` | 360 | px/sec drift applied to a `start_mass` cell at the band centreline; scales as `1 / (mass/start_mass)^0.25` (4th-root attenuation) and feathers smoothly toward the rim. Cell position is translated directly each tick (the seek/velocity layer is independent). Tuned slightly above base_speed (~280) so small cells get swept but can still fight the current. The 4th-root curve keeps medium-mass cells (400-1600 mass) firmly in the current's grip rather than the sqrt attenuation that previously made the band irrelevant once you grew past starter size. |
| `[wormholes]` | `pair_count` | 2 | Number of wormhole pairs (so 2 → 4 endpoints). 0 disables. |
| `[wormholes]` | `radius` | 120 | Capture radius of each endpoint. |
| `[wormholes]` | `cooldown_sec` | 3 | Per-cell teleport-cooldown so a warped cell doesn't immediately loop back. |
| `[geysers]` | `count` | 5 | Number of food-eruption geysers. 0 disables. |
| `[geysers]` | `interval_sec` | 28 | Mean time between eruptions per geyser. |
| `[geysers]` | `telegraph_sec` | 3 | Warning window before eruption. |
| `[geysers]` | `food_count_min/max` | 14 / 22 | Random pellet count per eruption. |
| `[geysers]` | `food_eject_speed` | 520 | Initial outward velocity of erupted food. |

`reload_tuning` re-reads this file at runtime — no restart needed.

---

## 12. Persistence

- **Save file**: `~/Library/Application Support/CellRoyale/save.bin` (macOS),
  `~/.local/share/cell_royale/save.bin` (Linux),
  `%APPDATA%/CellRoyale/save.bin` (Windows).
- Stores: level + total XP, lifetime stats, settings (volumes, palette,
  contrast, HUD scale, FPS cap, input prefs), daily mission progress.
- Atomic write (writes to `.tmp` then renames). Previous save is kept as
  `.bak` so a bad write doesn't wipe progress.
- Version chain: v1 → v2 → v3 → v4. Newer code reads older saves by zero-
  filling missing fields.

---

## 13. CLI flags

Passing flags to the executable:

| Flag | What it does |
|---|---|
| `--headless` | Run the sim without a window. Useful for fuzz / determinism tests. |
| `--ticks N` | Override total ticks in headless mode (default 5000). |
| `--seed N` | Force the initial RNG seed. |
| `--replay-save FILE` | After headless run, write replay tape to FILE. |
| `--replay-load FILE` | Replay a saved tape (sim runs the recorded commands). |

---

## 14. Accessibility

- **Colorblind palettes**: Default / Deuteranopia / Protanopia / Tritanopia.
  Selectable in Settings.
- **High contrast outlines**: Cell edges get a thicker white stroke against
  busy backgrounds.
- **HUD text scale**: 0.85× – 1.30×, settings preview included.

---

## Appendix: file layout

```
src/
  core/        Types, RNG, snapshot, commands, events, tuning.
  sim/         World, Simulation, Rules (physics + interactions), Replay.
  ai/          BotDirector + per-personality decision logic.
  client/     Renderer, Hud, Camera, Interpolator, IntroScreen,
              SettingsScreen, MainMenu, RoyaleMenu, LocalLobby, DevConsole,
              UiWidgets.
  feel/        ScreenShake, Hitstop, Particles, Popups, Audio, ChromaShift.
  transport/   ITransport, LocalTransport, NetworkTransport (ENet), Codec.
  meta/        SaveFile, Missions.
  platform/    Input (desktop + touch), Paths.
docs/
  game_mechanics/  this folder.
  roadmap/         development roadmap by phase.
```
