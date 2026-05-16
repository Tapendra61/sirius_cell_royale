# Project notes for Claude

## Always-on policies

### Keep `docs/game_mechanics/README.md` up to date

Whenever a change touches any of the following, update
`docs/game_mechanics/README.md` in the **same commit**:

- New / changed gameplay mechanic (abilities, combat math, world hazards,
  power-ups, food tiers, viruses, black holes, comet, etc.).
- New / changed keyboard or mouse binding, touch gesture, or input mode.
- New / changed dev console command (or any change to its host-gated status).
- New / changed game mode or menu flow (lobby states, run-end behavior, etc.).
- New / changed HUD element (combo, killfeed, bars, minimap, banners).
- New / changed multiplayer behavior (transport channels, handshake, respawn,
  pause semantics, etc.).
- New / changed CLI flag, save file field, or tuning.ini key worth surfacing.

The doc is the player + dev reference. If the source of truth changes,
the doc has to follow in lockstep — don't wait to be asked. Cross-check the
section headers (modes, input, abilities, combat, power-ups, world features,
HUD, multiplayer, dev console, tuning) before declaring a change "done."

### Don't break the determinism test

`./cell_royale_test` is the canary. After any sim-layer change, run it.
Sim A and Sim B must produce bit-identical snapshots at tick 1000 and tick
1500. The codec round-trip and replay round-trip must also pass.

### Mode-specific tuning overrides

Two tuning values that `runMatch` adjusts based on `MatchMode`, saved
on entry and restored at the single return point:

- `bot_target_count`: SP respects tuning.ini (50 by default). Royale
  (LocalHost / LocalClient) forces 0 — hosts opt in via `bots N`.
- `match_duration_sec`: SP keeps tuning.ini's value (0 = unlimited
  sandbox). Royale forces 300 (5 min) if tuning.ini says 0, so MP
  matches always end with a winner overlay.

Don't change these defaults without an explicit reason. Don't break
the VS-AI-on / Royale-off bot asymmetry or the SP-unlimited /
MP-timed match asymmetry.

### Multiplayer pause semantics

Esc opens the pause overlay in all modes, but the sim's freeze behaviour
differs:

- **SinglePlayer**: Esc / `pause` freeze the sim (`effectiveDtMultiplier`
  returns 0) AND show the overlay (RESUME / MAIN MENU).
- **Multiplayer (LocalHost / LocalClient)**: Esc / `pause` show the
  overlay but the sim KEEPS ticking. The host can't freeze peers and a
  client can't freeze the host -- the overlay is purely a "menu while
  you stand still". Buttons read RESUME / END MATCH (host) or RESUME /
  DISCONNECT (client). Clicking either disconnect button bounces the
  player back to the LocalLobby.

Don't gate Esc out of MP or remove the overlay. Don't have the sim
freeze in MP.

### Host-only commands

Any console command that mutates the authoritative sim (spawn, despawn,
teleport, set mass, god, comet, bots, food) must check
`s.mode != MatchMode::LocalClient` and log a clear `host-only` message
when refused. The pattern is already established — match it for new
mutators.

## Build + run

```sh
cd build
cmake --build . -j           # build
./cell_royale_test           # determinism + replay + codec round-trip
./cell_royale                # game
```

ENet is fetched at configure time when `CR_ENABLE_NETWORK=ON` (the default).
The first configure after enabling it pulls the repo; subsequent builds are
incremental.

## Source layout

- `src/core/` — Types, RNG, snapshot, commands, events, tuning.
- `src/sim/` — World, Simulation, Rules (physics + interactions), Replay.
- `src/ai/` — BotDirector + per-personality decision logic.
- `src/client/` — Renderer, Hud, Camera, Interpolator, IntroScreen,
  SettingsScreen, MainMenu, RoyaleMenu, LocalLobby, DevConsole, UiWidgets.
- `src/feel/` — ScreenShake, Hitstop, Particles, Popups, Audio, ChromaShift.
- `src/transport/` — ITransport, LocalTransport, NetworkTransport (ENet),
  Codec.
- `src/meta/` — SaveFile, Missions.
- `src/platform/` — Input (desktop + touch), Paths.
- `docs/game_mechanics/` — player + dev reference.
- `docs/roadmap/` — phase plan (gitignored).
