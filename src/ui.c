// Online in the game's own UI: replaces screen content at load, fills the [[BBR_*]] macros,
// handles "OnGenericEvent:BBR_*" events from the UI, sends events/screen commands to the UI.
// All player-facing text is in English.
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "core.h"
#include "game.h"
#include "session.h"
#include "steamlink.h"
#include "online.h"
typedef bool (MSABI *fn_load)(void* self, void* reader);
typedef bool (MSABI *fn_macro)(void* self, const char* name, void* out);
typedef void (MSABI *fn_bcast)(void* mgr, uint32_t hash, void* params);
typedef void* (MSABI *fn_assign)(void* str, const char* p, size_t n);
typedef void (MSABI *fn_broadcast)(void* mgr, const char* ev, void* params);
typedef void (MSABI *fn_addparam)(void* params, int type, const void* data, int len);
typedef const char* (MSABI *fn_getstr)(void* db, const char* id);
typedef void (MSABI *fn_kbinit)(void* self);
typedef void* (MSABI *fn_uiexec)(void* self, void* ret, const void* params);
#define SCREEN_ONLINE_NAME "Screens_Simple/Extra"
static uint8_t* g_base; static fn_load o_load; static fn_macro o_macro; static fn_bcast o_bcast;
static fn_kbinit o_kbinit; static fn_uiexec o_start_ui, o_push_ui; static void** g_kbvt; static fn_load o_dbload; static int g_db_ok;
typedef void* (MSABI *fn_jget)(void* c, const char* k);
typedef void (MSABI *fn_jsetstr)(void* node, const void* s);
typedef void (MSABI *fn_resize)(void* vec, size_t n);
typedef void (MSABI *fn_plcopy)(void* dst, const void* src);
typedef struct { char buf[16]; uint64_t size, cap; } MsStr;   // MSVC std::string (short string)
static uint32_t fnv32(const char* s) { uint32_t h = 0x811c9dc5u; for (; *s; s++) h = (h ^ (uint8_t)*s) * 0x1000193u; return h; }
static const char* sstr(const void* s) {
    uint64_t size = *(const uint64_t*)((const char*)s + 0x10), cap = *(const uint64_t*)((const char*)s + 0x18);
    if (cap < 15 || size > cap || cap > 4096) return "";
    return cap > 15 ? *(const char* const*)s : (const char*)s;
}
static void sassign(void* str, const char* v) { ((fn_assign)(g_base + FN_STR_ASSIGN))(str, v, strlen(v)); }

// ---------- screen replacement ----------
typedef struct { char asset[96]; char file[96]; uint8_t* data; } Ovr;
static Ovr g_ovr[32]; static int g_novr;
static void load_overrides(void) {
    char p[MAX_PATH + 32]; snprintf(p, sizeof p, "%sui\\overrides.txt", mod_dir());
    FILE* f = fopen(p, "r"); if (!f) { LOG("ERROR: ui\\overrides.txt missing, in-game online screens disabled"); return; }
    char line[256];
    while (fgets(line, sizeof line, f) && g_novr < 32) {
        char* nl = strpbrk(line, "\r\n"); if (nl) *nl = 0;
        char* eq = strchr(line, '='); if (!eq || line[0] == '#') continue; *eq = 0;
        Ovr* o = &g_ovr[g_novr++]; strncpy(o->asset, line, 95); strncpy(o->file, eq + 1, 95);
    }
    fclose(f); LOG("ui: %d screen replacement entries", g_novr);
}
static uint8_t* read_file(const char* name) {
    char p[MAX_PATH + 128]; snprintf(p, sizeof p, "%sui\\%s", mod_dir(), name);
    FILE* f = fopen(p, "rb"); if (!f) return 0;
    fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f);
    uint8_t* d = (uint8_t*)VirtualAlloc(0, n + 64, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);   // never freed
    if (d && fread(d, 1, n, f) != (size_t)n) d = 0;
    fclose(f); return d;
}
static bool MSABI hk_load(void* self, void* reader) {
    bool r = o_load(self, reader);
    const char* name = sstr((uint8_t*)self + ASSET_NAME);
    for (int i = 0; i < g_novr; i++) if (!strcmp(g_ovr[i].asset, name)) {
        if (!g_ovr[i].data) g_ovr[i].data = read_file(g_ovr[i].file);
        if (g_ovr[i].data && *(uint32_t*)g_ovr[i].data == 6) { *(void**)((uint8_t*)self + PROJ_ROOT) = g_ovr[i].data; LOG("ui: '%s' replaced with our version", name); }
        else LOG("ERROR: UI file unreadable or corrupt: %s", g_ovr[i].file);
        break;
    }
    return r;
}

typedef struct { const uint8_t* data; int32_t size, pos; } Reader;
static bool MSABI hk_dbload(void* self, void* reader) {
    const char* name = sstr((uint8_t*)self + ASSET_NAME);
    for (int i = 0; i < g_novr; i++) if (!strcmp(g_ovr[i].asset, name)) {
        if (!g_ovr[i].data) g_ovr[i].data = read_file(g_ovr[i].file);
        uint8_t* d = g_ovr[i].data;
        if (!d || *(uint32_t*)d != 1) { LOG("ERROR: database file unreadable or corrupt: %s", g_ovr[i].file); break; }
        Reader fake = { d, *(int32_t*)(d + 4), 0 }; Reader* real = (Reader*)reader;
        bool r = o_dbload(self, &fake); real->pos = real->size;
        if (r && !strcmp(name, "ConstantDB")) g_db_ok = 1;
        LOG("ui: database '%s' loaded with our version (%s)", name, r ? "ok" : "ERROR"); return r;
    }
    return o_dbload(self, reader);
}

// ---------- events / screen commands to the game ----------
typedef struct { uint8_t buf[0x100]; int32_t size; int32_t pad[3]; } VuParams;
static void vp_add(VuParams* p, int type, const void* d, int len) { ((fn_addparam)(g_base + FN_ADDPARAM))(p, type, d, len); }
static void bcast(const char* ev, VuParams* p) {
    void* mgr = *(void**)(g_base + G_EVENTMGR); if (!mgr) return;
    VuParams z; if (!p) { memset(&z, 0, sizeof z); p = &z; }
    ((fn_broadcast)(g_base + FN_BROADCAST))(mgr, ev, p);
}
void ui_send(const char* ev) { char full[96]; snprintf(full, sizeof full, "OnGenericEvent:%s", ev); bcast(full, 0); }
static void ui_pop(void) { VuParams p; memset(&p, 0, sizeof p); uint8_t f = 0; vp_add(&p, 3, &f, 1); bcast("OnPopScreen", &p); }
static void ui_start_race(void) { VuParams p; memset(&p, 0, sizeof p); vp_add(&p, 4, "Premium", 8); bcast("OnStartActionGame", &p); }

// ---------- settings (online.ini) ----------
static char g_name[32], g_ip[64]; static int g_port = NET_PORT_DEFAULT;
static void ini_save(void);
static void ini_load(void) {
    char p[MAX_PATH + 32]; snprintf(p, sizeof p, "%sonline.ini", mod_dir());
    FILE* f = fopen(p, "r"); if (!f) { ini_save(); return; }
    char line[256]; int has_steam = 0;
    while (fgets(line, sizeof line, f)) {
        char* nl = strpbrk(line, "\r\n"); if (nl) *nl = 0;
        if (!strncmp(line, "name=", 5)) { strncpy(g_name, line + 5, 31); g_name[31] = 0; }
        else if (!strncmp(line, "ip=", 3)) { strncpy(g_ip, line + 3, 63); g_ip[63] = 0; }
        else if (!strncmp(line, "port=", 5)) { int v = atoi(line + 5); if (v > 0 && v < 65536) g_port = v; }
        else if (!strncmp(line, "steam=", 6)) has_steam = 1;
    }
    fclose(f);
    if (g_name[0]) online_set_name(g_name);
    if (!has_steam) ini_save();   // old file: add the steam= line and its description
}
static void ini_save(void) {
    char p[MAX_PATH + 32]; snprintf(p, sizeof p, "%sonline.ini", mod_dir());
    char sm[32]; sl_read_mode(sm, sizeof sm);   // steam= is read from the file (it may have been edited while the game runs)
    FILE* f = fopen(p, "w"); if (!f) return;
    fprintf(f, "name=%s\nip=%s\nport=%d\n"
        "# steam: which Steam network your lobbies use (lobby codes and Steam invites). Everyone in a lobby is on the host's network.\n"
        "#   game     - Beach Buggy Racing 2's own Steam network: only players who own the game on Steam (default).\n"
        "#   spacewar - Steam's free test app Spacewar (AppID 480): use it when a player has the game from another store.\n"
        "#              Steam must be running on every PC; friends may see you as playing Spacewar.\n"
        "#   off      - IP only (LAN, ZeroTier, port forwarding).\n"
        "# Joining with a lobby code works on both networks; this setting only picks the network for lobbies you create.\n"
        "steam=%s\n", g_name, g_ip, g_port, sm);
    fclose(f);
}

// ---------- localised names ----------
static const char* loc(const char* prefix, const char* name, char* out, int cap) {
    out[0] = 0; if (!name || !name[0]) return out;
    char id[128]; snprintf(id, sizeof id, "%s%s", prefix, name);
    void* db = *(void**)(g_base + G_STRINGDB); const char* r = 0;
    if (db) r = ((fn_getstr)(g_base + FN_GETSTRING))(db, id);
    if (!r || !r[0] || !strcmp(r, id)) r = name;
    snprintf(out, cap, "%s", r); return out;
}

// ---------- player list helpers ----------
static void* jget(void* c, const char* k) { return ((fn_jget)(g_base + FN_JSON_GET))(c, k); }
static void jset(void* node, const char* v) { MsStr s; memset(&s, 0, sizeof s); snprintf(s.buf, 16, "%s", v); s.size = strlen(s.buf); s.cap = 15; ((fn_jsetstr)(g_base + FN_JSON_SETSTR))(node, &s); }
static const char* jstr(void* node) { return node && *(int*)node == 4 && *(void**)((uint8_t*)node + 8) ? sstr(*(void**)((uint8_t*)node + 8)) : ""; }
static uint8_t* players(int* n) {
    uint8_t* cfg = *(uint8_t**)(g_base + G_GAMECFG); if (!cfg) { *n = 0; return 0; }
    uint8_t* pb = *(uint8_t**)(cfg + CFG_PLAYERS); uint8_t* pe = *(uint8_t**)(cfg + CFG_PLAYERS + 8);
    *n = pb ? (int)((pe - pb) / 0x100) : 0; return pb;
}
static void* player_state(uint8_t* pl) { return jget(jget(pl + PL_JSON, "SplitScreen"), "State"); }
// ---------- card lobby ----------
// The cards are VuViewportContainerEntity with Viewport 100+i; the car/driver images inside read the player list by the container's gamepad.
// Remote players are added to the list with a NEGATIVE gamepad: code that indexes the game's gamepad array (JoinLogic etc.)
// skips them with "js". The fake players stay in the list only while the lobby screen is open.
#define FAKE_PAD(s) (-2 - (s))
#define NO_PAD (-100)
typedef void (MSABI *fn_vpinit)(void* self);
static fn_vpinit o_vpinit;
static void* g_card[MAXP];
static int g_fakes, g_lobby_frozen, g_kstate[MAXP], g_tstate, g_qrandom, g_qpick_random; static uint64_t g_qrandom_off_ms;
// The cards use Viewport 0..7 (100+ overran the game's per-viewport array). Newly created containers are kept in a ring;
// when the lobby screen opens (OnProjectInitialized) the most recent ones are adopted as cards.
static void* g_vpnew[32]; static int g_vpnew_vp[32]; static unsigned g_vpnew_n;
static void MSABI hk_vpinit(void* self) {
    o_vpinit(self);
    int vp = *(int*)((uint8_t*)self + VPC_VIEWPORT);
    if (vp >= 0 && vp < MAXP) { g_vpnew[g_vpnew_n & 31] = self; g_vpnew_vp[g_vpnew_n & 31] = vp; g_vpnew_n++; }
}
static void vp_clear(void) { g_vpnew_n = 0; }
static void cards_adopt(void) {
    memset(g_card, 0, sizeof g_card);
    unsigned first = g_vpnew_n > 32 ? g_vpnew_n - 32 : 0; int got = 0;
    for (unsigned i = first; i < g_vpnew_n; i++) { int vp = g_vpnew_vp[i & 31]; if (!g_card[vp]) got++; g_card[vp] = g_vpnew[i & 31]; }
    for (int i = 0; i < MAXP; i++) if (g_card[i]) *(int*)((uint8_t*)g_card[i] + VPC_PAD) = NO_PAD;
    LOG("ui: %d card containers found", got); vp_clear();
}
static void fakes_remove(const char* why) {
    if (!g_fakes) return;
    uint8_t* cfg = *(uint8_t**)(g_base + G_GAMECFG); int n; players(&n);
    if (cfg && n > 1) ((fn_resize)(g_base + FN_PLAYERS_RESIZE))(cfg + CFG_PLAYERS, 1);
    LOG("ui: %d lobby players removed from the list (%s)", g_fakes, why); g_fakes = 0;
}
static void set_str(uint8_t* s, const char* v) { if (strcmp(sstr(s), v)) sassign(s, v); }
static void lobby_sync(const Lobby* L) {
    uint8_t* cfg = *(uint8_t**)(g_base + G_GAMECFG); if (!cfg) return;
    int n; uint8_t* pb = players(&n); if (!pb || n < 1) return;
    int want = 0; for (int s = 0; s < MAXP; s++) if (L->slot[s].kind == SL_PLAYER && L->slot[s].conn != L->my_id) want++;
    if (n != 1 + want) {
        ((fn_resize)(g_base + FN_PLAYERS_RESIZE))(cfg + CFG_PLAYERS, 1);
        if (want) ((fn_resize)(g_base + FN_PLAYERS_RESIZE))(cfg + CFG_PLAYERS, 1 + want);
        pb = players(&n); if (!pb || n != 1 + want) { g_fakes = n > 1 ? n - 1 : 0; return; }
        for (int k = 1; k < n; k++) { ((fn_plcopy)(g_base + FN_PLAYER_COPY))(pb + 0x100 * k, cfg + CFG_PLAYER_TMPL); *(int*)(pb + 0x100 * k) = NO_PAD; }
        if (want != g_fakes) LOG("ui: lobby players %d -> %d", g_fakes, want);
        g_fakes = want;
    }
    int k = 0;
    for (int s = 0; s < MAXP; s++) {
        const LobbySlot* sl = &L->slot[s]; int pad = NO_PAD;
        if (sl->kind == SL_PLAYER && sl->conn < MAXP) {
            if (sl->conn == L->my_id) pad = *(int*)pb;
            else if (k < want) {
                uint8_t* f = pb + 0x100 * (1 + k++); const CarSel* c = &L->p[sl->conn].car;
                *(int*)f = FAKE_PAD(s);
                set_str(f + PL_VEHICLE, c->vehicle); set_str(f + PL_DRIVER, c->driver); set_str(f + PL_SKIN, c->skin);
                set_str(f + PL_PAINT, c->paint); set_str(f + PL_DECALCOLOR, c->decalcolor); set_str(f + PL_DECAL, c->decal);
                if (c->vehicle[0]) pad = FAKE_PAD(s);
            }
        }
        if (g_card[s]) *(int*)((uint8_t*)g_card[s] + VPC_PAD) = pad;
        int ks = sl->kind == SL_PLAYER ? (pad != NO_PAD ? 1 : 2) : sl->kind == SL_OPEN ? 3 : sl->kind == SL_BOT ? 4 : 5;
        if (ks != g_kstate[s]) {
            static const char* nm[] = { "", "Player", "Choosing", "Open", "Bot", "Closed" };
            char ev[32]; snprintf(ev, sizeof ev, "BBR_C%d_%s", s + 1, nm[ks]); g_kstate[s] = ks; ui_send(ev);
        }
    }
}
static char g_tracks[160][48]; static int g_ntracks;
static void load_tracks(void) {
    char p[MAX_PATH + 32]; snprintf(p, sizeof p, "%sui\\tracks.txt", mod_dir());
    FILE* f = fopen(p, "r"); if (!f) return; char line[64];
    while (g_ntracks < 160 && fgets(line, sizeof line, f)) { char* nl = strpbrk(line, "\r\n"); if (nl) *nl = 0; if (strchr(line, '_')) snprintf(g_tracks[g_ntracks++], 48, "%s", line); }
    fclose(f); LOG("ui: random track list %d", g_ntracks);
}
static int tbl_index(uintptr_t tbl, int n, const char* want) {
    const char* const* t = (const char* const*)(g_base + tbl);
    for (int i = 0; i < n; i++) if (t[i] && !strcmp(t[i], want)) return i;
    return -1;
}
static int random_track(void) {   // random track + setting into the settings; Quick Event / Race mode
    uint8_t* cfg = *(uint8_t**)(g_base + G_GAMECFG); if (!cfg || !g_ntracks) return 0;
    static unsigned seed; if (!seed) seed = (unsigned)GetTickCount64() * 2654435761u;
    seed = seed * 1103515245u + 12345u; const char* pick = g_tracks[(seed >> 8) % g_ntracks];
    char tr[48]; snprintf(tr, sizeof tr, "%s", pick); char* us = strchr(tr, '_'); if (!us) return 0; *us = 0;
    sassign(cfg + CFG_TRACK, tr); sassign(cfg + CFG_SETTING, us + 1); cfg[CFG_MIRROR] = 0;
    int gt = tbl_index(TBL_GAMETYPE, TBL_GAMETYPE_N, "Race"), gp = tbl_index(TBL_GAMEPATH, TBL_GAMEPATH_N, "QuickEvent");
    if (gt >= 0) *(int*)(cfg + CFG_GAMETYPE) = gt;
    if (gp >= 0) *(int*)(cfg + CFG_GAMEPATH) = gp;
    if (*(int*)(cfg + CFG_LAPS) <= 0) *(int*)(cfg + CFG_LAPS) = 3;
    LOG("ui: random track %s / %s (laps %d, path %d, type %d)", tr, us + 1, *(int*)(cfg + CFG_LAPS), gp, gt);
    return 1;
}
void ui_fe_exit(void) { fakes_remove("front end exit"); memset(g_card, 0, sizeof g_card); g_lobby_frozen = 1; vp_clear(); }

// ---------- state ----------
enum { SC_NONE, SC_ONLINE, SC_LOBBY };
static int g_screen, g_nav_sent, g_closed_sent;
static int g_pick;                 // 1: went from the lobby to car select, 2: went to track select
static int g_track_ok;
static int g_kbmode; static void* g_kb; static void* g_kb_filled; static volatile int g_kb_open;
static volatile LONG g_kq_w; static LONG g_kq_r; static wchar_t g_kq[64];
static int g_do_start; static uint64_t g_start_warn_ms, g_nav_block_ms;
static char g_toast_txt[160]; static uint64_t g_toast_ms;
static void toast(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); vsnprintf(g_toast_txt, sizeof g_toast_txt, fmt, ap); va_end(ap); g_toast_ms = now_ms();
    online_toast("%s", g_toast_txt);
}
static Lobby g_L; static uint64_t g_L_ms;
static const Lobby* lobby(void) { uint64_t t = now_ms(); if (t - g_L_ms > 30 || !g_L_ms) { net_lobby_copy(&g_L); g_L_ms = t; } return &g_L; }
static int humans(const Lobby* L) { int n = 0; for (int i = 0; i < MAXP; i++) n += L->p[i].used ? 1 : 0; return n; }
void ui_race_ended(void) { LOG("ui: race ended in an online session"); }
static const char* lobby_stack(void) {
    if (!g_db_ok) return 0;
    const Lobby* L = lobby(); g_L_ms = 0; L = lobby();
    return L->cs == CS_HOSTING ? "BBR_HostLobby" : L->cs == CS_CONNECTED ? "BBR_ClientLobby" : 0;
}
static void set_initial_stack(const char* st) {
    void* g = *(void**)(g_base + G_SETTINGS); if (!g) return;
    void* node = ((fn_jget)(g_base + FN_JSON_GET))((uint8_t*)g + 8, "InitialScreenStack"); if (!node) return;
    MsStr s; memset(&s, 0, sizeof s); snprintf(s.buf, 16, "%s", st); s.size = strlen(s.buf); s.cap = 15;
    ((fn_jsetstr)(g_base + FN_JSON_SETSTR))(node, &s);
}
void ui_prepare_frontend(void) { const char* st = lobby_stack(); if (st) { set_initial_stack(st); LOG("ui: the front end will return to the lobby (%s)", st); } }
void* ui_frontend_wrap(void* self, void* ret, const void* params, void* (MSABI *orig)(void*, void*, const void*)) {
    const char* st = lobby_stack();
    if (!st) return orig(self, ret, params);
    uint8_t* sp = (uint8_t*)self + UIACT_SCREEN; char old[64]; snprintf(old, sizeof old, "%s", sstr(sp));
    sassign(sp, st); void* r = orig(self, ret, params); sassign(sp, old);
    LOG("ui: leaving the race to the lobby (%s instead of %s)", old[0] ? old : "-", st); return r;
}

// ---------- dynamic text ----------
static void player_row(const Lobby* L, int row, char* out, int cap) {
    out[0] = 0; int k = 0;
    for (int i = 0; i < MAXP; i++) {
        const PeerInfo* p = &L->p[i]; if (!p->used) continue;
        if (k++ != row) continue;
        char car[64], drv[64]; loc("Vehicle_", p->car.vehicle, car, sizeof car); loc("Driver_", p->car.driver, drv, sizeof drv);
        const char* st = i == 0 ? "Host" : p->state == PS_LOADING || p->state == PS_WAITING || p->state == PS_RACING ? "In race"
                       : p->ready ? "Ready" : "Not ready";
        char ping[24] = ""; if (i != L->my_id && p->ping > 0) snprintf(ping, sizeof ping, "   %d ms", p->ping);
        snprintf(out, cap, "%d.  %s%s   -   %s%s%s   -   %s%s", row + 1, p->name[0] ? p->name : "Player", i == L->my_id ? " (you)" : "",
                 car[0] ? car : "No car yet", drv[0] ? " / " : "", drv, st, ping);
        return;
    }
    int maxp = L->maxp ? L->maxp : MAXP;
    if (row < maxp) snprintf(out, cap, "%d.  Open slot", row + 1);
}
static void slot_text(const Lobby* L, int s, const char* what, char* v, int cap) {
    const LobbySlot* sl = &L->slot[s]; v[0] = 0;
    int name = !strcmp(what, "NAME"), state = !strcmp(what, "STATE"), car = !strcmp(what, "CAR");
    if (sl->kind == SL_PLAYER && sl->conn < MAXP) {
        const PeerInfo* p = &L->p[sl->conn];
        if (name) snprintf(v, cap, "%s%s", p->name[0] ? p->name : "Player", sl->conn == L->my_id ? " (you)" : "");
        else if (state) {
            const char* st = p->state == PS_LOADING || p->state == PS_WAITING || p->state == PS_RACING ? "IN RACE"
                           : sl->conn == 0 ? "HOST" : p->ready ? "READY" : "NOT READY";
            if (sl->conn != L->my_id && p->ping > 0) snprintf(v, cap, "%s   %d ms", st, p->ping); else snprintf(v, cap, "%s", st);
        } else if (car) {
            if (!p->car.vehicle[0]) { snprintf(v, cap, "Choosing a car..."); return; }
            char a[64], b[64]; loc("Vehicle_", p->car.vehicle, a, sizeof a); loc("Driver_", p->car.driver, b, sizeof b);
            snprintf(v, cap, "%s%s%s", a, b[0] ? " / " : "", b);
        }
    } else if (sl->kind == SL_BOT) { if (name) snprintf(v, cap, "Bot"); else if (state) snprintf(v, cap, "AI RACER"); }
    else if (sl->kind == SL_OPEN) { if (name) snprintf(v, cap, "Open"); else if (state) snprintf(v, cap, "Waiting for a player"); }
    else { if (name) snprintf(v, cap, "Closed"); }
}
static void make_macro(const char* name, char* v, int cap) {
    const Lobby* L = lobby(); v[0] = 0;
    if (!strcmp(name, "BBR_STATUS")) {
        if (L->cs == CS_IDLE) snprintf(v, cap, "%s", L->error[0] ? L->error : "Create a lobby and share its code with your friends, or join a friend's lobby with a code or an IP address.");
        else if (L->cs == CS_CONNECTING) snprintf(v, cap, "%s", L->status);
        else snprintf(v, cap, "%s", L->cs == CS_HOSTING ? "Hosting a lobby." : "Connected to a lobby.");
    }
    else if (!strcmp(name, "BBR_NAME")) snprintf(v, cap, "%s", g_name[0] ? g_name : "Player");
    else if (!strcmp(name, "BBR_KB_HEAD")) snprintf(v, cap, "%s", g_kbmode == 2 ? "Player Name" : g_kbmode == 3 ? "Chat" : "Join Lobby");
    else if (!strcmp(name, "BBR_KB_BODY")) snprintf(v, cap, "%s", g_kbmode == 2 ? "This name is shown to the other players." : g_kbmode == 3 ? "Type a message for everyone in the lobby." : "Enter the lobby code (for example K7Q2M) or the host's IP address (for example 10.147.17.25).");
    else if (!strncmp(name, "BBR_S", 5) && name[5] >= '1' && name[5] <= '8' && name[6] == '_') slot_text(L, name[5] - '1', name + 7, v, cap);
    else if (!strcmp(name, "BBR_TRACKNAME")) {
        if (L->track[0] && strcmp(L->track, "Random")) loc("Track_", L->track, v, cap);
        else snprintf(v, cap, "Random Track");
    }
    else if (!strcmp(name, "BBR_TRACKINFO")) {
        int h = humans(L), bots = L->bots; if (bots > 8 - h) bots = 8 - h; if (bots < 0) bots = 0;
        char laps[24] = ""; if (L->laps > 0) snprintf(laps, sizeof laps, "%d lap%s   |   ", L->laps, L->laps == 1 ? "" : "s");
        snprintf(v, cap, "%s%d player%s, %d bot%s%s", laps, h, h == 1 ? "" : "s", bots, bots == 1 ? "" : "s", L->equal ? ", same car" : "");
    }
    else if (!strcmp(name, "BBR_LOBBYHINT")) {
        char c[48] = ""; if (L->code[0]) snprintf(c, sizeof c, "Lobby code: %s (%s)", L->code, L->steam_net ? "Spacewar" : "Steam");
        if (L->role == ROLE_HOST) snprintf(v, cap, "%s%sSelect an empty slot to switch it between Open, Bot and Closed.", c, c[0] ? "   |   " : "");
        else {
            const PeerInfo* h = &L->p[0]; int racing = h->state == PS_LOADING || h->state == PS_WAITING || h->state == PS_RACING;
            if (racing) snprintf(v, cap, "%s%sThe host is in a race. Press Ready to join the next one.", c, c[0] ? "   |   " : "");
            else snprintf(v, cap, "%s", c);
        }
    }
    else if (!strncmp(name, "BBR_CHAT", 8) && name[8] >= '1' && name[8] <= '4' && !name[9]) snprintf(v, cap, "%s", L->chat[2 + name[8] - '1']);
    else if (!strncmp(name, "BBR_P", 5) && name[5] >= '1' && name[5] <= '8' && !name[6]) player_row(L, name[5] - '1', v, cap);
    else if (!strcmp(name, "BBR_LOBBYINFO")) {
        char tr[96]; loc("Track_", L->track, tr, sizeof tr);
        int h = humans(L), bots = L->bots; if (bots > 8 - h) bots = 8 - h; if (bots < 0) bots = 0;
        if (L->track[0]) snprintf(v, cap, "%s, %d lap%s   |   %d player%s + %d bot%s", tr, L->laps, L->laps == 1 ? "" : "s", h, h == 1 ? "" : "s", bots, bots == 1 ? "" : "s");
        else snprintf(v, cap, "%s   |   %d player%s + %d bot%s", L->role == ROLE_HOST ? "Choose a track" : "The host is choosing a track", h, h == 1 ? "" : "s", bots, bots == 1 ? "" : "s");
    }
    else if (!strcmp(name, "BBR_ADDR")) {
        if (L->role == ROLE_HOST) {
            char ips[4][96], ipt[112]; int n = net_local_ips(ips, 4);
            if (n > 0) { const char* s = ips[0]; int zt = !strncmp(s, "[ZeroTier] ", 11); if (zt) s += 11; char ip[48] = ""; sscanf(s, "%47s", ip);
                char ps[16] = ""; if (L->port != NET_PORT_DEFAULT) snprintf(ps, sizeof ps, ":%d", L->port);
                snprintf(ipt, sizeof ipt, "IP %s%s%s", ip, ps, zt ? " (ZeroTier)" : ""); }
            else snprintf(ipt, sizeof ipt, "Port %d", L->port);
            if (L->code[0]) snprintf(v, cap, "Lobby code: %s (%s)   |   %s", L->code, L->steam_net ? "Spacewar" : "Steam", ipt);
            else if (L->steam_note[0]) snprintf(v, cap, "%s   |   Friends join with %s", L->steam_note, ipt);
            else snprintf(v, cap, "Friends join with %s", ipt);
        } else snprintf(v, cap, "Connected to %s", L->joined_host);
    }
    else if (!strcmp(name, "BBR_SIZE")) snprintf(v, cap, "%d", L->maxp ? L->maxp : MAXP);
    else if (!strcmp(name, "BBR_BOTS")) snprintf(v, cap, "%d", L->bots);
    else if (!strcmp(name, "BBR_EQUAL")) snprintf(v, cap, "%s", L->equal ? "On" : "Off");
    else if (!strcmp(name, "BBR_FX")) snprintf(v, cap, "%s", L->effects ? "On" : "Off");
    else if (!strcmp(name, "BBR_READY")) snprintf(v, cap, "%s", L->p[L->my_id].ready ? "Yes" : "No");
    else if (!strcmp(name, "BBR_TOAST")) { if (g_toast_txt[0] && now_ms() - g_toast_ms < 5000) snprintf(v, cap, "%s", g_toast_txt); }
    else if (!strcmp(name, "BBR_HUD")) {
        OnlineStatus st; online_status(&st);
        if (st.toast[0]) snprintf(v, cap, "%s", st.toast); else if (st.in_race && st.note[0]) snprintf(v, cap, "%s", st.note);
    }
}
static bool MSABI hk_macro(void* self, const char* name, void* out) {
    if (name && name[0] == 'B' && !strncmp(name, "BBR_", 4)) {
        char v[700]; make_macro(name, v, sizeof v);
        sassign(out, v);
        return true;
    }
    return o_macro(self, name, out);
}

// ---------- keyboard popup ----------
static int kb_valid(void) { return g_kb && g_kb_open && *(void***)g_kb == g_kbvt; }
static void MSABI hk_kbinit(void* self) {
    static int calls; if (calls < 4) { LOG("ui: keyboard slot12 call %p (mode %d)", self, g_kbmode); calls++; }
    if (self != g_kb_filled && g_kbmode) {
        g_kb_filled = self; g_kb = self; g_kb_open = 1; g_kq_r = g_kq_w;
        *(int32_t*)((uint8_t*)self + KB_MAXLEN) = g_kbmode == 1 ? 26 : g_kbmode == 3 ? 60 : 16;
        sassign((uint8_t*)self + KB_TEXT, g_kbmode == 1 ? g_ip : g_kbmode == 3 ? "" : g_name);
    }
    o_kbinit(self);
}
static void kb_accept(void) {
    if (!g_kbmode) return;
    char t[96] = ""; if (kb_valid()) snprintf(t, sizeof t, "%s", sstr((uint8_t*)g_kb + KB_TEXT));
    char* a = t; while (*a == ' ') a++; char* e = a + strlen(a); while (e > a && e[-1] == ' ') *--e = 0;
    int mode = g_kbmode; g_kbmode = 0; g_kb_open = 0;
    if (mode == 1) {
        if (!a[0]) { toast("Enter the host's IP address to join"); return; }
        snprintf(g_ip, sizeof g_ip, "%s", a); ini_save();
        { int net; char norm[80]; if (sl_classify(a, &net, norm, sizeof norm)) { LOG("ui: join Steam lobby %s", a); net_cmd_join(a, 0); return; } }
        char host[64]; snprintf(host, sizeof host, "%s", a); int port = g_port;
        char* c = strchr(host, ':'); if (c) { *c = 0; int pv = atoi(c + 1); if (pv > 0 && pv < 65536) port = pv; }
        LOG("ui: join lobby %s:%d", host, port); net_cmd_join(host, port);
    } else if (mode == 3) {
        if (a[0]) net_cmd_chat(a);
    } else if (mode == 2) {
        if (!a[0]) return;
        snprintf(g_name, sizeof g_name, "%.16s", a); online_set_name(g_name); ini_save(); LOG("ui: name '%s'", g_name);
    }
}
static void kb_drain(void) {
    while (g_kq_r != g_kq_w) {
        wchar_t c = g_kq[g_kq_r & 63]; g_kq_r++;
        if (!kb_valid()) continue;
        uint8_t* kb = (uint8_t*)g_kb; char t[96]; snprintf(t, sizeof t, "%s", sstr(kb + KB_TEXT)); int n = (int)strlen(t);
        int maxl = *(int32_t*)(kb + KB_MAXLEN); if (maxl <= 0 || maxl > 60) maxl = 24;
        if (c == 8) { if (n) t[n - 1] = 0; }
        else if (c == 13) { ui_send("BBR_KbEnter"); continue; }
        else if (c >= 32 && c < 127) { if (n < maxl) { t[n] = (char)c; t[n + 1] = 0; } }
        else continue;
        sassign(kb + KB_TEXT, t); o_kbinit(g_kb);     // also updates the characters-remaining counter
    }
}
static HWND g_hwnd; static WNDPROC o_wndproc;
static int typing_key(WPARAM vk) {
    return (vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z') || vk == VK_SPACE || vk == VK_BACK || vk == VK_RETURN ||
           (vk >= VK_NUMPAD0 && vk <= VK_DIVIDE) || (vk >= 0xBA && vk <= 0xC0) || (vk >= 0xDB && vk <= 0xDF) || vk == 0xE2;
}
static LRESULT CALLBACK hk_wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (g_kb_open) {
        if (m == WM_CHAR) { LONG i = g_kq_w; if (i - g_kq_r < 64) { g_kq[i & 63] = (wchar_t)w; g_kq_w = i + 1; } return 0; }
        if ((m == WM_KEYDOWN || m == WM_KEYUP) && typing_key(w)) return 0;   // keep it away from the game's menu navigation
    }
    return CallWindowProcW(o_wndproc, h, m, w, l);
}
static BOOL CALLBACK enum_win(HWND h, LPARAM lp) {
    DWORD pid = 0; GetWindowThreadProcessId(h, &pid);
    if (pid != GetCurrentProcessId() || !IsWindowVisible(h) || GetWindow(h, GW_OWNER)) return TRUE;
    RECT r; GetClientRect(h, &r); if (r.right - r.left < 320) return TRUE;
    *(HWND*)lp = h; return FALSE;
}
static void hook_window(void) {
    static uint64_t last; uint64_t t = now_ms(); if (g_hwnd || t - last < 1000) return; last = t;
    HWND h = 0; EnumWindows(enum_win, (LPARAM)&h); if (!h) return;
    o_wndproc = (WNDPROC)SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)hk_wndproc);
    if (o_wndproc) { g_hwnd = h; char cls[64] = ""; GetClassNameA(h, cls, sizeof cls); LOG("ui: attached to the window (%s), keyboard typing on", cls); }
}

// ---------- events from the UI ----------
enum { E_CREATE = 1, E_JOINOPEN, E_NAMEOPEN, E_KBACCEPT, E_KBCLOSED, E_ONLINE_SHOWN, E_ONLINE_HIDDEN, E_LOBBY_SHOWN, E_LOBBY_HIDDEN,
       E_START, E_TRACK, E_CAR, E_READY, E_CHAT, E_EQUAL, E_FX, E_LEAVE, E_SLOT1, E_SLOT8 = E_SLOT1 + 7, E_QRANDOM, E_QNOTRANDOM, E_QSELECT, E_INVITE, E_COUNT };
static const char* k_ev[E_COUNT] = { 0, "BBR_CreateLobby", "BBR_JoinOpen", "BBR_NameOpen", "BBR_KbAccept", "BBR_KbClosed", "BBR_OnlineShown",
    "BBR_OnlineHidden", "BBR_LobbyShown", "BBR_LobbyHidden", "BBR_StartRace", "BBR_ChooseTrack", "BBR_ChooseCar", "BBR_Ready", "BBR_ChatOpen",
    "BBR_Equal", "BBR_Fx", "BBR_Leave", "BBR_Slot1", "BBR_Slot2", "BBR_Slot3", "BBR_Slot4", "BBR_Slot5", "BBR_Slot6", "BBR_Slot7", "BBR_Slot8", "BBR_QRandom", "BBR_QNotRandom", "BBR_QSelect", "BBR_Invite" };
static uint32_t g_evh[E_COUNT];
static void settings(int bots, int equal, int fx, int maxp) { net_cmd_settings(bots, equal, fx, maxp); g_L_ms = 0; }
static void on_event(int e) {
    const Lobby* L = lobby();
    switch (e) {
    case E_CREATE:
        if (L->cs == CS_HOSTING) { g_nav_sent = 0; break; }
        LOG("arayuz: Create Lobby"); g_track_ok = 0; net_cmd_host(g_port); break;
    case E_JOINOPEN: g_kbmode = 1; g_kb_filled = 0; break;
    case E_NAMEOPEN: g_kbmode = 2; g_kb_filled = 0; break;
    case E_KBACCEPT: kb_accept(); break;
    case E_KBCLOSED: g_kb_open = 0; g_kb = 0; g_kb_filled = 0; break;
    case E_ONLINE_SHOWN: g_screen = SC_ONLINE; g_nav_sent = 0; g_pick = 0; break;
    case E_ONLINE_HIDDEN: if (g_screen == SC_ONLINE) g_screen = SC_NONE; break;
    case E_LOBBY_SHOWN: g_screen = SC_LOBBY; g_closed_sent = 0; g_pick = 0; g_lobby_frozen = 0; memset(g_kstate, 0, sizeof g_kstate); g_tstate = 0; cards_adopt(); break;
    case E_QRANDOM: g_qrandom = 1; break;
    case E_QNOTRANDOM: if (g_qrandom) g_qrandom_off_ms = now_ms(); g_qrandom = 0; break;
    case E_QSELECT: g_qpick_random = g_qrandom || now_ms() - g_qrandom_off_ms < 400; LOG("ui: selection on the track grid (random %d)", g_qpick_random); break;
    case E_LOBBY_HIDDEN: if (g_screen == SC_LOBBY) g_screen = SC_NONE; fakes_remove("lobby closed"); memset(g_card, 0, sizeof g_card); vp_clear(); break;
    case E_START: g_do_start = 1; break;
    case E_TRACK: g_pick = 2; g_qpick_random = 0; g_qrandom = 0; g_lobby_frozen = 1; fakes_remove("track select"); break;
    case E_CAR: {
        g_pick = 1; g_lobby_frozen = 1; fakes_remove("car select");
        uint8_t* cfg = *(uint8_t**)(g_base + G_GAMECFG); if (!cfg) break;
        uint8_t* pb = *(uint8_t**)(cfg + CFG_PLAYERS); uint8_t* pe = *(uint8_t**)(cfg + CFG_PLAYERS + 8);
        int n = pb ? (int)((pe - pb) / 0x100) : 0;
        LOG("ui: opening car select with %d players, gamepad %d", n, n > 0 ? *(int*)pb : -1);
        if (n <= 0) {
            ((fn_resize)(g_base + FN_PLAYERS_RESIZE))(cfg + CFG_PLAYERS, 1);
            pb = *(uint8_t**)(cfg + CFG_PLAYERS);
            if (pb) { ((fn_plcopy)(g_base + FN_PLAYER_COPY))(pb, cfg + CFG_PLAYER_TMPL); *(int*)pb = online_local_pad(); }
            LOG("ui: player list was empty, local player added (gamepad %d)", online_local_pad());
        }
        pb = players(&n);
        if (pb) {   // a "Ready"/"Leave" state left from an earlier pick made the screen blank or close at once
            void* st = player_state(pb); LOG("ui: local player state '%s' -> Driver", jstr(st)); jset(st, "Driver");
        }
        break; }
    case E_READY: net_cmd_ready(!L->p[L->my_id].ready); g_L_ms = 0; break;
    case E_CHAT: g_kbmode = 3; g_kb_filled = 0; break;
    case E_EQUAL: settings(L->bots, !L->equal, L->effects, L->maxp); break;
    case E_FX: settings(L->bots, L->equal, !L->effects, L->maxp); break;
    case E_LEAVE: if (L->cs != CS_IDLE) { net_cmd_leave(); toast("You left the lobby"); } g_closed_sent = 1; g_track_ok = 0; g_nav_block_ms = now_ms() + 2000; g_lobby_frozen = 1; break;
    case E_INVITE:
        if (!L->code[0]) toast("%s", L->cs == CS_IDLE ? "Not in a lobby." : "Invites need a Steam lobby. Friends on IP join with your IP address.");
        else if (L->steam_net) toast("Spacewar lobby: friends join with the code %s.", L->code);
        else { net_cmd_invite(); toast("Opening the Steam invite window..."); }
        break;
    default:
        if (e >= E_SLOT1 && e <= E_SLOT8) {
            int i = e - E_SLOT1; const LobbySlot* sl = &L->slot[i];
            if (L->role == ROLE_HOST && sl->kind != SL_PLAYER) { net_cmd_slot(i); g_L_ms = 0; }
            else if (sl->kind == SL_PLAYER && sl->conn < MAXP && sl->conn != L->my_id) toast("%s", L->p[sl->conn].name);
        }
        break;
    }
}
static void MSABI hk_bcast(void* mgr, uint32_t hash, void* params) {
    for (int i = 1; i < E_COUNT; i++) if (hash == g_evh[i]) { LOG("ui: event %s", k_ev[i]); on_event(i); break; }
    o_bcast(mgr, hash, params);
}

// ---------- the game's UI actions: send pick flows started from the lobby back to the lobby ----------
static void* MSABI hk_start_ui(void* self, void* ret, const void* params) {
    int role = net_role();
    { int n; uint8_t* pb = players(&n); LOG("ui: StartActionGame (role %d, pick %d, players %d%s)", role, g_pick, n, g_fakes ? ", lobby players in the list" : "");
      for (int i = 0; pb && i < n && i < 8; i++) LOG("   player %d: gamepad %d, state '%s', car %s", i, *(int*)(pb + 0x100 * i), jstr(player_state(pb + 0x100 * i)), sstr(pb + 0x100 * i + PL_VEHICLE)); }
    if (role != ROLE_NONE && (g_pick == 1 || role == ROLE_CLIENT)) {
        g_pick = 0; ui_pop();
        CarSel c; int pad = 0; char nm[32]; race_read_local_car(&c, &pad, nm, sizeof nm);
        LOG("ui: car picked (%s / %s), returning to the lobby", c.vehicle, c.driver);
        if (role == ROLE_CLIENT) net_cmd_ready(1);
        char car[64]; toast("Car selected: %s", loc("Vehicle_", c.vehicle, car, sizeof car));
        *(int*)ret = 0; return ret;
    }
    return o_start_ui(self, ret, params);
}
static void* MSABI hk_push_ui(void* self, void* ret, const void* params) {
    const char* scr = sstr((uint8_t*)self + UIACT_SCREEN);
    LOG("ui: pushing screen %s", scr); vp_clear();
    if (!strcmp(scr, SCREEN_ONLINE_NAME)) {
        const Lobby* L = lobby();
        if (L->cs == CS_HOSTING || L->cs == CS_CONNECTED) {
            uint8_t* sp = (uint8_t*)self + UIACT_SCREEN;
            sassign(sp, L->cs == CS_HOSTING ? "Screens_Simple/Leaderboard" : "Screens_Simple/Credits");
            void* r = o_push_ui(self, ret, params); sassign(sp, SCREEN_ONLINE_NAME); return r;
        }
    }
    if (g_pick == 2 && net_role() == ROLE_HOST && !strncmp(scr, "Screens_Premium/SplitScreen_CarSelect", 37)) {
        g_pick = 0; ui_pop();
        uint8_t* cfg = *(uint8_t**)(g_base + G_GAMECFG);
        if (cfg) {
            const char* tr = sstr(cfg + CFG_TRACK); int laps = *(const int*)(cfg + CFG_LAPS);
            if (g_qpick_random) { net_cmd_track("Random", "", laps); toast("Track selected: Random"); }
            else { net_cmd_track(tr, sstr(cfg + CFG_SETTING), laps); char nm[96]; toast("Track selected: %s", loc("Track_", tr, nm, sizeof nm)); }
            g_track_ok = 1; g_L_ms = 0;
            LOG("ui: track picked '%s' (%d laps, path '%s', type '%s')", tr, laps, sstr(cfg + CFG_GAMEPATH), sstr(cfg + CFG_GAMETYPE));
        }
        *(int*)ret = 0; return ret;
    }
    return o_push_ui(self, ret, params);
}

// ---------- front-end tick (game thread) ----------
void ui_menu_tick(void) {
    hook_window();
    kb_drain();
    const Lobby* L = lobby();
    { static uint32_t seen; if (L->note_serial != seen) { seen = L->note_serial; if (L->note[0]) toast("%s", L->note); } }
    if (g_screen == SC_LOBBY && !g_lobby_frozen && L->cs != CS_IDLE) {
        lobby_sync(L);
        int rnd = !L->track[0] || !strcmp(L->track, "Random"), ts = rnd ? 1 : 2;
        if (ts != g_tstate) { g_tstate = ts; ui_send(rnd ? "BBR_TrackRandom" : "BBR_TrackReal"); }
        if (L->role == ROLE_CLIENT && !rnd) {   // client: the track image shows the host's track
            uint8_t* cfg = *(uint8_t**)(g_base + G_GAMECFG);
            if (cfg) { set_str(cfg + CFG_TRACK, L->track); if (L->setting[0]) set_str(cfg + CFG_SETTING, L->setting); }
        }
    }
    {   // connected through an invite (in the background at the main menu): open the Online screen, which moves on to the lobby
        static int prev_cs; static uint64_t until, last;
        if (L->cs == CS_CONNECTED && prev_cs != CS_CONNECTED && g_screen == SC_NONE) { until = now_ms() + 20000; last = 0; LOG("ui: connected from the menu, opening the Online screen"); }
        prev_cs = L->cs;
        if (until) {
            if (g_screen != SC_NONE || L->cs != CS_CONNECTED || now_ms() > until) until = 0;
            else if (now_ms() - last > 1500) { last = now_ms(); ui_send("BBR_GoOnline"); }
        }
    }
    if (g_screen == SC_ONLINE && !g_nav_sent && now_ms() > g_nav_block_ms && (L->cs == CS_HOSTING || L->cs == CS_CONNECTED)) {
        g_nav_sent = 1; ui_send(L->cs == CS_HOSTING ? "BBR_GoHostLobby" : "BBR_GoClientLobby");
    }
    if (g_screen == SC_LOBBY && !g_closed_sent && L->cs == CS_IDLE) {
        g_closed_sent = 1; ui_send("BBR_LobbyClosed"); if (L->error[0]) toast("Disconnected from the lobby");
    }
    if (g_do_start) {
        g_do_start = 0;
        if (L->role != ROLE_HOST) return;
        if (!L->track[0] || !strcmp(L->track, "Random")) { if (!random_track()) { toast("No track list found (bbr2mod\\ui\\tracks.txt)"); return; } }
        uint8_t* cfg = *(uint8_t**)(g_base + G_GAMECFG);
        int64_t np = cfg ? (*(int64_t*)(cfg + CFG_PLAYERS + 8) - *(int64_t*)(cfg + CFG_PLAYERS)) / 0x100 : 0;
        if (np <= 0) { toast("Choose your car first"); LOG("ui: player list empty, not started"); return; }
        int notready = 0; char who[32] = "";
        for (int i = 1; i < MAXP; i++) if (L->p[i].used && !L->p[i].ready) { notready++; if (!who[0]) snprintf(who, sizeof who, "%s", L->p[i].name); }
        if (notready && now_ms() - g_start_warn_ms > 6000) {
            g_start_warn_ms = now_ms();
            if (notready == 1) toast("%s is not ready. Press Start Race again to start without them.", who);
            else toast("%d players are not ready. Press Start Race again to start without them.", notready);
            return;
        }
        fakes_remove("start"); g_lobby_frozen = 1; g_start_warn_ms = 0; LOG("arayuz: Start Race"); ui_start_race();
    }
}

void ui_install(uint8_t* base) {
    g_base = base;
    load_overrides(); ini_load(); load_tracks();
    for (int i = 1; i < E_COUNT; i++) { char full[96]; snprintf(full, sizeof full, "OnGenericEvent:%s", k_ev[i]); g_evh[i] = fnv32(full); }
    void** vvc = rtti_vtable(base, "VuViewportContainerEntity");
    if (vvc) o_vpinit = (fn_vpinit)vt_hook(vvc, VPC_SLOT_INIT, (void*)hk_vpinit);
    void** vdb = rtti_vtable(base, "VuDBAsset");
    if (vdb && g_novr) o_dbload = (fn_load)vt_hook(vdb, DB_SLOT_LOAD, (void*)hk_dbload);
    void** vp = rtti_vtable(base, "VuProjectAsset");
    if (vp && g_novr) o_load = (fn_load)vt_hook(vp, PROJ_SLOT_LOAD, (void*)hk_load);
    static const uint8_t ms[GETMACRO_LEN] = GETMACRO_SIG, bs[BROADCASTH_LEN] = BROADCASTH_SIG;
    o_macro = (fn_macro)inline_hook(base + FN_GETMACRO, GETMACRO_LEN, ms, (void*)hk_macro);
    o_bcast = (fn_bcast)inline_hook(base + FN_BROADCASTH, BROADCASTH_LEN, bs, (void*)hk_bcast);
    g_kbvt = rtti_vtable(base, "VuGenericKeyboardEntity");
    if (g_kbvt) o_kbinit = (fn_kbinit)vt_hook(g_kbvt, KB_SLOT_INIT, (void*)hk_kbinit);
    void** vs = rtti_vtable(base, "VuStartActionGameUIAction"); void** vpu = rtti_vtable(base, "VuPushScreenUIAction");
    if (vs) o_start_ui = (fn_uiexec)vt_hook(vs, UIACT_SLOT_EXEC, (void*)hk_start_ui);
    if (vpu) o_push_ui = (fn_uiexec)vt_hook(vpu, UIACT_SLOT_EXEC, (void*)hk_push_ui);
    LOG("ui hooks: project %s, macro %s, event %s, keyboard %s, actions %s",
        o_load ? "ok" : "missing", o_macro ? "ok" : "ERROR", o_bcast ? "ok" : "ERROR", o_kbinit ? "ok" : "ERROR", o_start_ui && o_push_ui ? "ok" : "ERROR"); LOG("ui: card container hook %s", o_vpinit ? "ok" : "ERROR");
}
