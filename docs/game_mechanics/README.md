# Cell Royale — Game Mechanics

A complete reference for everything you can do in the game. Covers both **VS AI**
(single-player) and **Royale → Local** (LAN multiplayer). The **Global** mode is a
placeholder; it'll match this document when it ships.

---

## 1. Game modes

| Mode | How to start | What it does |
|---|---|---|
| **VS AI** | Main Menu → `VS AI` (or press Enter / Space) | Single-player sandbox. Authoritative sim runs locally; bots fill the world (host-controlled via the `bots` console command). |
| **Royale → Local → Host** | Main Menu → `ROYALE` → `LOCAL` → `HOST` → `START GAME` | Opens a UDP listener on port 7456. Other players on your LAN can join. You own the sim; clients render snapshots you broadcast. |
| **Royale → Local → Join** | Main Menu → `ROYALE` → `LOCAL` → `JOIN` → enter `host:port` (default `127.0.0.1:7456`) → `JOIN` | Connects to a host. Your local sim stops ticking; you render the host's world and your inputs are forwarded upstream. |
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
| Mass Blast (Q) | `Q` | Spends 20% of your biggest cell's mass to push every enemy cell + nearby food radially outward. 4s cooldown. Min cast mass = 300. Blast radius scales with caster size. |
| Pause | `Esc` (in match, **single-player only**) | Opens the Pause overlay. RESUME or MAIN MENU buttons. In multiplayer Esc is silently ignored as a pause toggle (the host can't freeze the world for clients and a client can't freeze the host either). |
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
- Cooldown: 4 seconds. Live cooldown bar in the top-right HUD ramps red → orange
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
  cell within ~440 units → instant kill. Visualised with a fire shader (head)
  plus a smooth shader-driven trail and fiery embers radiating from the body.
- **Immunity**: cells hiding inside a black hole, and cells with an active
  Shield pickup, survive a comet pass.
- Despawns once it exits the world. The kill is logged as **COMET** in the
  killfeed.

---

## 7. HUD reference

| Element | Position | What it shows |
|---|---|---|
| Leaderboard | Top-left corner | Top 15 players by total mass (summed across all of their cells). Each row: rank, cell-coloured dot, letter+id (e.g. `P1`, `H7`), mass. Your row is highlighted in gold. If you're outside the top 15, a separate "your rank" row is appended below with the same gold highlight. Builds from the latest snapshot so it works identically in SinglePlayer / LocalHost / LocalClient. |
| Combo counter | Top-right (under Q-bar) | Pulsing `xN` when ≥ 2. Bigger / dimmer with chain length. |
| Killfeed | Top-right, below combo | Last 5 inter-player kills. Player-involved kills get a gold accent. Comet kills label predator as **COMET** in hot orange. |
| Q (Mass Blast) cooldown bar | Top-right corner | 220×18 px. Red → orange → bright yellow as it refills. "BLAST" overlay when ready; "NEED MASS" overlay when biggest cell is below 300. Subtle pulsing glow when castable. |
| Minimap | Bottom-right corner | 200 px square. Shows black holes, all live cells (size scales with mass), comet path + position, and the camera frustum. Your cells get a white outline; elites get a red outline. |
| Black hole stamina bar | Bottom-center (only while hiding) | Drains as your hide-time runs out. Hue shifts toward red when nearly empty. |
| Debug stats | Bottom-left | FPS, current sim tick, watched cell mass + pos, zoom, dt multiplier. Tagged `[PAUSED]` when paused. |
| Near-miss / crit flashes | Full-screen | Brief red / gold tint on close-encounter events. |
| Comet warning banner | Top-center | "!! COMET INCOMING !!" with fade in/out + dark backdrop strip. |
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
| 3 | Host → New Peer | Welcome (player_id + initial cell_id) |

All channels are reliable. Default port: **UDP 7456**.

### Lobby flow

1. Host hits `START GAME` in the lobby — the UDP socket opens, host begins
   ticking.
2. Each connecting peer:
   - Host allocates the next free `PlayerId` (slot 1 stays the host's).
   - Host spawns a fresh cell for them at a clear position with `start_mass`.
   - Host ships a `WelcomeMsg{player_id, cell_id}` on channel 3.
3. The peer's client receives the welcome, sets its watched cell, and begins
   rendering snapshots.
4. Inputs from each peer flow upstream as Commands; host queues them into its
   sim alongside its own inputs.

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

Esc and the `pause` dev console command are both **single-player only**.
Pausing the host would stop snapshots for clients; pausing a client would
just freeze its local view. Both are confusing in multiplayer, so the
shortcut and the command both refuse cleanly.

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
| `spawn_food N` | Drop N random-tier food at random positions. |
| `seed_food N [mass]` | Drop N food. Optional second arg pins the mass tier to one of `1`, `3`, `6`, `12`, `36`. |
| `kick PID` | **LocalHost only.** Gracefully disconnect the peer that owns `PID`, then despawn all of their cells. Refuses to kick `PID = 1` (the host) or any PID with no matching peer. |

### Client-friendly commands

Work in any mode, including a multiplayer client (they affect the local client
only).

| Command | What it does |
|---|---|
| `slowmo F` | Set the dt multiplier. `slowmo 1` = normal speed; `slowmo 0.25` = quarter-speed (cinematic / debug). `slowmo 2` = double-speed. |
| `pause` | Toggle local pause. **Single-player only**; refuses in multiplayer. |
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
| `[blast]` | `cooldown_sec` | 4 | Q recharge time. |
| `[dash]` | `cooldown_sec` | 4 | Dash recharge time. |
| `[world]` | `width` / `height` | 16000 / 16000 | Playfield. |
| `[bots]` | `target_count` | 0 | Default AI count. Use `bots N` to change at runtime. |
| `[blackholes]` | `count` | 5 | Black holes per match. |
| `[blackholes]` | `stamina_drain_sec` | 9 | Time from full → empty hide stamina. |
| `[comet]` | `event_interval_sec` | 75 | Mean time between comet events. |
| `[comet]` | `radius` | 440 | Comet kill radius + visual size. |
| `[comet]` | `first_after_sec` | 45 | Delay before the first comet of a match. |

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
