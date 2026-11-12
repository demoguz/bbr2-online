// Online race: recipe distribution, synchronised start (hold at the end of the intro), car ownership and puppets.
#include <windows.h>
#include <stdio.h>
#include <math.h>
#include <stdarg.h>
#include "core.h"
#include "game.h"
#include "race.h"
#include "repl.h"
#include "session.h"
#include "online.h"
typedef void (MSABI *fn_update)(void* self, float dt);
typedef void (MSABI *fn_setcond)(void* fsm, const char* name, uint8_t v);
typedef uint32_t (MSABI *fn_effect)(void* ctrl, const char* name, const void* params);
typedef uint64_t (MSABI *fn_sysraw)(void* sys);
static fn_effect o_effect; static volatile int g_replaying;
typedef void* (MSABI *fn_uiexec)(void* self, void* ret, const void* params);
typedef void (MSABI *fn_broadcast)(void* mgr, const char* ev, void* params);
static fn_uiexec o_restart_ui, o_frontend_ui, o_tip_ui;
static volatile int g_do_restart, g_do_frontend;   // network commands: applied on the game thread
static char g_toast[160]; static uint64_t g_toast_ms;
static int g_eff_sent, g_eff_supp, g_eff_rep, g_eff_log;
static fn_update o_human, o_ai; static fn_setcond o_setcond;
static uint8_t* g_base; static const char *s_cine, *s_skip, *s_force, *s_start, *s_end;
// countdown lock and host-refereed results
static int64_t g_go_qpc, g_rel_qpc; static void* g_go_fsm; static int g_go_pending, g_go_done, g_is_race;
static uint8_t g_fin_sent[12]; static int g_res_sent, g_res_applied, g_end_wait, g_end_local; static RaceResult g_res;
static uint64_t g_first_fin_ms, g_end_wait_ms;
#define END_TIMEOUT_MS 30000   // wait at most this long after the first human finishes
static volatile int g_race_on, g_hold, g_released, g_pending_online;
static OnlineRace g_race; static int g_my_slot = -1, g_my_id;
static int g_seen_cine, g_loaded_sent; static uint64_t g_hold_t0;
static int64_t g_t0;
static void* g_slotveh[12];
static double g_last_send, g_last_stat, g_last_state;
static ReplCfg g_rc = { 0.15f, 0.4f, 0.15f, 0.4f, 8.0f, 0.35f, 0, {0,0,0}, 1 };
static ReplStats g_st[12];
static OnlineRace g_last_host; static int g_have_last;
static char g_name[32]; static CarSel g_car; static int g_have_car, g_pad;
static int g_warned_slot;
static int64_t qpc(void) { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
static double qpf(void) { static LARGE_INTEGER f; if (!f.QuadPart) QueryPerformanceFrequency(&f); return (double)f.QuadPart; }
static double clock_s(void) { return g_released ? (qpc() - g_t0) / qpf() : 0; }
void online_set_name(const char* n) { strncpy(g_name, n, 31); g_name[31] = 0; }
void online_get_name(char* out, int cap) { strncpy(out, g_name, cap - 1); out[cap - 1] = 0; }
int online_active(void) { return net_connected(); }
int online_is_pending_race(void) { return g_pending_online; }
int online_last_start_available(void) { return net_role() == ROLE_HOST && race_in_menu(); }
static int slot_of(void* veh) {
    uint8_t* c = race_cfg(); if (!c || !veh) return -1;
    uint8_t* b = *(uint8_t**)(c + CFG_VEHICLES); uint8_t* e = *(uint8_t**)(c + CFG_VEHICLES + 8);
    uint8_t* p = *(uint8_t**)((uint8_t*)veh + VEH_CFG);
    if (!b || p < b || p >= e || (p - b) % VC_SIZE) return -1;
    int s = (int)((p - b) / VC_SIZE); return s < 12 ? s : -1;
}
static void make_variant(const OnlineRace* r, int my_id, int pad, RaceDesc* out) {
    *out = r->desc;
    for (int i = 0; i < r->nslot && i < out->nveh; i++) {
        VehDesc* v = &out->v[i];
        if (r->human[i] && r->owner[i] == my_id) { v->input = 0; v->viewport = 0; v->ctrlmask = 1u << (pad & 31); }
        else { v->input = 1; v->viewport = -1; v->ctrlmask = 0; }
    }
    out->gridmode = 0; out->nplayers = 1; out->pad0 = pad;
}
static void apply_car(VehDesc* v, const CarSel* c) {
    if (!c->vehicle[0]) return;
    memcpy(v->vehicle, c->vehicle, 48); memcpy(v->driver, c->driver, 48); memcpy(v->skin, c->skin, 48);
    memcpy(v->paint, c->paint, 48); memcpy(v->decalcolor, c->decalcolor, 48); memcpy(v->decal, c->decal, 48);
}
static void same_car(VehDesc* v, const VehDesc* host) {   // "Same Car for All": car, skin and decal from the host; driver and paint stay the player's own
    memcpy(v->vehicle, host->vehicle, 48); memcpy(v->skin, host->skin, 48); memcpy(v->decal, host->decal, 48); memcpy(v->decalcolor, host->decalcolor, 48);
}
static int build_race(const RaceDesc* base, OnlineRace* r) {
    uint8_t ids[MAXP]; PeerInfo inf[MAXP]; int n = 0;
    net_host_ready_peers(ids, &n, inf);
    if (n == 0) return 0;
    Lobby L; net_lobby_copy(&L);
    int hi = -1; for (int i = 0; i < base->nveh; i++) if (base->v[i].input == 0) { hi = i; break; }
    if (hi < 0) { LOG("online: no human car in the recipe, could not convert"); return 0; }
    memset(r, 0, sizeof *r);
    static uint32_t ctr; r->race_id = ((uint32_t)now_ms() << 8) | (++ctr & 0xff);
    RaceDesc* d = &r->desc; *d = *base; d->nveh = 0;
    int humans = n + 1, ai = L.bots; if (ai > 8 - humans) ai = 8 - humans; if (ai < 0) ai = 0;
    int nai = 0; for (int i = 0; i < base->nveh && nai < ai; i++) if (base->v[i].input != 0) { d->v[d->nveh] = base->v[i]; d->v[d->nveh].name[0] = 0; if (L.equal) same_car(&d->v[d->nveh], &base->v[hi]);
        r->owner[d->nveh] = 0; r->human[d->nveh] = 0; d->nveh++; nai++; }
    VehDesc hv = base->v[hi]; hv.ndeck = 0; hv.powerups = 1; hv.ailevel = base->v[hi].ailevel;
    for (int k = 0; k <= n; k++) {     // clients, then the host last (starts at the back)
        VehDesc v = hv;
        if (k < n) { apply_car(&v, &inf[k].car); if (L.equal) same_car(&v, &hv); memcpy(v.name, inf[k].name, 32); r->owner[d->nveh] = ids[k]; }
        else { memcpy(v.name, L.p[0].name, 32); r->owner[d->nveh] = 0; }
        v.name[31] = 0; r->human[d->nveh] = 1; d->v[d->nveh++] = v;
    }
    r->nslot = (uint8_t)d->nveh; d->gridmode = 0; r->effects = L.effects;
    return 1;
}
int online_local_pad(void) { return g_pad; }
int online_host_trim(const RaceDesc* base, RaceDesc* out) {
    if (net_role() != ROLE_HOST) return 0;
    Lobby L; net_lobby_copy(&L);
    int nh = 0; for (int i = 0; i < base->nveh; i++) if (base->v[i].input == 0) nh++;
    int ai = L.bots; if (ai > 8 - nh) ai = 8 - nh; if (ai < 0) ai = 0;
    *out = *base; out->nveh = 0; int nai = 0;
    for (int i = 0; i < base->nveh; i++) if (base->v[i].input == 0 || nai++ < ai) out->v[out->nveh++] = base->v[i];
    if (L.equal) { int hi = -1; for (int i = 0; i < out->nveh; i++) if (out->v[i].input == 0) { hi = i; break; }
        if (hi >= 0) for (int i = 0; i < out->nveh; i++) if (i != hi) same_car(&out->v[i], &out->v[hi]); }
    if (out->nveh == base->nveh && !L.equal) return 0;
    LOG("online: solo race, %d bots (%d cars)%s", ai, out->nveh, L.equal ? ", same car for all" : ""); return 1;
}
int online_host_convert(const RaceDesc* base, RaceDesc* local) {
    if (net_role() != ROLE_HOST) return 0;
    OnlineRace r;
    if (!build_race(base, &r)) { LOG("online: no ready client, this race is offline"); return 0; }
    g_last_host = r; g_have_last = 1;
    net_send_start(&r);
    g_race = r; g_my_id = 0;
    int pad = 0; CarSel tmp; race_read_local_car(&tmp, &pad, 0, 0);
    make_variant(&r, 0, pad, local);
    LOG("online: race %u set up, %d cars, %s", r.race_id, r.nslot, r.desc.track);
    return 1;
}
void online_host_restart_last(void) {
    RaceDesc base; if (!race_get_capture(&base)) { LOG("online: start a race from the game's menu first"); return; }
    OnlineRace r; if (!build_race(&base, &r)) { LOG("online: no ready client"); return; }
    g_last_host = r; g_have_last = 1; net_send_start(&r); g_race = r; g_my_id = 0;
    int pad = 0; CarSel tmp; race_read_local_car(&tmp, &pad, 0, 0);
    RaceDesc v; make_variant(&r, 0, pad, &v); g_pending_online = 1; race_request(&v);
    LOG("online: restarting race %u (%s)", r.race_id, r.desc.track);
}
void online_toast(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); vsnprintf(g_toast, sizeof g_toast, fmt, ap); va_end(ap); g_toast_ms = now_ms();
    LOG("notice: %s", g_toast);
}
static void hold_reset(void) {
    memset(g_slotveh, 0, sizeof g_slotveh); memset(g_st, 0, sizeof g_st);
    g_hold = 1; g_released = 0; g_seen_cine = 0; g_loaded_sent = 0; g_hold_t0 = now_ms(); g_last_send = g_last_stat = g_last_state = 0; g_warned_slot = 0;
    g_eff_sent = g_eff_supp = g_eff_rep = g_eff_log = 0;
    g_go_qpc = g_rel_qpc = 0; g_go_fsm = 0; g_go_pending = g_go_done = 0;
    memset(g_fin_sent, 0, sizeof g_fin_sent); g_res_sent = g_res_applied = g_end_wait = g_end_local = 0; g_first_fin_ms = g_end_wait_ms = 0;
}
static void broadcast_event(const char* ev) {
    void* mgr = *(void**)(g_base + G_EVENTMGR); if (!mgr) return;
    static uint8_t params[0x110]; memset(params, 0, sizeof params);
    ((fn_broadcast)(g_base + FN_BROADCAST))(mgr, ev, params);
}
static int owned_here(int slot);
static double hclock(void) { return (double)(net_host_now_us() % 100000000000LL) / 1e6; }   // ortak saat, sn (gunluk)
static double since_rel(void) { return g_rel_qpc ? (qpc() - g_rel_qpc) / qpf() : 0; }
static void go_poll(void) {
    if (!g_go_pending || qpc() < g_go_qpc) return;
    g_go_pending = 0; g_go_done = 1; o_setcond(g_go_fsm, s_start, 1);
    LOG("olcum: GO (ortak an), serbest+%.3f s, ortak saat %.3f", since_rel(), hclock());
}
static void res_log(const char* who) {
    char line[700]; int k = snprintf(line, sizeof line, "measure: final results (%s):", who);
    for (int i = 0; i < g_res.n && i < 12 && k < 640; i++) {
        void* v = g_slotveh[i]; double lt = v ? *(double*)((char*)v + VEH_TIME) : -1; int lf = v ? *(uint8_t*)((char*)v + VEH_FINISHED) : -1;
        if (g_res.fin[i]) k += snprintf(line + k, sizeof line - k, " #%d %.3f (local %s%.3f);", i, g_res.time[i], lf == 1 ? "" : "unfinished ", lt);
        else k += snprintf(line + k, sizeof line - k, " #%d did not finish;", i);
    }
    LOG("%s", line);
}
static void res_apply(void) {   // write the host's table into the vehicles (every tick, so that a later write by the game is corrected)
    for (int i = 0; i < g_res.n && i < 12; i++) {
        char* v = (char*)g_slotveh[i]; if (!v || !g_res.fin[i]) continue;
        *(uint8_t*)(v + VEH_FINISHED) = 1; *(double*)(v + VEH_TIME) = g_res.time[i]; *(uint8_t*)(v + VEH_DNF) = 0;
    }
}
static void results_tick(void) {
    if (!g_race_on || !g_released || !g_is_race) return;
    uint32_t rid = g_race.race_id;
    for (int i = 0; i < g_race.nslot && i < 12; i++) {   // this PC's cars: report the moment they finish
        char* v = (char*)g_slotveh[i]; if (!v || g_fin_sent[i] || !owned_here(i) || !*(uint8_t*)(v + VEH_FINISHED)) continue;
        g_fin_sent[i] = 1; double t = *(double*)(v + VEH_TIME); net_report_finish(rid, i, t);
        LOG("measure: slot %d finished (%s), time %.3f s, common clock %.3f", i, g_race.human[i] ? "human" : "YZ", t, hclock());
    }
    if (g_res_applied) { res_apply(); return; }
    if (net_role() == ROLE_HOST) {
        uint8_t has[12]; double t[12]; if (!net_finish_table(rid, has, t)) return;
        int need = 0, done = 0;
        for (int i = 0; i < g_race.nslot && i < 12; i++) if (g_race.human[i] && (g_race.owner[i] != 0 || i == g_my_slot)) { need++; done += has[i] != 0; }
        if (done && !g_first_fin_ms) g_first_fin_ms = now_ms();
        int timeout = g_first_fin_ms && now_ms() - g_first_fin_ms >= END_TIMEOUT_MS;
        if (!need || (done < need && !timeout)) return;
        memset(&g_res, 0, sizeof g_res); g_res.race_id = rid; g_res.n = (uint8_t)g_race.nslot;
        for (int i = 0; i < g_race.nslot && i < 12; i++) { g_res.fin[i] = has[i]; g_res.time[i] = has[i] ? t[i] : 0; }
        net_send_result(&g_res); g_res_sent = g_res_applied = 1; res_apply();
        LOG("online: race result final (%s), sent to everyone", timeout ? "timeout" : "all players finished"); res_log("host");
    } else if (net_take_result(rid, &g_res)) { g_res_applied = 1; res_log("from host"); res_apply(); }
}
void online_race_entered(void* cfg, int online) {
    (void)cfg;
    g_pending_online = 0;
    if (!online) { g_race_on = 0; return; }
    g_my_id = net_my_id(); g_my_slot = -1;
    for (int i = 0; i < g_race.nslot; i++) if (g_race.human[i] && g_race.owner[i] == g_my_id) g_my_slot = i;
    g_is_race = !strcmp(g_race.desc.gametype, "Race");
    hold_reset(); g_do_restart = g_do_frontend = 0; { WireEffect junk; while (net_take_effect(&junk)) {} RaceCmd rc; while (net_take_racecmd(&rc)) {} }
    g_race_on = 1;
    LOG("online: race %u started, my slot=%d, effect sync %s, owners:", g_race.race_id, g_my_slot, g_race.effects ? "on" : "off");
    for (int i = 0; i < g_race.nslot; i++) LOG("   slot %d: %s owner=%d %s", i, g_race.human[i] ? "human" : "YZ", g_race.owner[i], g_race.desc.v[i].name);
}
void online_race_exit(void) {
    if (g_race_on) { LOG("online: race %u finished/left", g_race.race_id); ui_race_ended(); }
    g_race_on = 0; g_hold = 0; g_released = 0;
}
void online_menu_tick(void) {
    static uint64_t last;
    uint64_t now = now_ms();
    OnlineRace r;
    if (net_role() == ROLE_CLIENT && net_take_start(&r)) {
        if (race_pending()) LOG("online: a race is already pending, new recipe ignored");
        else {
            int pad = 0; CarSel tmp; race_read_local_car(&tmp, &pad, 0, 0);
            g_race = r; RaceDesc v; make_variant(&r, net_my_id(), pad, &v);
            g_pending_online = 1; race_request(&v);
            LOG("online: loading the host's race (%s)", r.desc.track);
        }
    }
    if (now - last < 500) return; last = now;
    CarSel c; memset(&c, 0, sizeof c); int pad = 0; char nm[48];
    if (race_read_local_car(&c, &pad, nm, sizeof nm)) { g_car = c; g_have_car = 1; g_pad = pad; if (!g_name[0] && nm[0]) online_set_name(nm); }
    if (!g_name[0]) strcpy(g_name, "Player");
    net_set_local(g_name, &g_car, PS_MENU);
}
static int owned_here(int slot) {
    if (slot < 0 || slot >= g_race.nslot) return 1;
    return g_race.human[slot] ? g_race.owner[slot] == g_my_id : g_my_id == 0;
}
static const char* sstr_(const void* s) {
    uint64_t size = *(const uint64_t*)((const char*)s + 0x10), cap = *(const uint64_t*)((const char*)s + 0x18);
    if (cap < 15 || size > cap || cap > 4096) return "";
    return cap > 15 ? *(const char* const*)s : (const char*)s;
}
static void* powerup_find(const char* name) {     // lookup by name in the VuPowerUpManager map (same as 0xc2a41)
    uint8_t* m = *(uint8_t**)(g_base + G_POWERUPMGR); if (!m || !name[0]) return 0;
    uint32_t h = 0x811c9dc5u; for (const char* p = name; *p; p++) h = (h ^ (uint8_t)*p) * 0x1000193u;
    uint64_t k = 0xcbf29ce484222325ull; for (int i = 0; i < 4; i++) k = (k ^ ((h >> (8*i)) & 0xff)) * 0x100000001b3ull;
    uint8_t* end = *(uint8_t**)(m + 0x58); uint8_t* bk = *(uint8_t**)(m + 0x68); uint64_t mask = *(uint64_t*)(m + 0x80);
    if (!bk) return 0;
    uint8_t* b = bk + (k & mask) * 16; uint8_t* first = *(uint8_t**)b; uint8_t* n = *(uint8_t**)(b + 8);
    if (n == end) return 0;
    for (int guard = 0; guard < 4096; guard++) {
        if (*(uint32_t*)(n + 0x10) == h) return *(void**)(n + 0x18);
        if (n == first) return 0;
        n = *(uint8_t**)(n + 8);
    }
    return 0;
}
static void replay_effects(void) {
    WireEffect e;
    while (net_take_effect(&e)) {
        if (e.tslot >= 12) continue;
        void* veh = g_slotveh[e.tslot];
        if (!veh || owned_here(e.tslot)) continue;       // another PC's decision is not applied to our own car
        uint8_t params[EP_SIZE]; memset(params, 0, sizeof params);
        *(int32_t*)(params + EP_LEVEL) = e.level;
        *(void**)(params + EP_SOURCE) = e.sslot < 12 ? g_slotveh[e.sslot] : 0;
        *(void**)(params + EP_POWERUP) = powerup_find(e.powerup);
        void* sys = *(void**)(g_base + G_SYS);
        if (sys) *(uint64_t*)(params + EP_TIME) = ((fn_sysraw)VT(sys)[8])(sys);
        memcpy(params + EP_FLAG, &e.flag, 8);
        void* ctrl = *(void**)((uint8_t*)veh + VEH_EFFECTS); if (!ctrl) continue;
        g_replaying = 1; uint32_t r = o_effect(ctrl, e.name, params); g_replaying = 0;
        g_eff_rep++;
        if (g_eff_log < 60) { g_eff_log++; LOG("effect (remote): %s -> slot %d, source %d, power-up '%s'%s, result %u", e.name, e.tslot, e.sslot == 255 ? -1 : e.sslot,
                                               e.powerup, e.powerup[0] && !*(void**)(params + EP_POWERUP) ? " NOT FOUND" : "", r); }
    }
}
static uint32_t MSABI hk_effect(void* ctrl, const char* name, const void* params) {
    if (!g_race_on || g_replaying || !ctrl) {
        uint32_t r = o_effect(ctrl, name, params);
        if (!g_race_on && !g_replaying && g_eff_log < 40 && ctrl) {   // offline: log to learn what happens
            const uint8_t* pr = (const uint8_t*)params; void* pu = *(void**)(pr + EP_POWERUP);
            const char* pn = pu ? sstr_(pu) : ""; void* found = pu ? powerup_find(pn) : 0;
            g_eff_log++; LOG("effect: %s -> slot %d, source %d, level %d, power-up '%s'%s, result %u", name, slot_of(*(void**)ctrl),
                             slot_of(*(void**)(pr + EP_SOURCE)), *(const int*)(pr + EP_LEVEL), pn,
                             !pu ? "" : found == pu ? " (adla arama tutuyor)" : " (adla arama TUTMUYOR)", r);
        }
        return r;
    }
    void* veh = *(void**)ctrl; int slot = slot_of(veh);
    if (!g_race.effects || slot < 0) return o_effect(ctrl, name, params);
    if (!owned_here(slot)) { g_eff_supp++; return 0; }    // another PC's car: its owner decides, then tells us
    uint32_t r = o_effect(ctrl, name, params);
    // The return value is not "applied or not": 0 for instant effects, a handle for lasting ones. So always report.
    // The same effect on the same target (e.g. the hula hoop) repeats quickly: do not resend a repeat within 150 ms.
    static uint32_t last_hash[12]; static uint64_t last_ms[12];
    uint32_t hh = 0x811c9dc5u; for (const char* p = name; *p; p++) hh = (hh ^ (uint8_t)*p) * 0x1000193u;
    uint64_t nowm = now_ms(); int dup = slot < 12 && last_hash[slot] == hh && nowm - last_ms[slot] < 150;
    if (slot < 12) { last_hash[slot] = hh; last_ms[slot] = nowm; }
    if (g_released && !dup) {
        const uint8_t* pr = (const uint8_t*)params; WireEffect e; memset(&e, 0, sizeof e);
        int src = slot_of(*(void**)(pr + EP_SOURCE)); void* pu = *(void**)(pr + EP_POWERUP);
        e.tslot = (uint8_t)slot; e.sslot = (uint8_t)(src < 0 ? 255 : src); e.level = *(const int32_t*)(pr + EP_LEVEL);
        memcpy(&e.flag, pr + EP_FLAG, 8);
        strncpy(e.name, name, 47); if (pu) strncpy(e.powerup, sstr_(pu), 47);
        net_send_effect(g_race.race_id, &e); g_eff_sent++;
        if (g_eff_log < 60) { g_eff_log++; LOG("effect (local, sent): %s -> slot %d, source %d, power-up '%s'", name, slot, src, e.powerup); }
    }
    return r;
}
static void fill_snap(WireSnap* w, int slot, void* veh) {
    void* a = veh_actor(veh); PxT t;
    px_get_pose(a, &t); memcpy(w->q, t.q, 16); memcpy(w->p, t.p, 12); px_get_lin(a, w->v); px_get_ang(a, w->w);
    memcpy(w->ctl, (char*)veh + VEH_CTL, 16); w->slot = (uint8_t)slot; w->t = (float)clock_s();
}
static void online_human(void* veh, float dt) {
    (void)dt;
    // On restart the cars are destroyed and recreated (without leaving Action mode): drop the old pointers.
    static void* last_hveh;
    if (veh != last_hveh) { if (last_hveh) LOG("online: cars recreated (restart), old pointers cleared"); memset(g_slotveh, 0, sizeof g_slotveh); last_hveh = veh; }
    int slot = slot_of(veh);
    if (slot >= 0) g_slotveh[slot] = veh;
    if (slot != g_my_slot && !g_warned_slot) { g_warned_slot = 1; LOG("WARNING: human car slot %d, expected %d", slot, g_my_slot); }
    double wall = now_ms() / 1000.0;
    if (wall - g_last_state > 0.5) { g_last_state = wall; net_set_local(g_name, &g_car, g_hold ? PS_WAITING : PS_RACING); }
    if (g_released && g_race.effects) replay_effects();
}
// State is sent every tick (the CinematicActive condition is written every tick). Separate from the human input: when a car
// finishes, the game replaces its input with a PostGameDrive controller (0x14fe50) and the human input is no longer called.
static void send_tick(void) {
    if (!g_race_on || !g_released) return;
    double t = clock_s();
    if (t - g_last_send < 1.0/30 - 1e-4) return;
    g_last_send = t;
    WireSnap w[12]; int n = 0;
    for (int i = 0; i < g_race.nslot && i < 12; i++) if (owned_here(i) && g_slotveh[i] && veh_actor(g_slotveh[i])) fill_snap(&w[n++], i, g_slotveh[i]);
    if (n) net_send_snaps(g_race.race_id, w, n);
    if (t - g_last_stat >= 5.0) {
        g_last_stat = t; char line[600]; int k = 0;
        k += snprintf(line + k, sizeof line - k, "online t=%.0f effects sent %d suppressed %d remote %d | puppets:", t, g_eff_sent, g_eff_supp, g_eff_rep);
        for (int i = 0; i < 12 && k < 560; i++) if (g_st[i].n) {
            k += snprintf(line + k, sizeof line - k, " #%d avg %.2fm max %.1fm teleports %d age %.0fms;", i, g_st[i].sum_err/g_st[i].n, g_st[i].max_err, g_st[i].snaps, 1000*g_st[i].sum_age/g_st[i].n);
            memset(&g_st[i], 0, sizeof g_st[i]);
        }
        LOG("%s", line);
    }
}
static int online_ai(void* veh, float dt) {
    int slot = slot_of(veh);
    if (slot < 0) return 0;
    g_slotveh[slot] = veh;
    if (owned_here(slot)) return 0;     // a car of this PC (bots and taken-over cars on the host): the AI drives it
    WireSnap w; uint32_t age;
    if (!net_get_snap(slot, &w, &age)) { memset((char*)veh + VEH_CTL, 0, VEH_CTL_SIZE); return 1; }
    Snap s; memset(&s, 0, sizeof s);
    s.t = w.t; memcpy(s.q, w.q, 16); memcpy(s.p, w.p, 12); memcpy(s.v, w.v, 12); memcpy(s.w, w.w, 12); memcpy(s.ctl, w.ctl, 16);
    repl_apply(veh, &s, clock_s(), dt, &g_rc, &g_st[slot]);
    return 1;
}
static void MSABI hk_human(void* self, float dt) {
    o_human(self, dt);
    void* veh = *(void**)((char*)self + INPUT_VEH);
    if (*(int*)((char*)self + INPUT_PAD) < 0) return;
    if (g_race_on) { online_human(veh, dt); go_poll(); }
    else if (!net_connected()) shadow_human(self, veh, dt);
}
static void MSABI hk_ai(void* self, float dt) {
    void* veh = *(void**)((char*)self + INPUT_VEH);
    if (g_race_on) { if (online_ai(veh, dt)) return; }
    else if (!net_connected() && shadow_ai(self, veh, dt)) return;
    o_ai(self, dt);
}
static void process_racecmds(void) {
    RaceCmd c;
    if (net_role() == ROLE_HOST) { char nm[32]; if (net_take_join(nm, sizeof nm)) online_toast("%s joined the lobby and will race next time", nm[0] ? nm : "A player"); }
    while (net_take_racecmd(&c)) {
        if (!g_race_on) continue;
        if (net_role() == ROLE_HOST && c.cmd == RC_LEFT) {          // a client left the race: its cars go to the host
            if (c.race_id && c.race_id != g_race.race_id) { /* old race */ }
            net_race_peer_left(c.owner);
            for (int i = 0; i < g_race.nslot; i++) if (g_race.owner[i] == c.owner) {
                g_race.owner[i] = 0;
                RaceCmd o; memset(&o, 0, sizeof o); o.cmd = RC_OWNER; o.slot = (uint8_t)i; o.owner = 0; o.race_id = g_race.race_id; net_send_racecmd(&o);
                LOG("online: player %d left the race, slot %d handed to the AI", c.owner, i);
                online_toast("%s left the race - AI took over", g_race.desc.v[i].name);
            }
        } else if (net_role() == ROLE_CLIENT) {
            if (c.race_id != g_race.race_id) { LOG("online: command for another race ignored (%u)", c.race_id); continue; }
            if (c.cmd == RC_OWNER && c.slot < 12) { g_race.owner[c.slot] = c.owner; LOG("online: slot %d now owned by %d", c.slot, c.owner); }
            else if (c.cmd == RC_RESTART) {
                g_race.race_id = c.new_id; net_race_restart(c.new_id); hold_reset(); g_do_restart = 1;
                LOG("online: the host is restarting the race (new race %u)", c.new_id); online_toast("Host restarted the race");
            } else if (c.cmd == RC_QUIT) {
                g_do_frontend = 1; LOG("online: the host left the race, returning to the menu"); online_toast("Host ended the race");
            }
        }
    }
    if (g_do_restart) { g_do_restart = 0; broadcast_event("OnRestartActionGame"); }
    if (g_do_frontend) { g_do_frontend = 0; g_race_on = 0; ui_prepare_frontend(); broadcast_event("OnStartFrontEnd"); }
}
// UI actions: Retry and Quit in the pause/results menu, and tip popups
static void* MSABI hk_restart_ui(void* self, void* ret, const void* params) {
    if (g_race_on) {
        if (net_role() == ROLE_CLIENT) { online_toast("Only the host can restart an online race"); *(int*)ret = 0; return ret; }
        if (net_role() == ROLE_HOST) {
            RaceCmd c; memset(&c, 0, sizeof c); c.cmd = RC_RESTART; c.race_id = g_race.race_id;
            static uint32_t ctr; c.new_id = ((uint32_t)now_ms() << 8) | (++ctr & 0xff);
            net_send_racecmd(&c); g_race.race_id = c.new_id; net_race_restart(c.new_id); hold_reset();
            LOG("online: restarting the race for everyone (new race %u)", c.new_id);
        }
    }
    return o_restart_ui(self, ret, params);
}
static void* MSABI hk_frontend_ui(void* self, void* ret, const void* params) {
    if (g_race_on) {
        RaceCmd c; memset(&c, 0, sizeof c); c.race_id = g_race.race_id;
        if (net_role() == ROLE_HOST) { c.cmd = RC_QUIT; net_send_racecmd(&c); LOG("online: the host is leaving the race, everyone to the menu"); }
        else if (net_role() == ROLE_CLIENT) { c.cmd = RC_LEFT; net_send_racecmd(&c); LOG("online: leaving the race"); }
        g_race_on = 0;
    }
    return ui_frontend_wrap(self, ret, params, o_frontend_ui);
}
static void* MSABI hk_tip_ui(void* self, void* ret, const void* params) {
    if (g_race_on) { LOG("online: tip popup skipped"); *(int*)ret = 0; return ret; }   // online, a tip popup would pause the countdown
    return o_tip_ui(self, ret, params);
}
static void MSABI hk_setcond(void* fsm, const char* name, uint8_t v) {
    if (g_race_on) {
        if (name == s_skip) v = 0;
        else if (name == s_force) v = 1;
        if (name == s_cine) { process_racecmds(); go_poll(); send_tick(); results_tick(); }
        if (name == s_skip || name == s_force) {}
        else if (name == s_cine && g_hold) {
            if (v) g_seen_cine = 1;
            if (!v && !g_loaded_sent && (g_seen_cine || now_ms() - g_hold_t0 > 3000)) {
                g_loaded_sent = 1;
                if (g_my_id == 0) net_set_host_waiting(g_race.race_id, 1); else net_send_loaded(g_race.race_id);
                LOG("online: intro finished, waiting for the other players");
            }
            int64_t rel = net_take_release(g_race.race_id);
            if (rel && qpc() >= rel) {
                g_hold = 0; g_t0 = g_rel_qpc = rel; g_released = 1; g_go_qpc = rel + (int64_t)(4.2 * qpf());   // countdown 4 s + margin
                LOG("online: start released (measure: common clock %.3f, clock %s)", hclock(), net_clock_ok() ? "senkron" : "not synced");
            }
            else v = 1;
        }
    }
    if (g_race_on && g_released && name == s_start && v && !g_go_done) {
        int64_t left = g_go_qpc - qpc();
        if (g_go_qpc && left > 0 && left < (int64_t)qpf()) {   // the local countdown ended early: hold until the common moment
            if (!g_go_pending) { g_go_pending = 1; g_go_fsm = fsm; LOG("measure: local countdown ended at release+%.3f s, waiting %.0f ms", since_rel(), left * 1000.0 / qpf()); }
            return;
        }
        g_go_done = 1; LOG("measure: GO (local), release+%.3f s, common clock %.3f", since_rel(), hclock());
    }
    if (g_race_on && g_released && g_is_race && name == s_end) {
        if (g_res_applied) v = 1;                       // final results arrived: everyone ends together (anyone unfinished gets DNF)
        else if (v && !g_end_local) {
            if (!g_end_wait) { g_end_wait = 1; g_end_wait_ms = now_ms(); LOG("online: I finished, waiting for the other players (common clock %.3f)", hclock()); }
            if (!net_connected() || now_ms() - g_end_wait_ms > END_TIMEOUT_MS + 15000) { g_end_local = 1; LOG("online: no result from the host, using local results"); }
            else v = 0;
        }
    }
    o_setcond(fsm, name, v);
}
void online_status(OnlineStatus* st) {
    memset(st, 0, sizeof *st);
    if (g_toast[0] && now_ms() - g_toast_ms < 5000) { strcpy(st->toast, g_toast); st->toast_age_ms = (uint32_t)(now_ms() - g_toast_ms); }
    st->in_race = g_race_on; st->holding = g_hold; st->my_slot = g_my_slot; st->nslot = g_race.nslot; st->race_id = g_race.race_id; st->clock = (float)clock_s();
    if (g_race_on && g_hold) snprintf(st->note, sizeof st->note, g_loaded_sent ? "Waiting for the other players..." : "Get ready...");
    else if (g_race_on && g_end_wait && !g_res_applied && !g_end_local) {
        int left = g_first_fin_ms ? (int)((END_TIMEOUT_MS - (int64_t)(now_ms() - g_first_fin_ms)) / 1000) : -1;
        if (net_role() == ROLE_HOST && left >= 0) snprintf(st->note, sizeof st->note, "Waiting for the other players to finish... %d", left);
        else snprintf(st->note, sizeof st->note, "Waiting for the other players to finish...");
    }
}
void online_install(uint8_t* base) {
    g_base = base;
    s_cine = (const char*)(base + STR_CINEMATIC); s_skip = (const char*)(base + STR_SKIPINTRO); s_force = (const char*)(base + STR_FORCEINTRO);
    s_start = (const char*)(base + STR_STARTGAME); s_end = (const char*)(base + STR_ENDGAME);
    int strok = !strcmp(s_cine, "CinematicActive") && !strcmp(s_skip, "SkipIntro") && !strcmp(s_force, "ForceIntro")
             && !strcmp(s_start, "GameFlowStartGame") && !strcmp(s_end, "GameFlowEndGame");
    void** vh = rtti_vtable(base, "VuVehicleInputHumanGame");
    void** va = rtti_vtable(base, "VuVehicleInputAiGame");
    if (!vh || !va || !strok) { LOG("ERROR: online hooks not installed (vtable %p %p, strings %d)", (void*)vh, (void*)va, strok); return; }
    o_human = (fn_update)vt_hook(vh, INPUT_SLOT_UPDATE, (void*)hk_human);
    o_ai = (fn_update)vt_hook(va, INPUT_SLOT_UPDATE, (void*)hk_ai);
    static const uint8_t sig[SETCOND_LEN] = SETCOND_SIG;
    o_setcond = (fn_setcond)inline_hook(base + FN_SETCOND, SETCOND_LEN, sig, (void*)hk_setcond);
    LOG(o_setcond ? "online hooks installed" : "ERROR: condition hook not installed (signature mismatch)");
    static const uint8_t esig[APPLYEFFECT_LEN] = APPLYEFFECT_SIG;
    o_effect = (fn_effect)inline_hook(base + FN_APPLYEFFECT, APPLYEFFECT_LEN, esig, (void*)hk_effect);
    LOG(o_effect ? "effect hook installed" : "ERROR: effect hook not installed (signature mismatch)");
    void** vr = rtti_vtable(base, "VuRestartActionGameUIAction");
    void** vfe = rtti_vtable(base, "VuStartFrontEndUIAction");
    void** vt = rtti_vtable(base, "VuShowTipUIAction");
    if (vr && vfe && vt) {
        o_restart_ui = (fn_uiexec)vt_hook(vr, UIACT_SLOT_EXEC, (void*)hk_restart_ui);
        o_frontend_ui = (fn_uiexec)vt_hook(vfe, UIACT_SLOT_EXEC, (void*)hk_frontend_ui);
        o_tip_ui = (fn_uiexec)vt_hook(vt, UIACT_SLOT_EXEC, (void*)hk_tip_ui);
        LOG("lifecycle hooks installed");
    } else LOG("ERROR: lifecycle hooks not installed");
}
