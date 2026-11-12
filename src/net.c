// ENet-based session layer. Only the thread in this file touches ENet.
#include <enet/enet.h>
#include <windows.h>
#include <iphlpapi.h>
#include <stdio.h>
#include <stdlib.h>
#include "core.h"
#include "session.h"
#include "steamlink.h"
enum { M_HELLO = 1, M_WELCOME, M_REJECT, M_LOBBY, M_READY, M_CAR, M_STATE, M_CHAT, M_LOG, M_START, M_LOADED, M_GO, M_SNAP, M_EFFECT, M_RACECMD, M_TSYNC, M_TSYNCR, M_FINISH, M_RESULT };
#pragma pack(push, 1)
typedef struct { uint8_t t; uint32_t proto, exe_ts, descsize; char name[32]; CarSel car; uint8_t state; } MHello;
typedef struct { uint8_t t, id; } MWelcome;
typedef struct { uint8_t used, id, ready, state; char name[32]; CarSel car; int16_t ping; } LobbyEnt;
typedef struct { uint8_t t, bots, equal, effects, maxp, pad[3]; int32_t laps; char track[48], setting[48]; LobbySlot slot[MAXP]; LobbyEnt p[MAXP]; } MLobby;
typedef struct { uint8_t t, v; } MByte;
typedef struct { uint8_t t; char name[32]; CarSel car; } MCar;
typedef struct { uint8_t t; uint32_t race_id; } MLoaded;
typedef struct { uint8_t t; uint32_t race_id; uint16_t delay_ms; int64_t go_us; } MGo;   // go_us: exact moment on the host's clock
typedef struct { uint8_t t; int64_t t0; } MTsync;             // client -> host: send time (client clock, us)
typedef struct { uint8_t t; int64_t t0, t1; } MTsyncR;        // host -> client: t0 echoed + receive time (host clock)
typedef struct { uint8_t t; uint32_t race_id; uint8_t slot; double time; } MFinish;
typedef struct { uint8_t t; RaceResult r; } MResult;
typedef struct { uint8_t t; uint32_t race_id; uint8_t n; } MSnapHdr;
typedef struct { uint8_t t; uint32_t race_id; WireEffect e; } MEffect;
typedef struct { uint8_t t; RaceCmd c; } MRaceCmd;
#pragma pack(pop)
static CRITICAL_SECTION g_cs; static int g_init;
#define LOCK EnterCriticalSection(&g_cs)
#define UNLOCK LeaveCriticalSection(&g_cs)
static Lobby g_l;
static ENetHost* g_host; static ENetPeer* g_peer[MAXP];   // host: id -> peer; client: [0] = host
static uint64_t g_connect_start;
// komutlar
static volatile int c_host, c_join, c_leave, c_selftest, c_settings, c_invite; static int c_port; static char c_addr[64];
static int c_equal = 0, c_effects = 1, c_laps; static char c_track[48], c_setting[48];
static WireEffect g_eff[128]; static int g_eh, g_et;
static RaceCmd g_rc[32]; static int g_rch, g_rct;
// outgoing queue
typedef struct { ENetPacket* pk; int target; uint8_t ch; } Out;   // target: -1 everyone, -2 host, id
static Out g_out[512]; static int g_oh, g_ot;
// race
static OnlineRace g_start; static int g_start_new;
static uint32_t g_race_id; static uint8_t g_loaded[MAXP], g_in_race[MAXP]; static int g_host_waiting; static uint64_t g_race_start_ms;
static volatile int64_t g_release_qpc; static uint32_t g_release_race;
static WireSnap g_snap[12]; static uint32_t g_snap_ms[12]; static uint8_t g_snap_has[12];
// results: the host collects finish reports and sends the final table
static uint8_t g_fin_has[12], g_owner[12]; static double g_fin_t[12]; static RaceResult g_res; static int g_res_new;
// common clock (client): host - client offset from the lowest round-trip sample
static int64_t g_clk_rtt[16], g_clk_off[16]; static int g_clk_n; static volatile int64_t g_off_us; static volatile int g_clk_ok; static uint64_t g_clk_last;
// log ring
static char g_ring[400][200]; static int g_rh; static uint32_t g_rserial;
static uint32_t g_log_tokens = 30, g_log_last;
static int64_t qpc(void) { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
static int64_t qpf(void) { static LARGE_INTEGER f; if (!f.QuadPart) QueryPerformanceFrequency(&f); return f.QuadPart; }
static int64_t now_us(void) { int64_t c = qpc(), f = qpf(); return c / f * 1000000 + c % f * 1000000 / f; }
static void clk_reset(void) { g_clk_n = 0; g_clk_ok = 0; g_off_us = 0; g_clk_last = 0; }
static void results_reset(void) { memset(g_fin_has, 0, sizeof g_fin_has); g_res_new = 0; }   // under the lock
int64_t net_host_now_us(void) { return g_l.role == ROLE_CLIENT ? now_us() + g_off_us : now_us(); }
int net_clock_ok(void) { return g_l.role != ROLE_CLIENT || g_clk_ok; }
int64_t net_clock_offset_us(void) { return g_l.role == ROLE_CLIENT ? g_off_us : 0; }
static void slots_derive(void) {   // under the lock
    int b = 0, m = 0; for (int i = 0; i < MAXP; i++) { if (g_l.slot[i].kind == SL_BOT) b++; if (g_l.slot[i].kind != SL_CLOSED) m++; }
    g_l.bots = (uint8_t)b; g_l.maxp = (uint8_t)m;
}
static void slots_default(void) {  // under the lock: 1 = host, 2-4 open, 5-8 bot (classic 8-car race)
    memset(g_l.slot, 0, sizeof g_l.slot);
    g_l.slot[0].kind = SL_PLAYER; g_l.slot[0].conn = 0;
    for (int i = 1; i < MAXP; i++) g_l.slot[i].kind = i < 4 ? SL_OPEN : SL_BOT;
    slots_derive();
}
static void chat_push(const char* line) {   // does not lock; the caller must not hold the lock
    LOCK; memmove(g_l.chat[0], g_l.chat[1], sizeof g_l.chat[0] * 5); snprintf(g_l.chat[5], sizeof g_l.chat[5], "%s", line); g_l.chat_serial++; UNLOCK;
}
static void ensure(void) { if (!g_init) { InitializeCriticalSection(&g_cs); g_init = 1; g_l.effects = 1; slots_default(); g_l.port = NET_PORT_DEFAULT; } }
static void setstatus(const char* fmt, ...) { va_list ap; va_start(ap, fmt); vsnprintf(g_l.status, sizeof g_l.status, fmt, ap); va_end(ap); }
static void seterror(const char* fmt, ...) { va_list ap; va_start(ap, fmt); vsnprintf(g_l.error, sizeof g_l.error, fmt, ap); va_end(ap); }
// ---------- gunluk ----------
void log_ring_push(const char* line) {
    ensure(); LOCK;
    strncpy(g_ring[g_rh], line, 199); g_ring[g_rh][199] = 0; g_rh = (g_rh + 1) % 400; g_rserial++;
    UNLOCK;
}
int log_ring_copy(char (*lines)[200], int max, uint32_t* serial) {
    ensure(); LOCK;
    int n = g_rserial < 400 ? (int)g_rserial : 400; if (n > max) n = max;
    for (int i = 0; i < n; i++) memcpy(lines[i], g_ring[(g_rh - n + i + 400) % 400], 200);
    if (serial) *serial = g_rserial;
    UNLOCK; return n;
}
static void enqueue(const void* data, size_t len, int target, uint8_t ch, int reliable) {
    ENetPacket* pk = enet_packet_create(data, len, reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
    if (!pk) return;
    LOCK;
    int nh = (g_oh + 1) % 512;
    if (nh == g_ot) { UNLOCK; enet_packet_destroy(pk); return; }
    g_out[g_oh].pk = pk; g_out[g_oh].target = target; g_out[g_oh].ch = ch; g_oh = nh;
    UNLOCK;
}
static void send_text(uint8_t type, const char* text, int target) {
    char buf[260]; buf[0] = (char)type; strncpy(buf + 1, text, 255); buf[256] = 0;
    enqueue(buf, 2 + strlen(buf + 1), target, 0, 1);
}
void net_forward_log(const char* line) {
    ensure();
    if (!(g_l.cs == CS_CONNECTED || (g_l.cs == CS_HOSTING))) return;
    uint32_t now = (uint32_t)now_ms();
    if (now - g_log_last > 1000) { g_log_tokens = 30; g_log_last = now; }
    if (!g_log_tokens) return; g_log_tokens--;
    send_text(M_LOG, line, g_l.role == ROLE_HOST ? -1 : -2);
}
// ---------- komutlar ----------
void net_cmd_host(int port) { ensure(); LOCK; c_port = port; c_host = 1; UNLOCK; }
void net_cmd_join(const char* host, int port) { ensure(); LOCK; strncpy(c_addr, host, 63); c_addr[63] = 0; c_port = port; c_join = 1; UNLOCK; }
void net_cmd_leave(void) { ensure(); c_leave = 1; }
void net_cmd_selftest(void) { ensure(); c_selftest = 1; }
void net_cmd_invite(void) { ensure(); c_invite = 1; }
void net_cmd_settings(int bots, int equal, int effects, int maxp) { (void)bots; (void)maxp; ensure(); LOCK; c_equal = equal; c_effects = effects; c_settings = 1; UNLOCK; }
void net_cmd_track(const char* track, const char* setting, int laps) { ensure(); LOCK; snprintf(c_track, 48, "%s", track ? track : ""); snprintf(c_setting, 48, "%s", setting ? setting : ""); c_laps = laps; c_settings = 1; UNLOCK; }
void net_cmd_slot(int i) {
    ensure(); if (i < 0 || i >= MAXP) return;
    LOCK; LobbySlot* s = &g_l.slot[i];
    if (g_l.role == ROLE_HOST && s->kind != SL_PLAYER) { s->kind = s->kind == SL_OPEN ? SL_BOT : s->kind == SL_BOT ? SL_CLOSED : SL_OPEN; slots_derive(); c_settings = 1; }
    UNLOCK;
}
void net_cmd_ready(int ready) {
    ensure(); LOCK; PeerInfo* me = &g_l.p[g_l.my_id]; me->ready = (uint8_t)ready; int role = g_l.role; UNLOCK;
    if (role == ROLE_CLIENT) { MByte m = { M_READY, (uint8_t)ready }; enqueue(&m, sizeof m, -2, 0, 1); }
}
void net_cmd_chat(const char* text) {
    char line[240]; ensure();
    LOCK; snprintf(line, sizeof line, "%s: %s", g_l.p[g_l.my_id].name[0] ? g_l.p[g_l.my_id].name : "ben", text); int role = g_l.role; UNLOCK;
    logf_raw(line); chat_push(line);
    if (role) send_text(M_CHAT, line, role == ROLE_HOST ? -1 : -2);
}
void net_set_local(const char* name, const CarSel* car, int state) {
    ensure(); LOCK;
    PeerInfo* me = &g_l.p[g_l.my_id];
    int changed = strcmp(me->name, name) || memcmp(&me->car, car, sizeof *car);
    int schanged = me->state != state;
    strncpy(me->name, name, 31); me->name[31] = 0; me->car = *car; me->state = (uint8_t)state;
    int role = g_l.role, cs = g_l.cs; UNLOCK;
    if (role == ROLE_CLIENT && cs == CS_CONNECTED) {
        if (changed) { MCar m; m.t = M_CAR; memset(m.name, 0, 32); strncpy(m.name, name, 31); m.car = *car; enqueue(&m, sizeof m, -2, 0, 1); }
        if (schanged) { MByte m = { M_STATE, (uint8_t)state }; enqueue(&m, sizeof m, -2, 0, 1); }
    }
}
void net_lobby_copy(Lobby* out) { ensure(); LOCK; *out = g_l; UNLOCK; }
int net_role(void) { ensure(); return g_l.cs == CS_HOSTING ? ROLE_HOST : g_l.cs == CS_CONNECTED ? ROLE_CLIENT : ROLE_NONE; }
uint8_t net_my_id(void) { return g_l.my_id; }
int net_connected(void) { return net_role() != ROLE_NONE; }
void net_host_ready_peers(uint8_t* ids, int* n, PeerInfo* infos) {
    ensure(); LOCK; *n = 0;
    for (int i = 1; i < MAXP; i++) if (g_l.p[i].used && g_l.p[i].ready && g_l.p[i].state == PS_MENU) { ids[*n] = (uint8_t)i; infos[*n] = g_l.p[i]; (*n)++; }
    UNLOCK;
}
// ---------- race ----------
void net_send_start(const OnlineRace* r) {
    ensure();
    static uint8_t buf[1 + sizeof(OnlineRace)]; buf[0] = M_START; memcpy(buf + 1, r, sizeof *r);
    LOCK; g_race_id = r->race_id; memset(g_loaded, 0, sizeof g_loaded); memset(g_in_race, 0, sizeof g_in_race);
    for (int i = 0; i < r->nslot; i++) g_in_race[r->owner[i]] = 1;
    memcpy(g_owner, r->owner, sizeof g_owner); results_reset();
    g_host_waiting = 0; g_race_start_ms = now_ms(); g_release_qpc = 0; memset(g_snap_has, 0, sizeof g_snap_has); g_et = g_eh; UNLOCK;
    for (int i = 1; i < MAXP; i++) if (g_in_race[i]) enqueue(buf, sizeof buf, i, 0, 1);
}
int net_take_start(OnlineRace* out) { ensure(); LOCK; int r = g_start_new; if (r) { *out = g_start; g_start_new = 0; } UNLOCK; return r; }
void net_send_loaded(uint32_t race_id) { MLoaded m = { M_LOADED, race_id }; enqueue(&m, sizeof m, -2, 0, 1); }
void net_set_host_waiting(uint32_t race_id, int waiting) { ensure(); LOCK; if (race_id == g_race_id) g_host_waiting = waiting; UNLOCK; }
int64_t net_take_release(uint32_t race_id) { return (g_release_race == race_id) ? g_release_qpc : 0; }
void net_race_reset(uint32_t race_id) {
    ensure(); LOCK; g_race_id = race_id; memset(g_snap_has, 0, sizeof g_snap_has); g_release_qpc = 0; g_release_race = 0; results_reset(); UNLOCK;
}
void net_send_snaps(uint32_t race_id, const WireSnap* s, int n) {
    if (n <= 0 || n > 12) return;
    uint8_t buf[sizeof(MSnapHdr) + 12*sizeof(WireSnap)];
    MSnapHdr h = { M_SNAP, race_id, (uint8_t)n }; memcpy(buf, &h, sizeof h); memcpy(buf + sizeof h, s, n*sizeof *s);
    enqueue(buf, sizeof h + n*sizeof *s, g_l.role == ROLE_HOST ? -1 : -2, 1, 0);
}
void net_send_effect(uint32_t race_id, const WireEffect* e) {
    MEffect m; m.t = M_EFFECT; m.race_id = race_id; m.e = *e;
    enqueue(&m, sizeof m, g_l.role == ROLE_HOST ? -1 : -2, 0, 1);
}
static void store_effect(const uint8_t* d, size_t len) {
    if (len < sizeof(MEffect)) return;
    MEffect m; memcpy(&m, d, sizeof m);
    if (m.race_id != g_race_id) return;
    m.e.name[47] = 0; m.e.powerup[47] = 0;
    LOCK; int nh = (g_eh + 1) % 128; if (nh != g_et) { g_eff[g_eh] = m.e; g_eh = nh; } UNLOCK;
}
int net_take_effect(WireEffect* out) {
    ensure(); LOCK; int r = g_et != g_eh; if (r) { *out = g_eff[g_et]; g_et = (g_et + 1) % 128; } UNLOCK; return r;
}
static void push_racecmd(const RaceCmd* c) { LOCK; int nh = (g_rch + 1) % 32; if (nh != g_rct) { g_rc[g_rch] = *c; g_rch = nh; } UNLOCK; }
void net_send_racecmd(const RaceCmd* c) { MRaceCmd m; m.t = M_RACECMD; m.c = *c; enqueue(&m, sizeof m, g_l.role == ROLE_HOST ? -1 : -2, 0, 1); }
int net_take_racecmd(RaceCmd* out) { ensure(); LOCK; int r = g_rct != g_rch; if (r) { *out = g_rc[g_rct]; g_rct = (g_rct + 1) % 32; } UNLOCK; return r; }
void net_race_restart(uint32_t new_id) {
    ensure(); LOCK;
    g_race_id = new_id; memset(g_loaded, 0, sizeof g_loaded); g_host_waiting = 0; g_race_start_ms = now_ms();
    g_release_qpc = 0; g_release_race = 0; memset(g_snap_has, 0, sizeof g_snap_has); g_et = g_eh; results_reset();
    UNLOCK;
}
void net_report_finish(uint32_t race_id, int slot, double t) {
    if (slot < 0 || slot >= 12) return;
    ensure();
    if (g_l.role == ROLE_HOST) { LOCK; if (race_id == g_race_id && !g_fin_has[slot]) { g_fin_has[slot] = 1; g_fin_t[slot] = t; } UNLOCK; }
    else { MFinish m = { M_FINISH, race_id, (uint8_t)slot, t }; enqueue(&m, sizeof m, -2, 0, 1); }
}
int net_finish_table(uint32_t race_id, uint8_t* has, double* t) {
    ensure(); LOCK; int ok = race_id == g_race_id; if (ok) { memcpy(has, g_fin_has, 12); memcpy(t, g_fin_t, sizeof g_fin_t); } UNLOCK; return ok;
}
static uint32_t g_join_seen;
int net_take_join(char* out, int cap) { ensure(); LOCK; int r = g_l.join_serial != g_join_seen; if (r) { g_join_seen = g_l.join_serial; snprintf(out, cap, "%s", g_l.join_name); } UNLOCK; return r; }
void net_send_result(const RaceResult* r) { MResult m; m.t = M_RESULT; m.r = *r; enqueue(&m, sizeof m, -1, 0, 1); }
int net_take_result(uint32_t race_id, RaceResult* out) {
    ensure(); LOCK; int r = g_res_new && g_res.race_id == race_id; if (r) { *out = g_res; g_res_new = 0; } UNLOCK; return r;
}
void net_race_peer_left(uint8_t id) { ensure(); LOCK; if (id < MAXP) g_in_race[id] = 0; UNLOCK; }
int net_get_snap(int slot, WireSnap* out, uint32_t* age_ms) {
    if (slot < 0 || slot >= 12) return 0;
    ensure(); LOCK; int r = g_snap_has[slot]; if (r) { *out = g_snap[slot]; if (age_ms) *age_ms = (uint32_t)now_ms() - g_snap_ms[slot]; } UNLOCK;
    return r;
}
static void store_snaps(const uint8_t* d, size_t len) {
    if (len < sizeof(MSnapHdr)) return;
    MSnapHdr h; memcpy(&h, d, sizeof h);
    if (h.race_id != g_race_id || len < sizeof h + h.n*sizeof(WireSnap)) return;
    const WireSnap* s = (const WireSnap*)(d + sizeof h);
    LOCK;
    for (int i = 0; i < h.n; i++) {
        WireSnap w; memcpy(&w, &s[i], sizeof w);
        if (w.slot >= 12) continue;
        if (g_snap_has[w.slot] && w.t < g_snap[w.slot].t) continue;   // old packet
        g_snap[w.slot] = w; g_snap_ms[w.slot] = (uint32_t)now_ms(); g_snap_has[w.slot] = 1;
    }
    UNLOCK;
}
// ---------- ENet side ----------
static void peer_addr(ENetPeer* p, char* out, int cap) {
    char ip[64] = "?"; enet_address_get_host_ip(&p->address, ip, sizeof ip); snprintf(out, cap, "%s:%u", ip, p->address.port);
}
static void broadcast_lobby(void) {
    MLobby m; memset(&m, 0, sizeof m); m.t = M_LOBBY;
    LOCK;
    m.bots = g_l.bots; m.equal = g_l.equal; m.effects = g_l.effects; m.maxp = g_l.maxp; m.laps = g_l.laps; memcpy(m.track, g_l.track, 48);
    memcpy(m.setting, g_l.setting, 48); memcpy(m.slot, g_l.slot, sizeof m.slot);
    for (int i = 0; i < MAXP; i++) {
        PeerInfo* p = &g_l.p[i]; LobbyEnt* e = &m.p[i];
        e->used = p->used; e->id = (uint8_t)i; e->ready = p->ready; e->state = p->state; e->ping = (int16_t)p->ping;
        memcpy(e->name, p->name, 32); e->car = p->car;
    }
    UNLOCK;
    for (int i = 1; i < MAXP; i++) if (g_peer[i]) { ENetPacket* pk = enet_packet_create(&m, sizeof m, ENET_PACKET_FLAG_RELIABLE); enet_peer_send(g_peer[i], 0, pk); }
}
#define TUN_MTU 1100   // Steam tunnel: every ENet packet must fit in one Steam packet
static int g_sjoin; static uint64_t g_sjoin_ms;   // looking for / entering a Steam lobby (no ENet yet)
static void close_all2(const char* why, int keep_steam);
static void close_all(const char* why) { close_all2(why, 0); }
static void close_all2(const char* why, int keep_steam) {   // keep_steam: keep the Steam tunnel open (while connecting through it)
    if (g_host) {
        for (int i = 0; i < MAXP; i++) if (g_peer[i]) enet_peer_disconnect_now(g_peer[i], 0);
        enet_host_flush(g_host); enet_host_destroy(g_host); g_host = 0;
    }
    memset(g_peer, 0, sizeof g_peer);
    if (!keep_steam) { sl_leave(); g_sjoin = 0; }
    LOCK;
    PeerInfo me = g_l.p[g_l.my_id]; memset(g_l.p, 0, sizeof g_l.p);
    g_l.p[0] = me; g_l.p[0].id = 0; g_l.p[0].used = 1; g_l.p[0].ready = 0;
    g_l.role = ROLE_NONE; g_l.cs = CS_IDLE; g_l.my_id = 0; g_race_id = 0; g_release_qpc = 0; clk_reset(); results_reset();
    g_l.code[0] = 0; g_l.steam_note[0] = 0;
    if (why) setstatus("%s", why);
    UNLOCK;
}
static void do_host(int port) {
    close_all(0);
    ENetAddress a; a.host = ENET_HOST_ANY; a.port = (enet_uint16)port;
    g_host = enet_host_create(&a, MAXP, 2, 0, 0);
    if (g_host) g_host->mtu = TUN_MTU;
    LOCK;
    if (!g_host) {
        seterror("Could not open port %d. Another program (or a second copy of the game) may be using it. Set a different port= in bbr2mod\\online.ini.", port);
        setstatus("Could not create the lobby"); UNLOCK; LOG("ERROR: could not listen on UDP %d", port); return;
    }
    g_l.role = ROLE_HOST; g_l.cs = CS_HOSTING; g_l.my_id = 0; g_l.port = port; g_l.p[0].used = 1; g_l.p[0].id = 0; slots_default();
    g_l.track[0] = g_l.setting[0] = 0; memset(g_l.chat, 0, sizeof g_l.chat);
    g_l.since_ms = (uint32_t)now_ms(); g_l.error[0] = 0;
    setstatus("Hosting a lobby on UDP port %d.", port);
    UNLOCK;
    LOG("host: listening on UDP %d", port);
    char sm[16]; int mode = sl_read_mode(sm, sizeof sm);
    if (mode >= 0) {   // an IP lobby always exists; a Steam lobby (code + invites) is added on top
        char nm[32]; LOCK; snprintf(nm, sizeof nm, "%s", g_l.p[0].name); g_l.steam_net = (uint8_t)mode;
        snprintf(g_l.steam_note, sizeof g_l.steam_note, "Creating a %s lobby...", mode ? "Spacewar" : "Steam"); UNLOCK;
        sl_host(mode, port, MAXP, nm);
    } else LOG("steam=off: IP only");
}
static void do_join_ex(const char* addr, int port, const char* label, int keep_steam) {
    close_all2(0, keep_steam);
    g_host = enet_host_create(NULL, 1, 2, 0, 0);
    if (!g_host) { LOCK; seterror("Could not create a network socket (Winsock)."); UNLOCK; if (keep_steam) sl_leave(); return; }
    g_host->mtu = TUN_MTU;
    ENetAddress a; a.port = (enet_uint16)port;
    if (enet_address_set_host(&a, addr) != 0) {
        LOCK; seterror("Could not resolve '%s'. Type the IP address with numbers, for example 10.147.17.25.", addr); setstatus("Could not connect"); UNLOCK;
        enet_host_destroy(g_host); g_host = 0; return;
    }
    g_peer[0] = enet_host_connect(g_host, &a, 2, NET_PROTO);
    g_connect_start = now_ms();
    char disp[64]; if (label && label[0]) snprintf(disp, sizeof disp, "%s", label); else snprintf(disp, sizeof disp, "%s:%d", addr, port);
    LOCK; g_l.role = ROLE_CLIENT; g_l.cs = CS_CONNECTING; snprintf(g_l.joined_host, sizeof g_l.joined_host, "%s", disp);
    g_l.error[0] = 0; setstatus("Connecting to %s...", disp); UNLOCK;
    LOG("client: connecting to %s:%d (%s)", addr, port, disp);
}
static void do_join(const char* addr, int port) { do_join_ex(addr, port, 0, 0); }
// ---------- Steam: joining by code or invite. First the Steam lobby is found, then ENet connects to the local tunnel ----------
static void steam_join(int net, const char* norm, const char* who) {
    close_all(0);
    sl_join(net, norm); g_sjoin = 1; g_sjoin_ms = now_ms();
    LOCK; g_l.role = ROLE_CLIENT; g_l.cs = CS_CONNECTING; g_l.error[0] = 0;
    snprintf(g_l.joined_host, sizeof g_l.joined_host, "%s", !strncmp(norm, "code ", 5) ? norm + 5 : "a Steam lobby");
    if (who && who[0]) setstatus("Joining %s's lobby on %s...", who, net ? "Spacewar" : "Steam");
    else setstatus("Looking for lobby %s on %s...", g_l.joined_host, net ? "Spacewar" : "Steam");
    UNLOCK;
    LOG("steam: join started (%s, %s)", net ? "Spacewar" : "Steam", norm);
}
static void net_note(const char* s) { LOCK; snprintf(g_l.note, sizeof g_l.note, "%s", s); g_l.note_serial++; UNLOCK; }
static void steam_step(void) {
    if (c_invite) { c_invite = 0; sl_invite(); }
    SlEvent e;
    while (sl_poll(&e)) {
        const char* nn = e.net ? "Spacewar" : "Steam";
        switch (e.type) {
        case SLE_HOSTED:
            LOCK; if (g_l.cs == CS_HOSTING) { snprintf(g_l.code, sizeof g_l.code, "%s", e.a); g_l.steam_net = (uint8_t)e.net; g_l.steam_note[0] = 0; } UNLOCK;
            LOG("steam: lobby code %s (%s)", e.a, nn);
            { char t[100]; snprintf(t, sizeof t, "Lobby code: %s (%s). Friends join with Online > Join Lobby.", e.a, nn); chat_push(t); }
            break;
        case SLE_HOSTFAIL:
            LOCK; g_l.code[0] = 0; snprintf(g_l.steam_note, sizeof g_l.steam_note, "No %s lobby", nn); UNLOCK;
            LOG("steam: could not create the lobby: %s", e.b);
            { char t[300]; snprintf(t, sizeof t, "%s lobby failed: %s", nn, e.b); net_note(t); }
            break;
        case SLE_JOINED:
            if (!g_sjoin) break;
            g_sjoin = 0;
            { char label[64]; snprintf(label, sizeof label, "%s (%s)", e.a[0] ? e.a : e.b, nn);
              LOG("steam: lobby found, host %s; ENet connecting to the 127.0.0.1:%d tunnel", e.b, e.port);
              do_join_ex("127.0.0.1", e.port, label, 1);
              LOCK; snprintf(g_l.code, sizeof g_l.code, "%s", e.a); g_l.steam_net = (uint8_t)e.net; UNLOCK;
              char t[100]; snprintf(t, sizeof t, "Joining lobby %s (%s)...", e.a[0] ? e.a : e.b, nn); chat_push(t); }
            break;
        case SLE_JOINFAIL:
            if (!g_sjoin) break;
            close_all(0); LOCK; seterror("%s", e.b); setstatus("Could not connect"); UNLOCK;
            break;
        case SLE_INVITED:
            LOG("steam: invite/join request (%s, %s)", nn, e.a);
            steam_join(e.net, e.a, e.b); net_note("Joining a friend's Steam lobby...");
            break;
        case SLE_NOTE: net_note(e.b); break;
        }
    }
    if (g_sjoin && now_ms() - g_sjoin_ms > 30000) {
        close_all(0); LOCK; seterror("Steam did not answer in time. Check that Steam is running and try again."); setstatus("Could not connect"); UNLOCK;
    }
}
static void handle_host(ENetEvent* ev) {
    int id = (int)(intptr_t)ev->peer->data;
    if (ev->type == ENET_EVENT_TYPE_CONNECT) {
        char a[48]; peer_addr(ev->peer, a, sizeof a);
        LOG("host: %s connected, waiting for the handshake", a);
        ev->peer->data = (void*)(intptr_t)-1;
        enet_peer_timeout(ev->peer, 32, 8000, 15000);
        return;
    }
    if (ev->type == ENET_EVENT_TYPE_DISCONNECT) {
        if (id > 0 && id < MAXP) {
            LOCK; char nm[32]; memcpy(nm, g_l.p[id].name, 32); memset(&g_l.p[id], 0, sizeof g_l.p[id]);
            for (int i = 0; i < MAXP; i++) if (g_l.slot[i].kind == SL_PLAYER && g_l.slot[i].conn == id) { g_l.slot[i].kind = SL_OPEN; g_l.slot[i].conn = 0; }
            slots_derive(); UNLOCK;
            g_peer[id] = 0; LOG("host: '%s' left (connection %s)", nm, ev->data ? "closed" : "timed out or closed");
            if (g_race_id && g_in_race[id]) { RaceCmd c; memset(&c, 0, sizeof c); c.cmd = RC_LEFT; c.owner = (uint8_t)id; c.race_id = g_race_id; push_racecmd(&c); }
            broadcast_lobby();
        }
        return;
    }
    const uint8_t* d = ev->packet->data; size_t len = ev->packet->dataLength;
    LOCK; g_l.rx_bytes += (uint32_t)len; UNLOCK;
    if (!len) return;
    if (d[0] == M_HELLO && len >= sizeof(MHello)) {
        MHello h; memcpy(&h, d, sizeof h); h.name[31] = 0;
        char a[48]; peer_addr(ev->peer, a, sizeof a);
        char reason[200] = "";
        uint32_t ts = pe_timestamp((const uint8_t*)GetModuleHandleA(0));
        if (h.proto != NET_PROTO) snprintf(reason, sizeof reason, "Different mod versions (you %u, host %u). Use the same version.dll.", h.proto, NET_PROTO);
        else if (h.exe_ts != ts) snprintf(reason, sizeof reason, "Different game versions (exe %08x / %08x).", h.exe_ts, ts);
        else if (h.descsize != sizeof(OnlineRace)) snprintf(reason, sizeof reason, "Different mod builds. Use the same version.dll.");
        int nid = -1;
        if (!reason[0]) { int used = 1; for (int i = 1; i < MAXP; i++) if (g_peer[i]) used++;
            for (int i = 1; i < MAXP; i++) if (!g_peer[i]) { nid = i; break; }
            (void)used; int fs = -1; LOCK; for (int i = 0; i < MAXP; i++) if (g_l.slot[i].kind == SL_OPEN) { fs = i; break; } UNLOCK;
            if (nid < 0 || fs < 0) { nid = -1; snprintf(reason, sizeof reason, "The lobby is full."); } }
        if (reason[0]) {
            char buf[202]; buf[0] = M_REJECT; strncpy(buf + 1, reason, 200); buf[201] = 0;
            enet_peer_send(ev->peer, 0, enet_packet_create(buf, 2 + strlen(buf + 1), ENET_PACKET_FLAG_RELIABLE));
            enet_peer_disconnect_later(ev->peer, 0);
            LOG("host: %s ('%s') refused: %s", a, h.name, reason); return;
        }
        g_peer[nid] = ev->peer; ev->peer->data = (void*)(intptr_t)nid;
        LOCK; for (int i = 0; i < MAXP; i++) if (g_l.slot[i].kind == SL_OPEN) { g_l.slot[i].kind = SL_PLAYER; g_l.slot[i].conn = (uint8_t)nid; g_l.slot[i].local = 0; break; }
        slots_derive();
        PeerInfo* p = &g_l.p[nid]; memset(p, 0, sizeof *p); p->used = 1; p->id = (uint8_t)nid; memcpy(p->name, h.name, 32);
        p->car = h.car; p->state = h.state; strncpy(p->addr, a, 47); p->last_rx_ms = (uint32_t)now_ms(); UNLOCK;
        MWelcome w = { M_WELCOME, (uint8_t)nid };
        enet_peer_send(ev->peer, 0, enet_packet_create(&w, sizeof w, ENET_PACKET_FLAG_RELIABLE));
        LOG("host: '%s' (%s) joined the lobby, id=%d, car=%s", h.name, a, nid, h.car.vehicle);
        if (g_l.p[0].state != PS_MENU) { LOCK; snprintf(g_l.join_name, sizeof g_l.join_name, "%s", h.name); g_l.join_serial++; UNLOCK; }
        broadcast_lobby(); return;
    }
    if (id <= 0 || id >= MAXP) return;
    LOCK; g_l.p[id].last_rx_ms = (uint32_t)now_ms(); UNLOCK;
    switch (d[0]) {
    case M_READY: if (len >= 2) { LOCK; g_l.p[id].ready = d[1]; UNLOCK; broadcast_lobby(); } break;
    case M_STATE: if (len >= 2) { LOCK; g_l.p[id].state = d[1]; UNLOCK; broadcast_lobby(); } break;
    case M_CAR: if (len >= sizeof(MCar)) { MCar m; memcpy(&m, d, sizeof m); m.name[31] = 0; LOCK; memcpy(g_l.p[id].name, m.name, 32); g_l.p[id].car = m.car; UNLOCK; broadcast_lobby(); } break;
    case M_CHAT: {
        char t[256]; size_t n = len - 1 < 255 ? len - 1 : 255; memcpy(t, d + 1, n); t[n] = 0; logf_raw(t); chat_push(t);
        for (int i = 1; i < MAXP; i++) if (g_peer[i] && i != id) enet_peer_send(g_peer[i], 0, enet_packet_create(d, len, ENET_PACKET_FLAG_RELIABLE));
        break; }
    case M_LOG: {
        char t[256], nm[32]; size_t n = len - 1 < 255 ? len - 1 : 255; memcpy(t, d + 1, n); t[n] = 0;
        LOCK; memcpy(nm, g_l.p[id].name, 32); UNLOCK; logf_remote(nm, t); break; }
    case M_LOADED: if (len >= sizeof(MLoaded)) { MLoaded m; memcpy(&m, d, sizeof m);
        if (m.race_id == g_race_id) { g_loaded[id] = 1; LOG("host: player %d loaded, waiting for the start", id); } } break;
    case M_SNAP:
        store_snaps(d, len);
        for (int i = 1; i < MAXP; i++) if (g_peer[i] && i != id) enet_peer_send(g_peer[i], 1, enet_packet_create(d, len, 0));
        break;
    case M_EFFECT:
        store_effect(d, len);
        for (int i = 1; i < MAXP; i++) if (g_peer[i] && i != id) enet_peer_send(g_peer[i], 0, enet_packet_create(d, len, ENET_PACKET_FLAG_RELIABLE));
        break;
    case M_RACECMD: if (len >= sizeof(MRaceCmd)) { MRaceCmd m; memcpy(&m, d, sizeof m);
        if (m.c.cmd == RC_LEFT) { m.c.owner = (uint8_t)id; push_racecmd(&m.c); } } break;
    case M_TSYNC: if (len >= sizeof(MTsync)) { MTsync m; memcpy(&m, d, sizeof m);
        MTsyncR r = { M_TSYNCR, m.t0, now_us() };
        enet_peer_send(ev->peer, 1, enet_packet_create(&r, sizeof r, ENET_PACKET_FLAG_UNSEQUENCED)); enet_host_flush(g_host); } break;
    case M_FINISH: if (len >= sizeof(MFinish)) { MFinish m; memcpy(&m, d, sizeof m);
        if (m.race_id == g_race_id && m.slot < 12 && g_owner[m.slot] == id) {
            LOCK; int nw = !g_fin_has[m.slot]; if (nw) { g_fin_has[m.slot] = 1; g_fin_t[m.slot] = m.time; } UNLOCK;
            if (nw) LOG("host: slot %d finished, %.3f s (player %d)", m.slot, m.time, id);
        } } break;
    }
}
static void handle_client(ENetEvent* ev) {
    if (ev->type == ENET_EVENT_TYPE_CONNECT) {
        MHello h; memset(&h, 0, sizeof h); h.t = M_HELLO; h.proto = NET_PROTO; h.exe_ts = pe_timestamp((const uint8_t*)GetModuleHandleA(0));
        h.descsize = sizeof(OnlineRace);
        LOCK; memcpy(h.name, g_l.p[g_l.my_id].name, 32); h.car = g_l.p[g_l.my_id].car; h.state = g_l.p[g_l.my_id].state; UNLOCK;
        enet_peer_send(ev->peer, 0, enet_packet_create(&h, sizeof h, ENET_PACKET_FLAG_RELIABLE));
        enet_peer_timeout(ev->peer, 32, 8000, 15000);
        LOG("client: UDP connection up (%llu ms), handshake sent", (unsigned long long)(now_ms() - g_connect_start));
        LOCK; setstatus("Connected, waiting for the host..."); UNLOCK;
        return;
    }
    if (ev->type == ENET_EVENT_TYPE_DISCONNECT) {
        LOCK; int was = g_l.cs; char hostaddr[64]; strcpy(hostaddr, g_l.joined_host); UNLOCK;
        g_peer[0] = 0;
        if (was == CS_CONNECTING) {
            LOCK; if (!g_l.error[0]) seterror(
                "No answer from %s after %llu ms.\n"
                "1) Is the host in an online lobby, on the same port?\n"
                "2) Is this the host's ZeroTier IP (shown in their lobby)?\n"
                "3) Are you both joined and authorized in the same ZeroTier network?\n"
                "4) The host's Windows Firewall must allow Game_x64.exe (the prompt can hide behind a fullscreen game).\n"
                "5) Does 'ping <host IP>' answer in a command prompt?",
                hostaddr, (unsigned long long)(now_ms() - g_connect_start));
            UNLOCK; LOG("client: could not connect (%s, no answer)", hostaddr);
        } else { LOCK; seterror("Lost the connection to the host (timed out or the host left)."); UNLOCK; LOG("client: connection lost"); }
        close_all("Not connected");
        return;
    }
    const uint8_t* d = ev->packet->data; size_t len = ev->packet->dataLength;
    LOCK; g_l.rx_bytes += (uint32_t)len; g_l.p[0].last_rx_ms = (uint32_t)now_ms(); UNLOCK;
    if (!len) return;
    switch (d[0]) {
    case M_WELCOME: if (len >= 2) {
        clk_reset(); LOCK; PeerInfo me = g_l.p[g_l.my_id]; memset(&g_l.p[g_l.my_id], 0, sizeof me); g_l.my_id = d[1]; me.id = d[1]; me.used = 1; g_l.p[d[1]] = me;
        g_l.cs = CS_CONNECTED; g_l.since_ms = (uint32_t)now_ms(); setstatus("Joined the lobby."); UNLOCK;
        LOG("client: joined the lobby, id=%d", d[1]); } break;
    case M_REJECT: { char t[202]; size_t n = len - 1 < 200 ? len - 1 : 200; memcpy(t, d + 1, n); t[n] = 0;
        LOCK; seterror("The host refused the connection: %s", t); UNLOCK; LOG("client: refused: %s", t); break; }
    case M_LOBBY: if (len >= sizeof(MLobby)) {
        MLobby m; memcpy(&m, d, sizeof m);
        LOCK; g_l.bots = m.bots; g_l.equal = m.equal; g_l.effects = m.effects; g_l.maxp = m.maxp; g_l.laps = m.laps; memcpy(g_l.track, m.track, 48); g_l.track[47] = 0;
        memcpy(g_l.setting, m.setting, 48); g_l.setting[47] = 0; memcpy(g_l.slot, m.slot, sizeof g_l.slot);
        PeerInfo me = g_l.p[g_l.my_id];
        for (int i = 0; i < MAXP; i++) {
            PeerInfo* p = &g_l.p[i]; LobbyEnt* e = &m.p[i]; CarSel car = p->car;
            memset(p, 0, sizeof *p); p->used = e->used; p->id = e->id; p->ready = e->ready; p->state = e->state; p->ping = e->ping;
            memcpy(p->name, e->name, 32); p->name[31] = 0; car = e->car; p->car = car;
        }
        if (g_l.p[g_l.my_id].used) { g_l.p[g_l.my_id].car = me.car; }
        UNLOCK; } break;
    case M_CHAT: { char t[256]; size_t n = len - 1 < 255 ? len - 1 : 255; memcpy(t, d + 1, n); t[n] = 0; logf_raw(t); chat_push(t); break; }
    case M_LOG: { char t[256]; size_t n = len - 1 < 255 ? len - 1 : 255; memcpy(t, d + 1, n); t[n] = 0; logf_remote("host", t); break; }
    case M_START: if (len >= 1 + sizeof(OnlineRace)) {
        LOCK; memcpy(&g_start, d + 1, sizeof g_start); g_start_new = 1; g_race_id = g_start.race_id; memset(g_snap_has, 0, sizeof g_snap_has); g_et = g_eh;
        g_release_qpc = 0; g_release_race = 0; results_reset(); UNLOCK;
        LOG("client: race recipe received (race %u, %d cars, track %s)", g_start.race_id, g_start.nslot, g_start.desc.track); } break;
    case M_GO: if (len >= sizeof(MGo)) { MGo m; memcpy(&m, d, sizeof m);
        if (m.race_id == g_race_id) {
            int64_t dt = g_clk_ok ? m.go_us - g_off_us - now_us() : -1;
            if (g_clk_ok && dt >= 0 && dt < 3000000) {
                g_release_qpc = qpc() + dt * qpf() / 1000000;
                LOG("client: start signal (common clock), in %lld ms, old method %u ms", (long long)(dt / 1000), m.delay_ms);
            } else {
                g_release_qpc = qpc() + (int64_t)m.delay_ms * qpf() / 1000;
                LOG("client: start signal, in %u ms (common clock %s)", m.delay_ms, g_clk_ok ? "out of range" : "not ready");
            }
            g_release_race = m.race_id; } } break;
    case M_TSYNCR: if (len >= sizeof(MTsyncR)) { MTsyncR m; memcpy(&m, d, sizeof m);
        int64_t t2 = now_us(), rtt = t2 - m.t0;
        if (rtt < 0 || rtt > 2000000) break;
        int k = g_clk_n % 16; g_clk_rtt[k] = rtt; g_clk_off[k] = m.t1 - (m.t0 + t2) / 2; g_clk_n++;
        int n = g_clk_n < 16 ? g_clk_n : 16, b = 0; for (int i = 1; i < n; i++) if (g_clk_rtt[i] < g_clk_rtt[b]) b = i;
        g_off_us = g_clk_off[b];
        if (g_clk_n == 4) { g_clk_ok = 1; LOG("common clock ready: host offset %lld us, best round trip %lld us", (long long)g_off_us, (long long)g_clk_rtt[b]); }
        break; }
    case M_FINISH: break;
    case M_RESULT: if (len >= sizeof(MResult)) { MResult m; memcpy(&m, d, sizeof m);
        if (m.r.race_id == g_race_id) { LOCK; g_res = m.r; g_res_new = 1; UNLOCK; LOG("client: final results table received from the host"); } } break;
    case M_SNAP: store_snaps(d, len); break;
    case M_EFFECT: store_effect(d, len); break;
    case M_RACECMD: if (len >= sizeof(MRaceCmd)) { MRaceCmd m; memcpy(&m, d, sizeof m); push_racecmd(&m.c); } break;
    }
}
static void host_periodic(void) {
    static uint64_t last; uint64_t now = now_ms();
    if (now - last < 1000) return; last = now;
    LOCK;
    for (int i = 1; i < MAXP; i++) if (g_peer[i] && g_l.p[i].used) {
        g_l.p[i].ping = (int)g_peer[i]->roundTripTime; g_l.p[i].pingvar = (int)g_peer[i]->roundTripTimeVariance;
        g_l.p[i].loss = g_peer[i]->packetLoss / (float)ENET_PEER_PACKET_LOSS_SCALE;
    }
    UNLOCK;
    broadcast_lobby();
}
static void host_check_go(void) {
    if (!g_race_id || !g_host_waiting || g_release_qpc) return;
    int all = 1, maxrtt = 0;
    for (int i = 1; i < MAXP; i++) if (g_in_race[i]) {
        if (!g_peer[i]) continue;                         // do not wait for a player who left
        if (!g_loaded[i]) all = 0;
        if ((int)g_peer[i]->roundTripTime > maxrtt) maxrtt = (int)g_peer[i]->roundTripTime;
    }
    int timeout = now_ms() - g_race_start_ms > 45000;
    if (!all && !timeout) return;
    int D = 200 + maxrtt / 2;
    int64_t go_us = now_us() + (int64_t)D * 1000;
    for (int i = 1; i < MAXP; i++) if (g_in_race[i] && g_peer[i]) {
        int dl = D - (int)g_peer[i]->roundTripTime / 2; if (dl < 0) dl = 0;
        MGo m = { M_GO, g_race_id, (uint16_t)dl, go_us };
        enet_peer_send(g_peer[i], 0, enet_packet_create(&m, sizeof m, ENET_PACKET_FLAG_RELIABLE));
    }
    g_release_qpc = qpc() + (int64_t)D * qpf() / 1000; g_release_race = g_race_id;
    LOG("host: %s, starting in %d ms (highest ping %d ms)", timeout ? "45 s passed, starting without waiting" : "everyone ready", D, maxrtt);
}
// self test: connect to the host's own port through 127.0.0.1
static ENetHost* g_test; static ENetPeer* g_testpeer; static uint64_t g_test_t0;
static void selftest_step(void) {
    if (c_selftest) {
        c_selftest = 0;
        if (g_test) { enet_host_destroy(g_test); g_test = 0; }
        if (g_l.cs != CS_HOSTING) { LOCK; g_l.selftest_state = 3; snprintf(g_l.selftest, sizeof g_l.selftest, "Create a lobby first; the test connects to your own port."); UNLOCK; return; }
        g_test = enet_host_create(NULL, 1, 2, 0, 0);
        ENetAddress a; enet_address_set_host(&a, "127.0.0.1"); a.port = (enet_uint16)g_l.port;
        g_testpeer = g_test ? enet_host_connect(g_test, &a, 2, 0) : 0; g_test_t0 = now_ms();
        LOCK; g_l.selftest_state = 1; snprintf(g_l.selftest, sizeof g_l.selftest, "Test: trying 127.0.0.1:%d...", g_l.port); UNLOCK;
    }
    if (!g_test) return;
    ENetEvent ev;
    while (enet_host_service(g_test, &ev, 0) > 0) {
        if (ev.type == ENET_EVENT_TYPE_CONNECT) {
            LOCK; g_l.selftest_state = 2; snprintf(g_l.selftest, sizeof g_l.selftest, "Test passed: the lobby port answers (%llu ms). If friends still cannot join, check ZeroTier and the firewall.", (unsigned long long)(now_ms() - g_test_t0)); UNLOCK;
            enet_peer_disconnect_now(g_testpeer, 0); enet_host_flush(g_test); enet_host_destroy(g_test); g_test = 0; LOG("self test passed"); return;
        }
        if (ev.type == ENET_EVENT_TYPE_RECEIVE) enet_packet_destroy(ev.packet);
    }
    if (now_ms() - g_test_t0 > 4000) {
        LOCK; g_l.selftest_state = 3; snprintf(g_l.selftest, sizeof g_l.selftest, "Test failed: even your own port does not answer. Security software may be blocking the game."); UNLOCK;
        enet_host_destroy(g_test); g_test = 0; LOG("self test failed");
    }
}
static DWORD WINAPI net_thread(void* arg) {
    (void)arg;
    if (enet_initialize() != 0) { LOCK; seterror("Could not start networking (ENet/Winsock)."); UNLOCK; LOG("ERROR: enet_initialize"); return 0; }
    LOG("network layer ready (ENet %d.%d.%d, protocol %d)", ENET_VERSION_MAJOR, ENET_VERSION_MINOR, ENET_VERSION_PATCH, NET_PROTO);
    sl_init();
    uint64_t rate_t = now_ms(); uint32_t tx0 = 0, rx0 = 0;
    for (;;) {
        if (c_leave) { c_leave = 0; if (g_host) LOG("you left the session"); close_all("Not in a lobby"); LOCK; g_l.error[0] = 0; UNLOCK; }
        if (c_host) { c_host = 0; do_host(c_port); }
        if (c_join) { c_join = 0; char a[64]; LOCK; strcpy(a, c_addr); UNLOCK;
            int net = 0; char norm[80]; int k = sl_classify(a, &net, norm, sizeof norm);
            if (k == 1) steam_join(net, norm, 0);
            else if (k == 2) { LOCK; seterror("'%s' is not a valid lobby code. Lobby codes have 5 letters and digits, for example K7Q2M.", a); setstatus("Could not connect"); UNLOCK; }
            else do_join(a, c_port); }
        if (c_settings) { c_settings = 0; LOCK; g_l.equal = (uint8_t)c_equal; g_l.effects = (uint8_t)c_effects; memcpy(g_l.track, c_track, 48); memcpy(g_l.setting, c_setting, 48); g_l.laps = c_laps; slots_derive(); UNLOCK; if (g_l.cs == CS_HOSTING) broadcast_lobby(); }
        steam_step();
        selftest_step();
        if (!g_host) {   // drop the queue when there is no host
            LOCK; while (g_ot != g_oh) { enet_packet_destroy(g_out[g_ot].pk); g_ot = (g_ot + 1) % 512; } UNLOCK;
            Sleep(10); continue;
        }
        ENetEvent ev; int first = 1, k = 0;
        while (k++ < 256 && enet_host_service(g_host, &ev, first ? 1 : 0) > 0) {
            first = 0;
            if (!g_host) break;
            if (g_l.role == ROLE_HOST) handle_host(&ev); else handle_client(&ev);
            if (ev.type == ENET_EVENT_TYPE_RECEIVE) enet_packet_destroy(ev.packet);
            if (!g_host) break;
        }
        if (!g_host) continue;
        // outgoing queue
        for (;;) {
            LOCK; if (g_ot == g_oh) { UNLOCK; break; } Out o = g_out[g_ot]; g_ot = (g_ot + 1) % 512; g_l.tx_bytes += (uint32_t)o.pk->dataLength; UNLOCK;
            int sent = 0;
            if (o.target == -2) { if (g_peer[0] && g_l.role == ROLE_CLIENT) { enet_peer_send(g_peer[0], o.ch, o.pk); sent = 1; } }
            else if (o.target == -1) { if (g_l.role == ROLE_HOST) { for (int i = 1; i < MAXP; i++) if (g_peer[i]) { enet_peer_send(g_peer[i], o.ch, o.pk); sent = 1; } } }
            else if (o.target > 0 && o.target < MAXP && g_peer[o.target]) { enet_peer_send(g_peer[o.target], o.ch, o.pk); sent = 1; }
            if (!sent) enet_packet_destroy(o.pk);
        }
        if (g_l.role == ROLE_HOST) { host_periodic(); host_check_go(); }
        else if (g_peer[0]) {
            if (g_l.cs == CS_CONNECTED && now_ms() - g_clk_last >= (g_clk_n < 8 ? 120u : 1000u)) {
                g_clk_last = now_ms(); MTsync m = { M_TSYNC, now_us() };
                enet_peer_send(g_peer[0], 1, enet_packet_create(&m, sizeof m, ENET_PACKET_FLAG_UNSEQUENCED)); enet_host_flush(g_host);
            }
            LOCK; g_l.p[0].ping = (int)g_peer[0]->roundTripTime; g_l.p[0].pingvar = (int)g_peer[0]->roundTripTimeVariance;
            g_l.p[0].loss = g_peer[0]->packetLoss / (float)ENET_PEER_PACKET_LOSS_SCALE; UNLOCK;
            if (g_l.cs == CS_CONNECTING) { LOCK; setstatus("Connecting to %s... %llu s", g_l.joined_host, (unsigned long long)((now_ms() - g_connect_start) / 1000)); UNLOCK; }
        }
        enet_host_flush(g_host);
        if (now_ms() - rate_t >= 1000) { LOCK; g_l.tx_rate = g_l.tx_bytes - tx0; g_l.rx_rate = g_l.rx_bytes - rx0; tx0 = g_l.tx_bytes; rx0 = g_l.rx_bytes; UNLOCK; rate_t = now_ms(); }
    }
    return 0;
}
void net_start(void) {
    ensure();
    LOCK; g_l.p[0].used = 1; UNLOCK;
    HANDLE h = CreateThread(0, 0, net_thread, 0, 0, 0); if (h) CloseHandle(h);
}
// ---------- diagnostics ----------
int net_local_ips(char (*out)[96], int max) {
    ULONG sz = 32768; IP_ADAPTER_ADDRESSES* a = (IP_ADAPTER_ADDRESSES*)malloc(sz); int n = 0;
    if (!a) return 0;
    if (GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, 0, a, &sz) == NO_ERROR) {
        for (int pass = 0; pass < 2; pass++)
        for (IP_ADAPTER_ADDRESSES* p = a; p && n < max; p = p->Next) {
            if (p->OperStatus != IfOperStatusUp || p->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
            char desc[128]; WideCharToMultiByte(CP_UTF8, 0, p->Description, -1, desc, sizeof desc, 0, 0);
            char fname[128]; WideCharToMultiByte(CP_UTF8, 0, p->FriendlyName, -1, fname, sizeof fname, 0, 0);
            int zt = strstr(desc, "ZeroTier") || strstr(fname, "ZeroTier");
            if ((pass == 0) != (zt != 0)) continue;
            for (IP_ADAPTER_UNICAST_ADDRESS* u = p->FirstUnicastAddress; u && n < max; u = u->Next) {
                struct sockaddr_in* s = (struct sockaddr_in*)u->Address.lpSockaddr; char ip[32];
                unsigned char* b = (unsigned char*)&s->sin_addr; snprintf(ip, sizeof ip, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
                snprintf(out[n++], 96, "%s%s  (%s)", zt ? "[ZeroTier] " : "", ip, fname);
            }
        }
    }
    free(a); return n;
}
void net_peer_stats(char* out, int cap) {
    ensure(); int k = 0; out[0] = 0;
    LOCK;
    k += snprintf(out + k, cap - k, "State: %s | sent %u B/s, received %u B/s\n", g_l.status, g_l.tx_rate, g_l.rx_rate);
    for (int i = 0; i < MAXP && k < cap - 100; i++) if (g_l.p[i].used && i != g_l.my_id)
        k += snprintf(out + k, cap - k, " #%d %-16s ping %d ms (+-%d) loss %.1f%% %s %s\n", i, g_l.p[i].name, g_l.p[i].ping, g_l.p[i].pingvar,
                      g_l.p[i].loss*100, g_l.p[i].addr, g_l.role == ROLE_HOST && g_l.p[i].last_rx_ms ? "" : "");
    UNLOCK;
}
