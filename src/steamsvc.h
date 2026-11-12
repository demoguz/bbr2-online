// Steam tunnel (service): Steam lobby (code, invites) + carries ENet's UDP packets over ISteamNetworkingMessages.
// The same code runs in two places:
//   - inside the game process (inproc = 1): the game started Steam with its own AppID, the game calls RunCallbacks;
//   - inside bbr2steam.exe (Spacewar, AppID 480): the service starts Steam and calls RunCallbacks.
// It talks to the game side (steamlink.c) over 127.0.0.1 UDP with one-line text messages:
//   game -> service: host <seq> <enet_port> <maxp> <proto> <exe> <version> <name>   | join <seq> <proto> code <CODE> | join <seq> <proto> id <lobby>
//                   invite | leave | quit
//   service -> game: hello <net> | ready <net> <appid> <steamid> <name> | fail <net> <reason> | log <net> <text>
//                   hosted <net> <seq> <code> <lobby> | hostfail <net> <seq> <reason>
//                   joined <net> <seq> <local_port> <code|-> <host> | joinfail <net> <seq> <reason>
//                   invited <net> <lobby> [friend] | note <net> <text>
#pragma once
#include <windows.h>
#include <stdint.h>
enum { SVC_NET_GAME = 0, SVC_NET_SPACEWAR = 1 };
typedef struct {
    int net;                  // SVC_NET_*
    int inproc;               // 1: game process
    HMODULE steam_dll;        // steam_api64.dll loaded in the helper; 0 in process (the game's is used)
    unsigned short ctl_port;  // control socket on the game side (127.0.0.1)
    HANDLE parent;            // helper: exit when the game process closes
} SvcConfig;
DWORD svc_run(const SvcConfig* c);   // does not return in process; returns on exit in the helper
