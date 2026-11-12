BBR2 Online {VERSION}
Online multiplayer for Beach Buggy Racing 2 (unofficial mod)
====================================================================

BBR2 Online adds an "Online" button to the main menu of Beach Buggy Racing 2.
Up to 8 cars race together: human players on different PCs, plus bots to fill
the grid. The lobby is built into the game's own menus.

This is an unofficial, fan-made mod. It is not made, supported or endorsed by
Vector Unit. Beach Buggy Racing is a trademark of Vector Unit.


REQUIREMENTS
------------
- Beach Buggy Racing 2 on Steam for Windows (64-bit). The mod checks the game
  build when it starts (Game_x64.exe, PE timestamp 0x67C624B1) and stays off on
  any other build.
- Every player in a lobby must use the same version of this mod.
- Steam running, for lobby codes and invites. Joining by IP address works
  without Steam.


INSTALL
-------
1. Close the game.
2. Open the game folder: in Steam, right-click Beach Buggy Racing 2 >
   Manage > Browse local files. It is the folder that contains Game_x64.exe.
3. Extract everything from this zip into that folder:
     version.dll
     bbr2mod\  (with the ui and steam folders inside)
   Allow overwriting if you are updating from an older version.
4. Start the game. The main menu now has an "Online" button.

UNINSTALL
---------
Close the game and delete version.dll and the bbr2mod folder from the game
folder. The mod does not edit any game files on disk; it only changes things
in memory while the game is running.


PLAYING
-------
Online > Name          Set the name the other players see.
Online > Create Lobby  Host a lobby. The lobby shows a lobby code such as
                       K7Q2M. Tell it to your friends, or use Invite Friends to
                       open the Steam invite window.
Online > Join Lobby    Type a lobby code (letters are not case sensitive) or
                       the host's IP address. You can also accept a Steam
                       invite; if the game is closed, Steam starts it. The lobby
                       should open by itself at the main menu; if it does
                       not, select Online.

In the lobby
- 8 player cards. The host selects an empty card to switch it between
  Open (a player can join), Bot (AI racer) and Closed.
- Choose Car & Driver: pick your car. Picking one makes you Ready.
- Choose Track (host): pick the track, or Random for a new random track every
  race.
- Same Car for All (host): everyone, bots included, races the host's car;
  drivers and paint stay your own.
- Chat: type a message for everyone in the lobby.
- Start Race (host): starts the race. If someone is not ready, the first press
  warns; the second press starts without the players who are not ready.

During a race
- The start is synchronised: everyone's countdown ends at the same moment.
- If a player leaves, an AI driver takes over their car.
- The host decides the results. When you finish, the game waits for the other
  players ("Waiting for the other players to finish..."), for at most 30
  seconds after the first player finished. Everyone then sees the same results
  table. Players who have not finished by then are placed behind the
  finishers.
- Only the host can restart the race or end it for everyone.
- A player who joins while a race is running waits in the lobby and races in
  the next one.


NETWORK OPTIONS
---------------
The setting is in bbr2mod\online.ini (created on first start):

  steam=game      Lobbies use Beach Buggy Racing 2's own Steam network
                  (default). Every player must own the game on Steam.
  steam=spacewar  Lobbies use Spacewar (AppID 480), Valve's free test app on
                  Steam. Steam must be running on every PC. Your friends may
                  see you as "playing Spacewar". There is no Steam invite
                  window on this network; share the lobby code instead.
  steam=off       IP only.

The setting only chooses the network for lobbies you create. A lobby code
always works: codes starting with A-M are on the game's network, N-Z on
Spacewar. Steam connections go through Steam's relay network, so no ports
need to be opened.

Joining by IP works on a LAN, over a virtual LAN such as ZeroTier, or with a
forwarded port. The host listens on UDP port 27420 (the "port=" line in
online.ini changes it).


TROUBLESHOOTING
---------------
- No Online button: check that version.dll is next to Game_x64.exe. Then open
  bbr2mod\netlog.txt; it says if this game build is not supported.
- "Different mod versions": everyone must install the same release.
- A lobby code is not found: the host must still be in the lobby, with Steam
  running.
- Joining by IP fails: check the host's firewall, the port forwarding, or use
  a lobby code instead.
- Crashes or other problems: send bbr2mod\netlog.txt and netlog_prev.txt
  (the previous session) to whoever you got the mod from. These logs contain
  player names, IP addresses and lobby codes, so look through them before
  posting them in public.


KNOWN LIMITS
------------
- Race mode only (Quick Event race tracks). No championships or battle modes.
- One player per PC (no online split screen yet).
- Each PC simulates the cars it owns (its own car; the host also runs the
  bots) and sends their positions to the others. Other cars are moved
  smoothly toward those positions, so a car can look a little different on
  each screen when the connection is bad. Power-ups are shared, but a rare
  hit can still look different on two screens.


PRIVACY
-------
The mod only talks to the players in your lobby, either directly (IP) or
through Steam. A Steam lobby lists its lobby code, the host's in-game name
and the mod version, so that friends can find it with the code. The mod
sends nothing anywhere else.


CREDITS
-------
Networking uses ENet (MIT license); see THIRD_PARTY_NOTICES.txt.
Steam features use the Steamworks API through the steam_api64.dll that comes
with the game; no Steam files are included in this package.
