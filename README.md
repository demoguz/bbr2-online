# BBR2 Online

**Online multiplayer for Beach Buggy Racing 2 on PC.** An unofficial mod that adds an **Online** button to the game's main menu: race your friends over the internet, with up to 8 cars per race (players plus bots).

> This is a fan-made mod. It is not made, supported or endorsed by Vector Unit. Beach Buggy Racing is a trademark of Vector Unit.

## Features

- **Lobby inside the game's own menus:** 8 player cards. The host sets empty slots to Open, Bot or Closed.
- **Track choice:** the host picks a track, or Random for a new track every race.
- **Same Car for All:** everyone, bots included, races the host's car.
- **Chat** in the lobby.
- **Easy joining:** a 5-letter lobby code, a Steam invite, or the host's IP address (LAN, ZeroTier, port forwarding).
- **Steam relay,** so no ports need to be opened.
- **Synchronised start,** shared power-ups, and the host decides the final results, so every screen shows the same table.
- **AI takes over** a player's car if they leave mid-race.
- **Late joiners** wait in the lobby and race in the next round.

## Download and install

1. Download `BBR2Online_<version>.zip` from [Releases](../../releases).
2. In Steam, right-click Beach Buggy Racing 2 > **Manage** > **Browse local files**.
3. Extract the zip into that folder (next to `Game_x64.exe`).
4. Start the game and select **Online** in the main menu.

**Requirements**
- The Steam version of the game for Windows.
- Everyone in a lobby uses the same mod version.

**Uninstall:** delete `version.dll` and the `bbr2mod` folder. The mod does not change any game files on disk.

The full player guide, including network options, troubleshooting and known limits, is in [docs/release/README.txt](docs/release/README.txt). A Turkish install note is in [KURULUM.txt](docs/release/KURULUM.txt).

## How it works

The mod is a proxy `version.dll`. It hooks the game at runtime, uses ENet for networking and the Steamworks API from the game's own `steam_api64.dll` for lobbies. Each PC simulates the cars it owns and sends their state to the others; the host acts as referee for the start and the results.

[docs/TECHNICAL.md](docs/TECHNICAL.md) describes the hooks, the race sync, the protocol, the Steam integration, the UI and how to build the mod.

## Repository layout

| Folder | Contents |
|---|---|
| `src/` | the mod (C): `version.dll` and the Steam helper `bbr2steam.exe` |
| `uisrc/` | generator for the lobby screens (Python; reads the game's `Assets.apf`) |
| `tools/` | asset readers, static-analysis helper, packaging script |
| `tests/` | small native tests |
| `third_party/enet/` | ENet 1.3.18 (MIT) |
| `docs/` | technical overview, release texts, developer settings |

Game files, the generated `ui/` folder and the build toolchain are not in the repository.

## License

[MIT](LICENSE). Third-party notices: [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt).
