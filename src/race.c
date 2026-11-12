// Race setup: reads the game's settings object (capture), applies a recipe with the game's own parser,
// and switches the front end to Action mode. The online layer (online.c) uses this file.
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include "core.h"
#include "game.h"
#include "fastc.h"
#include "race.h"
#include "online.h"
typedef bool (MSABI *fn_enter)(void* self);
typedef void (MSABI *fn_exit)(void* self);
typedef const char* (MSABI *fn_tick)(void* self, float dt);
typedef void (MSABI *fn_parse)(void* cfg, const void* root);
typedef void* (MSABI *fn_assign)(void* str, const char* p, size_t n);
typedef void (MSABI *fn_resize)(void* vec, size_t n);
typedef void (MSABI *fn_plcopy)(void* dst, const void* src);
static uint8_t* g_base;
static fn_enter o_action_enter; static fn_exit o_action_exit, o_fe_exit; static fn_tick o_fe_tick;
static RaceDesc g_cap, g_req; static volatile int g_have, g_pending, g_ok, g_req_set;
static volatile uint64_t g_fe_last_ms;
static uint8_t g_blob[65536];
static const char* sstr(const void* s) {   // MSVC std::string reader
    uint64_t size = *(const uint64_t*)((const char*)s + 0x10), cap = *(const uint64_t*)((const char*)s + 0x18);
    if (cap < 15 || size > cap || cap > 65536) return "";
    return cap > 15 ? *(const char* const*)s : (const char*)s;
}
static void scpy(char* d, int cap, const char* s) { strncpy(d, s, cap - 1); d[cap - 1] = 0; }
static const char* tbl(uint32_t rva, int n, int i) { return (i >= 0 && i < n) ? *(const char**)(g_base + rva + 8*i) : "?"; }
void* race_cfg(void) { return g_base ? *(void**)(g_base + G_GAMECFG) : 0; }
int race_ready(void) { return g_ok; }
int race_get_capture(RaceDesc* out) { if (!g_have) return 0; *out = g_cap; return 1; }
void race_request(const RaceDesc* d) { g_req = *d; g_req_set = 1; }
int race_pending(void) { return g_req_set || g_pending; }
int race_in_menu(void) { return now_ms() - g_fe_last_ms < 500; }
int race_read_local_car(CarSel* car, int* pad, char* name, int namecap) {
    const uint8_t* c = race_cfg(); if (!c) return 0;
    const uint8_t* pb = *(const uint8_t* const*)(c + CFG_PLAYERS); const uint8_t* pe = *(const uint8_t* const*)(c + CFG_PLAYERS + 8);
    const uint8_t* p = (pb && pe > pb) ? pb : c + CFG_PLAYER_TMPL;
    scpy(car->vehicle, 48, sstr(p + PL_VEHICLE)); scpy(car->driver, 48, sstr(p + PL_DRIVER)); scpy(car->skin, 48, sstr(p + PL_SKIN));
    scpy(car->paint, 48, sstr(p + PL_PAINT)); scpy(car->decalcolor, 48, sstr(p + PL_DECALCOLOR)); scpy(car->decal, 48, sstr(p + PL_DECAL));
    if (pad) { int v = *(const int*)(p + PL_PAD); *pad = v < 0 ? 0 : v; }
    if (name && namecap) { name[0] = 0; if (g_have) for (int i = 0; i < g_cap.nveh; i++) if (g_cap.v[i].input == 0) { scpy(name, namecap, g_cap.v[i].name); break; } }
    return car->vehicle[0] != 0;
}
static void capture(const uint8_t* c, RaceDesc* d) {
    memset(d, 0, sizeof *d);
    scpy(d->gamepath, sizeof d->gamepath, tbl(TBL_GAMEPATH, TBL_GAMEPATH_N, *(const int*)(c + CFG_GAMEPATH)));
    scpy(d->gametype, sizeof d->gametype, tbl(TBL_GAMETYPE, TBL_GAMETYPE_N, *(const int*)(c + CFG_GAMETYPE)));
    scpy(d->track, sizeof d->track, sstr(c + CFG_TRACK)); scpy(d->setting, sizeof d->setting, sstr(c + CFG_SETTING));
    d->mirror = c[CFG_MIRROR]; d->laps = *(const int*)(c + CFG_LAPS); d->gridmode = *(const int*)(c + CFG_GRIDMODE);
    const uint8_t* sp = *(const uint8_t* const*)(c + CFG_SHAREDDECK); const uint8_t* se = *(const uint8_t* const*)(c + CFG_SHAREDDECK + 8);
    for (; sp && sp < se && d->nshared < 64; sp += VC_DECK_ELEM) snprintf(d->shared[d->nshared++], 40, "%.28s:%d", sstr(sp), *(const int*)(sp + 0x20));
    const uint8_t* pb = *(const uint8_t* const*)(c + CFG_PLAYERS); const uint8_t* pe = *(const uint8_t* const*)(c + CFG_PLAYERS + 8);
    d->nplayers = pb ? (int)((pe - pb) / PL_SIZE) : 0; d->pad0 = d->nplayers > 0 ? *(const int*)pb : -1;
    const uint8_t* b = *(const uint8_t* const*)(c + CFG_VEHICLES); const uint8_t* e = *(const uint8_t* const*)(c + CFG_VEHICLES + 8);
    for (; b && b < e && d->nveh < 12; b += VC_SIZE) {
        VehDesc* v = &d->v[d->nveh++];
        scpy(v->vehicle, 48, sstr(b + VC_VEHICLE)); scpy(v->driver, 48, sstr(b + VC_DRIVER)); scpy(v->skin, 48, sstr(b + VC_DRIVERSKIN));
        scpy(v->paint, 48, sstr(b + VC_PAINT)); scpy(v->decalcolor, 48, sstr(b + VC_DECALCOLOR)); scpy(v->decal, 48, sstr(b + VC_DECAL));
        scpy(v->name, 48, sstr(b + VC_NAME)); v->powerups = b[VC_POWERUPS];
        v->level = *(const int*)(b + VC_LEVEL); v->input = *(const int*)(b + VC_INPUT); v->ctrlmask = *(const uint32_t*)(b + VC_CTRLMASK);
        v->viewport = *(const int*)(b + VC_VIEWPORT); v->ailevel = *(const int*)(b + VC_AILEVEL); v->ability = *(const int*)(b + VC_ABILITY);
        if (d->nveh == 1) scpy(d->script, sizeof d->script, sstr(b + VC_SCRIPT));
        const uint8_t* p = *(const uint8_t* const*)(b + VC_DECK); const uint8_t* q = *(const uint8_t* const*)(b + VC_DECK + 8);
        for (; p && p < q && v->ndeck < 16; p += VC_DECK_ELEM) snprintf(v->deck[v->ndeck++], 40, "%.28s:%d", sstr(p), *(const int*)(p + 0x20));
    }
}
void race_dump(const char* title, const RaceDesc* d) {
    LOG("=== recipe (%s): path=%s type=%s track=%s setting=%s mirror=%d laps=%d grid=%d cars=%d sharedDeck=%d players=%d/%d",
        title, d->gamepath, d->gametype, d->track, d->setting, d->mirror, d->laps, d->gridmode, d->nveh, d->nshared, d->nplayers, d->pad0);
    for (int i = 0; i < d->nveh; i++) {
        const VehDesc* v = &d->v[i];
        LOG("  #%d %s car=%s driver=%s paint=%s name='%s' level=%d aiLevel=%d viewport=%d pad=%x powerups=%d deck=%d",
            i, v->input == 0 ? "HUMAN" : "YZ", v->vehicle, v->driver, v->paint, v->name, v->level, v->ailevel, v->viewport, v->ctrlmask, v->powerups, v->ndeck);
    }
}
static uint32_t build_blob(const RaceDesc* d) {
    fc_reset(); Fc* r = fc_obj();
    fc_set(r, "GamePath", fc_str(d->gamepath)); fc_set(r, "GameType", fc_str(d->gametype));
    fc_set(r, "Track", fc_str(d->track)); fc_set(r, "Setting", fc_str(d->setting));
    fc_set(r, "MirrorTrack", fc_bool(d->mirror)); fc_set(r, "LapCount", fc_int(d->laps));
    if (d->script[0]) fc_set(r, "RaceScript", fc_str(d->script));
    if (d->nshared) { Fc* sd = fc_arr(); for (int j = 0; j < d->nshared; j++) fc_push(sd, fc_str(d->shared[j])); fc_set(r, "SharedPowerUpDeck", sd); }
    Fc* vs = fc_arr();
    for (int i = 0; i < d->nveh; i++) {
        const VehDesc* v = &d->v[i]; Fc* o = fc_obj();
        fc_set(o, "Vehicle", fc_str(v->vehicle)); fc_set(o, "Driver", fc_str(v->driver)); fc_set(o, "DriverSkin", fc_str(v->skin));
        fc_set(o, "PaintColor", fc_str(v->paint)); fc_set(o, "DecalColor", fc_str(v->decalcolor)); fc_set(o, "Decal", fc_str(v->decal));
        fc_set(o, "Level", fc_int(v->level)); fc_set(o, "AiLevel", fc_int(v->ailevel));
        fc_set(o, "ViewportIndex", fc_int(v->viewport)); fc_set(o, "AbilityUses", fc_int(v->ability));
        fc_set(o, "InputType", fc_str(v->input == 0 ? "Human" : "AI"));
        if (v->ndeck) { Fc* dk = fc_arr(); for (int j = 0; j < v->ndeck; j++) fc_push(dk, fc_str(v->deck[j])); fc_set(o, "PowerUpDeck", dk); }
        fc_push(vs, o);
    }
    fc_set(r, "Vehicles", vs);
    return fc_build(r, g_blob, sizeof g_blob);
}
static void apply(uint8_t* c, RaceDesc d, int allow_ini) {
    char t[64];
    if (d.gamepath[0] == '?' || !strcmp(d.gamepath, "Dev")) scpy(d.gamepath, sizeof d.gamepath, "QuickEvent");
    if (allow_ini) {
        if (cfg_str("replay_track", t, sizeof t)) { scpy(d.track, sizeof d.track, t); d.setting[0] = 0; }
        if (cfg_str("replay_laps", t, sizeof t)) d.laps = atoi(t);
        if (cfg_str("replay_grid", t, sizeof t)) d.gridmode = atoi(t);
    }
    uint32_t n = build_blob(&d);
    if (!n) { LOG("ERROR: could not build the recipe tree"); return; }
    ((fn_parse)(g_base + FN_CFG_PARSE))(c, g_blob);
    fn_assign assign = (fn_assign)(g_base + FN_STR_ASSIGN);
    *(int*)(c + CFG_GRIDMODE) = d.gridmode;
    uint8_t* pb = *(uint8_t**)(c + CFG_PLAYERS); uint8_t* pe = *(uint8_t**)(c + CFG_PLAYERS + 8);
    if (!pb || pe <= pb) {
        ((fn_resize)(g_base + FN_PLAYERS_RESIZE))(c + CFG_PLAYERS, 1);
        pb = *(uint8_t**)(c + CFG_PLAYERS);
        if (pb) ((fn_plcopy)(g_base + FN_PLAYER_COPY))(pb, c + CFG_PLAYER_TMPL);
        LOG("player list was empty: 1 player created from the template");
    }
    if (pb && d.nplayers > 0 && d.pad0 >= 0) *(int*)pb = d.pad0;
    uint8_t* b = *(uint8_t**)(c + CFG_VEHICLES); uint8_t* e = *(uint8_t**)(c + CFG_VEHICLES + 8);
    for (int i = 0; b && b < e && i < d.nveh; b += VC_SIZE, i++) {
        *(uint32_t*)(b + VC_CTRLMASK) = d.v[i].ctrlmask;
        b[VC_POWERUPS] = (uint8_t)d.v[i].powerups;
        assign(b + VC_NAME, d.v[i].name, strlen(d.v[i].name));
    }
}
static bool MSABI hk_action_enter(void* self) {
    uint8_t* c = race_cfg();
    if (c) {
        RaceDesc now, variant;
        if (g_pending) {
            g_pending = 0; cfg_load();
            int online = online_is_pending_race();
            apply(c, g_req, !online);
            capture(c, &now); race_dump(online ? "online, applied" : "tekrar, uygulanan", &now);
            online_race_entered(c, online);
        } else {
            capture(c, &now);
            if (now.nveh > 0) { g_cap = now; g_have = 1; }
            race_dump("built by the game", &now);
            if (online_host_convert(&now, &variant)) {     // host: a race started from the menu is converted to online
                apply(c, variant, 0);
                capture(c, &now); race_dump("online, host", &now);
                online_race_entered(c, 1);
            } else if (online_host_trim(&now, &variant)) {  // host alone: the bot count from the lobby
                apply(c, variant, 0);
                capture(c, &now); race_dump("host alone", &now);
                online_race_entered(c, 0);
            } else online_race_entered(c, 0);
        }
    }
    return o_action_enter(self);
}
static void MSABI hk_action_exit(void* self) { online_race_exit(); o_action_exit(self); }
static void MSABI hk_fe_exit(void* self) {
    ui_fe_exit();
    LOG("front end exit: next mode='%s' path='%s'", sstr((char*)self + FE_NEXTMODE), sstr((char*)self + FE_GAMEPATH));
    o_fe_exit(self);
}
static void trigger(void* self) {
    fn_assign assign = (fn_assign)(g_base + FN_STR_ASSIGN);
    assign((char*)self + FE_GAMEPATH, "", 0);
    assign((char*)self + FE_NEXTMODE, "Action", 6);
    g_pending = 1;
}
static const char* MSABI hk_fe_tick(void* self, float dt) {
    static int was;
    g_fe_last_ms = now_ms();
    online_menu_tick();
    ui_menu_tick();
    int busy = *(uint64_t*)((char*)self + FE_NEXTMODE + 0x10) != 0;
    if (g_req_set && !busy && !g_pending) { g_req_set = 0; trigger(self); LOG("race request: switching to Action mode"); }
    #define KD(k) ((GetAsyncKeyState(k) & 0x8000) != 0)
    int down = KD(VK_F9) || (KD(VK_CONTROL) && KD('R'));   // offline replay (only when not online)
    if (down && !was && g_ok && !online_active() && cfg_num("dev", 0) > 0) {   // developer only: bbr2net.ini dev=1
        if (!g_have) LOG("trigger: no race captured yet");
        else if (busy) LOG("trigger: a mode change is already in progress");
        else { g_req = g_cap; trigger(self); LOG("trigger: restarting the last race"); }
    }
    was = down;
    return o_fe_tick(self, dt);
}
void race_install(uint8_t* base) {
    g_base = base;
    static const uint8_t sig_parse[8] = { 0x48, 0x89, 0x5c, 0x24, 0x18, 0x55, 0x56, 0x57 };
    static const uint8_t sig_assign[8] = { 0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x6c };
    static const uint8_t sig_resize[8] = { 0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x74 };
    static const uint8_t sig_plcopy[8] = { 0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74 };
    g_ok = !memcmp(base + FN_CFG_PARSE, sig_parse, 8) && !memcmp(base + FN_STR_ASSIGN, sig_assign, 8)
        && !memcmp(base + FN_PLAYERS_RESIZE, sig_resize, 8) && !memcmp(base + FN_PLAYER_COPY, sig_plcopy, 8);
    void** va = rtti_vtable(base, "VuActionGameMode");
    void** vf = rtti_vtable(base, "VuFrontEndGameMode");
    LOG("race: signature %s, vtable Action %p FrontEnd %p", g_ok ? "ok" : "UYUSMUYOR", (void*)va, (void*)vf);
    if (!g_ok || !va || !vf) { LOG("ERROR: race hooks not installed"); g_ok = 0; return; }
    o_action_enter = (fn_enter)vt_hook(va, GM_SLOT_ENTER, (void*)hk_action_enter);
    o_action_exit = (fn_exit)vt_hook(va, GM_SLOT_EXIT, (void*)hk_action_exit);
    o_fe_exit = (fn_exit)vt_hook(vf, GM_SLOT_EXIT, (void*)hk_fe_exit);
    o_fe_tick = (fn_tick)vt_hook(vf, GM_SLOT_TICK, (void*)hk_fe_tick);
    LOG("race hooks installed");
}
