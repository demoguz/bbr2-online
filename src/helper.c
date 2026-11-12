// bbr2steam.exe: Steam lobby and tunnel on Spacewar (AppID 480). Started by the game (steamlink.c), exits with the game.
// A separate process is needed: Steam treats one process as one AppID, and the game's process started with its own AppID.
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "steamsvc.h"
int main(int argc, char** argv) {
    SvcConfig c; memset(&c, 0, sizeof c); c.net = SVC_NET_SPACEWAR;
    unsigned long ppid = 0;
    for (int i = 1; i + 1 < argc; i++) {
        if (!strcmp(argv[i], "--ctl")) c.ctl_port = (unsigned short)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--parent")) ppid = strtoul(argv[++i], 0, 10);
    }
    if (!c.ctl_port) return 1;
    if (ppid) c.parent = OpenProcess(SYNCHRONIZE, FALSE, ppid);
    char dir[MAX_PATH]; GetModuleFileNameA(0, dir, MAX_PATH); char* s = strrchr(dir, '\\'); if (s) s[1] = 0;
    SetCurrentDirectoryA(dir);   // steam_appid.txt (480) is next to it
    // if the game was started from Steam, the environment holds the game's AppID and we inherit it: overwrite it
    SetEnvironmentVariableA("SteamAppId", "480"); SetEnvironmentVariableA("SteamGameId", "480"); SetEnvironmentVariableA("SteamOverlayGameId", "480");
    char p[MAX_PATH + 64], full[MAX_PATH + 64]; HMODULE m = 0;
    snprintf(p, sizeof p, "%ssteam_api64.dll", dir);
    if (GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES) m = LoadLibraryExA(p, 0, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!m) {   // Steam install: the game's own steam_api64.dll (bbr2mod\steam\..\..)
        snprintf(p, sizeof p, "%s..\\..\\steam_api64.dll", dir);
        if (GetFullPathNameA(p, sizeof full, full, 0)) m = LoadLibraryExA(full, 0, LOAD_WITH_ALTERED_SEARCH_PATH);
    }
    c.steam_dll = m;   // on 0 the service reports the reason
    return (int)svc_run(&c);
}
