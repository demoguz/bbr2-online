# BBR2 Online 0.10.0: technical overview

This document describes how the mod adds online multiplayer to Beach Buggy Racing 2 (Steam build for Windows), for anyone who wants to review, maintain or build on it, including the game's developers. The mod, its source comments and its diagnostic log (`bbr2mod\netlog.txt`) are in English; only the optional install note KURULUM.txt is in Turkish.

All addresses below are RVAs in `Game_x64.exe` with PE timestamp `0x67C624B1` (image base `0x140000000`). They are collected in `src/game.h`. The mod refuses to start on any other build, and every inline hook checks the original bytes first: if they differ, that feature is skipped and a line is logged.

## 1. Overview

- **Loading.** `version.dll` is a proxy DLL. The game loads it from its own folder; it forwards the real `version.dll` exports (`src/proxy.c`, `src/version.def`) then checks the exe build and installs its hooks.
- **Hooks.** Mostly vtable slot replacement. Class vtables are found by RTTI name (`src/rtti.c`), so most hooks survive small code changes. A few free functions use inline hooks with byte-signature checks (`src/hook.c`).
- **Network.** ENet 1.3.18 over UDP, either directly (IP, default port 27420) or through a Steam tunnel (Steam lobby plus `ISteamNetworkingMessages`). The protocol is the same on both paths.
- **Authority.** Owner-authoritative state sync. Every car has an owner PC that simulates it. The host owns the bots. Other PCs show the car as a puppet that follows the owner's snapshots. The host acts as referee for the start and the results.
- **UI.** A native lobby inside the game's own menu system. The mod replaces a few UI assets that the Premium build never opens with generated screens.

| File | Role |
|---|---|
| `main.c`, `proxy.c`, `util.c` | DLL entry, export forwarding, log, crash handler, developer ini |
| `rtti.c`, `hook.c` | vtable lookup by class name, inline hook with signature check |
| `race.c` | reading and writing the game's race setup ("recipe"), game mode enter/exit/tick hooks |
| `online.c` | everything in a race: hold/release, countdown lock, snapshots, effects, results, leaving |
| `repl.c` | puppet correction on the PhysX actor |
| `net.c`, `session.h` | ENet session layer: handshake, lobby, race messages, common clock |
| `steamsvc.c`, `steamlink.c`, `helper.c` | Steam service (in the game process, or in a separate helper for Spacewar) |
| `ui.c` | lobby logic: asset overrides, macros, events, keyboard, player cards |
| `fastc.c` | reader for the engine's binary "fast container" trees |
| `shadow.c`, `netsim.c` | offline test harness (a puppet of your own car over a simulated network); off unless enabled in the developer ini |
| `uisrc/*.py`, `tools/fc.py`, `tools/apf.py` | offline generator for the UI screens from the game's `Assets.apf` |

## 2. Race setup

The game describes a race in its settings object (`*(base+0xd6aa60)`, the "game config"). `race.c` reads it into a plain struct (`RaceDesc`, `src/race.h`) and writes it back:

- game path and type (tables at `0xcbcfc0` and `0xcbd040`; config +0x08 and +0x0c), track and setting (+0x18 and +0x38, `std::string`), mirror (+0x58), laps (+0xc0), grid mode (+0x14);
- vehicles: `std::vector` at +0x4e8, 0x250 bytes each: vehicle, driver, skin, paint, decal colour and decal (strings at +0x00 to +0xa0), name (+0xd8), input (+0x11c: 0 human, 1 AI), viewport (+0x124, -1 for none), AI level, power-up deck (+0x1a0) and more;
- local players: `std::vector` at +0x2a8, 0x100 bytes each (pad, vehicle, driver, skin, paint, decal, plus a JSON container at +0xe0 used by split-screen car select);
- shared power-up deck at +0x4c8.

The lobby leaves the game's own flow in charge. Choose Track opens the Quick Event track grid; Start Race fires the normal `OnStartActionGame` event. When the game enters Action mode (FrontEnd/Action game-mode vtable slots 2/3/4), the host converts the recipe the game built into an `OnlineRace`: the same `RaceDesc` plus an owner (0 = host, 1..7 = client) and a human flag for each car slot. The host sends it to every ready client. Each PC then applies a local variant: its own car becomes the human car with the local gamepad and viewport; every other car becomes an AI-input car, which the mod drives as a puppet. Grid order is fixed (grid mode 0), so car slot *i* is the same car on every PC.

## 3. In the race

- **Car identity.** `VuVehicleEntity`+0x190 points to the vehicle's own config row; its index in the vector is the car slot.
- **Inputs.** `VuVehicleInputHumanGame` and `VuVehicleInputAiGame` slot 1 (`update(dt)`) are hooked. For a puppet, the AI update is skipped and `repl.c` steers the PhysX actor (`VuVehicleEntity`+0x1b8; `PxRigidDynamic` pose and velocity through vtable slots 18 to 41) toward the latest snapshot. It blends position, velocity and rotation a little every frame, teleports when the error is above 8 m, and extrapolates for at most 0.35 s. The control block (+0x480, 24 bytes) is copied too, so wheels, steering and sounds look right.
- **Snapshots.** 30 Hz, unreliable, sent every tick from the owner (pose, linear and angular velocity, controls, owner clock). They no longer depend on the human input update: when a car finishes, the game replaces its input with a `PostGameDrive` controller (`0x14fe50`), and snapshots must keep flowing after that.
- **Power-ups.** `VuVehicleEffectController` apply (`0x1423f0`, inline hook) is the single point where an effect lands on a car. An effect that starts on the PC owning the target car is sent reliably (effect name, target and source slot, level, power-up name). On other PCs it is replayed, and the local copy that the puppet's own simulation would produce is suppressed.
- **Leaving and restarting.** A client that leaves or disconnects causes the host to give its cars to the AI and send `RC_OWNER`. The results-screen Retry is host-only and sends `RC_RESTART`, which carries a new race id; Quit to menu sends `RC_QUIT`. Lifecycle UI actions (`VuRestartActionGameUIAction`, `VuStartFrontEndUIAction`, `VuShowTipUIAction`) are hooked at vtable slot 17 (exec). Tips are skipped online because a tip popup pauses the countdown.

## 4. Start and finish (host as referee)

The game-flow FSM reads named conditions set through `setcond(fsm, name, bool)` at `0x42dea0` (inline hook). The mod watches these conditions:

| Condition | Who sets it | What the mod does |
|---|---|---|
| `SkipIntro` / `ForceIntro` | race load | forces the intro on, so every PC has the same hold point |
| `CinematicActive` | every tick | holds it at 1 after the intro until the host's release time. The per-tick work runs here too: commands, snapshots, results |
| `GameFlowStartGame` | `VuGameFlowStartGameEntity` → event → `0x17f250` | holds it at 0 until the common GO moment |
| `GameFlowEndGame` | `VuRaceGame` vtable `0xa7bd80` slot 15 (`0x1847a0`), every tick: true when every local human car has finished | holds it at 0 until the host's final table arrives, then forces it to 1 |

**Common clock.** A client sends a time sample (its QPC time in µs) to the host every 120 ms for the first 8 samples, then once a second. The host answers at once with its own time. The client keeps the last 16 samples and uses the offset from the one with the lowest round trip (NTP style, assuming a symmetric path). The clock counts as ready after 4 samples.

**Release.** Once every racer has loaded and the intro has ended (or after 45 s), the host picks a release moment D = 200 ms + max RTT / 2 in the future and sends it as an absolute host-clock time (`M_GO.go_us`). Each client converts it to local time with its offset. The old relative delay is still in the message and is used if the clock is not ready.

**Countdown.** After release, `HUD_StartSequence` (PreGame HUD) counts 1-2-3-GO with a `VuMultiDelayEntity` (4 s of game time) and then raises `GameFlowStartGame`. The mod lets that through only at release + 4.2 s on every PC. A PC whose countdown ends early waits a few frames; the GO sound still plays locally on time.

**Finish.** Every PC reports the cars it owns when their finished flag (`+0x730`) changes from 0 to 1, together with their race time (`+0x748`, a double that the race tick increases by dt until the car finishes, `0x17a880`). The host collects the reports. When every human car has finished, or 30 s after the first human finished, the host broadcasts the final table: for each slot, finished or not and the time. Every PC writes the finishers' values into their vehicles (`+0x730 = 1`, `+0x748 = time`, `+0x758 = 0`) on every tick until the race ends, then lets `GameFlowEndGame` through. The rest happens the game's normal way:

- At game end, `0x179890` marks every car that has not finished as not finished (`+0x758 = 1`, with an estimated time); it skips cars whose `+0x730` is set.
- Placing comes from `VuRaceGame` slot 21 → `0x17c3b0`, which sorts the list at game+0x1c8 with the comparator in `0x173560`: unfinished (`+0x758`) last, ordered by `+0x75c`; then finishers by time; then running cars by progress (`+0x778`). It writes the place to `+0x760`.
- The results table (`Table_Results_Row`) sorts by place, score and time (`0x7b9d0`).

Before this change, every PC ended the race as soon as its own player finished and estimated the other humans' times from their puppets, which lag by the network delay. That is why two screens could show different results.

## 5. Protocol (version 8)

The handshake (`M_HELLO`) checks the protocol number, the game exe timestamp and the size of the race struct, so a mismatched build is refused with a clear message. ENet channel 0 is reliable; channel 1 is unreliable.

| Message | Direction | Content |
|---|---|---|
| HELLO / WELCOME / REJECT | C→H / H→C | protocol, exe timestamp, struct size, name, car, state / client id / reason |
| LOBBY | H→C | settings, track and setting, 8 slots {kind, connection, local player, pad}, players (name, car, ready, state, ping) |
| READY, CAR, STATE, CHAT | C→H (chat relayed) | lobby updates |
| START | H→C | `OnlineRace` (race id, owners, humans, `RaceDesc`) |
| LOADED / GO | C→H / H→C | intro finished / release time (host clock µs, plus a relative delay as fallback) |
| SNAP | both (host relays) | race id, n × {slot, owner clock, pose, velocities, controls} |
| EFFECT | both (host relays) | power-up effect on a car |
| RACECMD | both | RC_RESTART, RC_QUIT, RC_LEFT, RC_OWNER |
| TSYNC / TSYNCR | C→H / H→C | clock sample t0 / t0 + host receive time |
| FINISH / RESULT | C→H / H→C | slot and race time / final table |
| LOG | both | log lines, copied into the other side's log |

**Slots.** There are 8 lobby slots: Open, Closed, Bot or Player. A player slot holds (connection, local player index); the local index is always 0 for now, which leaves room for online split screen. A joining player takes the first Open slot. The host cycles empty slots Open → Bot → Closed. A player who joins during a race waits in the lobby; the host includes ready players when the next race starts.

## 6. Steam

- **Functions.** The Steamworks flat API is resolved with `GetProcAddress` from the game's own `steam_api64.dll`; no SDK headers are needed and no Valve files are shipped. Interfaces used: Matchmaking009, Friends017, User023, Utils010, NetworkingMessages002 and NetworkingUtils004.
- **Two networks.** The game's own AppID (1230800) runs inside the game process, using the game's own `SteamAPI_RunCallbacks`. Valve's Spacewar test app (480) runs in a separate helper process, `bbr2mod\steam\bbr2steam.exe`, because Steam ties one process to one AppID.
- **Lobby.** The host creates a Steam lobby (public type, so that it can be found by code; not listed). Lobby data: `bbr2=1`, `bbr2code`, `bbr2proto`, `bbr2exe`, `bbr2ver`, `bbr2name`, `bbr2listed=0`.
- **Lobby code.** 5 characters from `ABCDEFGHJKMNPQRSTUVWXYZ23456789`. The first letter carries the network: A–M is the game's AppID, N–Z is Spacewar.
- **Tunnel.** ENet connects to a local UDP socket. The service forwards each datagram as an unreliable `ISteamNetworkingMessages` message (channel 0) to the lobby member; reliability stays in ENet. Session requests are accepted only from lobby members. Steam Datagram Relay handles NAT.
- **Fallback.** Direct IP is always available as well: the host's ENet listens on 0.0.0.0:27420 even when a Steam lobby is open.

## 7. UI

- **Asset overrides.** Hooks on the load functions (vtable slot 3) of `VuProjectAsset` and `VuDBAsset` swap the data of selected assets with generated files from `bbr2mod\ui` (map in `ui/overrides.txt`).
  - Screens that the Premium build never opens are reused as carriers: `Screens_Simple/Extra` (Online menu), `Screens_Simple/Leaderboard` (host lobby), `Screens_Simple/Credits` (client lobby) and `Popups_Tutorial/Onboarding_NameA` (keyboard).
  - The main menu, `QuickEvent_Race` and the 1P HUD get small additions.
  - `ConstantDB` gets the screen stacks used to return to the lobby after a race.
- **Generator.** The files are made by `uisrc/build.py` from the game's own trees (`Assets.apf`, VUPF v8, LZ4, fast container codec `tools/fc.py`). Every text in them is English.
- **Text.** The mod fills text with `[[BBR_*]]` font macros through a hook on `VuFontMacros::get` (`0x4752a0`), re-evaluated on every draw.
- **Events.** Screens send `VuGenericEventEntity` broadcasts named `BBR_*`, caught by a hook on the hashed broadcast (`0x42a6b0`, FNV-1a 32). The mod drives the screens by broadcasting `OnPushScreen`, `OnPopScreen`, `OnSetScreen` and `OnStartActionGame`, with `VuParams` built through `0x4143d0`.
- **Typing.** Text entry uses the game's virtual keyboard entity, with physical typing through a `WndProc` subclass.
- **Player cards.** Each of the 8 cards is a `VuViewportContainerEntity` (viewport 0..7). For remote players the mod adds temporary local player entries with negative pads while the lobby screen is shown. The game's own split-screen car and driver image entities then draw every player's choice. The entries are removed before any car select or race.

## 8. Building

Requirements: Linux, or WSL, with Python 3.10+. The C toolchain is `zig cc` 0.13 (from the `ziglang` wheel) targeting `x86_64-windows-gnu`. The analysis tools use `capstone` and `lz4`. Put the three wheels in `toolchain/wheels` (they are not included in the source package):

```
pip download ziglang==0.13.0 capstone==5.0.9 lz4==4.4.5 --only-binary=:all: \
    --platform manylinux2014_x86_64 --python-version 3.10 -d toolchain/wheels
bash build.sh           # build/version.dll and build/bbr2steam.exe
bash build.sh install   # also copies them into the game folder (game closed)
python3 uisrc/build.py  # regenerates bbr2mod/ui from the game's Assets.apf
bash tools/package.sh   # release and source zips in dist/
```

The source folder is expected at `<game folder>/bbr2mod`. The build scripts find `Game_x64.exe` and `Assets.apf` one level up.

`tools/bbre.py` is a small static-analysis helper used to find the addresses above: vtables by class, disassembly, cross-references and string search.

## 9. Limits and possible next steps

- **Scope.** Race mode only, one player per PC, up to 8 cars.
- **Accuracy.** Each PC still runs its own physics for puppets, so contact between cars is not identical on every screen; the owner's car is always correct on its owner's PC.
- **Unfinished cars.** At the 30 s timeout, cars that have not finished are ranked behind the finishers by the game on each PC. Their order among themselves can differ slightly between PCs.
- **Late join.** Late joiners wait for the next race. Taking over a bot mid-race would need the race-progress fields (lap, checkpoint, list order) to be transferred.
- **Ready-made extension points.** A public lobby list (`bbr2listed` flag already in the lobby data); online split screen (the slot model already has a local player index); a non-Steam route for other stores.
- **Integration help.** An official implementation could make all of this simpler with a few engine features: a "remote" vehicle input type, a race-results API, and a hook for the end-of-race condition.
