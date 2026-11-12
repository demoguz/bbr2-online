#include <stdio.h>
#include <stdlib.h>
#include <enet/enet.h>
#include "session.h"
enum { M_HELLO = 1, M_WELCOME, M_START, M_READY, M_GO, M_SNAP, M_PING, M_PONG, M_REJECT };
typedef struct { uint8_t type; uint8_t pad[3]; uint32_t proto, build, vsize; char name[32]; VehDesc car; } MsgHello;
typedef struct { uint8_t type; uint8_t id; uint8_t nplayers; uint8_t reason; } MsgWelcome;
typedef struct { uint8_t type; uint8_t pad[3]; NetRace r; } MsgStart;
typedef struct { uint8_t type; uint8_t pad[3]; uint32_t race_id; } MsgReady;
typedef struct { uint8_t type; uint8_t pad[3]; uint32_t race_id; double T; } MsgGo;
typedef struct { uint8_t type; uint8_t slot; uint8_t pad[2]; uint32_t race_id; Snap s; } MsgSnap;
typedef struct { uint8_t type; uint8_t pad[7]; double tc, th; } MsgPing;
typedef struct { int used, hello, ready; char name[32]; VehDesc car; ENetPeer* peer; } Player;
struct Session {
    int mode, port, my_id, connected, nping; uint32_t build; char addr[64], name[32]; VehDesc car; ses_logfn lg;
    ENetHost* host; ENetPeer* server; Player pl[SES_MAXP];
    NetRace race; int have_start, racing, go; double go_T, start_at, next_ping, next_conn;
    double off, best_rtt; double rtts[8], offs[8];
    SnapHist hist[SES_MAXSLOT];
};
static int g_enet_refs;
#define LG(...) do { if (s->lg) s->lg(__VA_ARGS__); } while (0)
static void send_to(ENetPeer* p, int ch, const void* d, size_t n, int reliable) {
    if (!p) return;
    ENetPacket* pk = enet_packet_create(d, n, reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
    if (pk && enet_peer_send(p, (enet_uint8)ch, pk) < 0) enet_packet_destroy(pk);
}
static void bcast(Session* s, int ch, const void* d, size_t n, int reliable, int except) {
    for (int i = 1; i < SES_MAXP; i++) if (s->pl[i].used && s->pl[i].hello && i != except) send_to(s->pl[i].peer, ch, d, n, reliable);
}
static void scpy(char* d, int cap, const char* src) { strncpy(d, src, cap - 1); d[cap - 1] = 0; }
Session* ses_create(int mode, const char* addr, int port, const char* name, const VehDesc* car, uint32_t build, ses_logfn lg) {
    if (!g_enet_refs && enet_initialize() != 0) return 0;
    g_enet_refs++;
    Session* s = calloc(1, sizeof *s); if (!s) return 0;
    s->mode = mode; s->port = port; s->build = build; s->lg = lg; s->best_rtt = 1e9;
    scpy(s->addr, sizeof s->addr, addr ? addr : ""); scpy(s->name, sizeof s->name, name && name[0] ? name : "player");
    if (car) s->car = *car;
    if (mode == SES_HOST) {
        ENetAddress a; a.host = ENET_HOST_ANY; a.port = (enet_uint16)port;
        s->host = enet_host_create(&a, SES_MAXP, 2, 0, 0);
        if (!s->host) { LG("ERROR: could not open port %d", port); free(s); return 0; }
        s->pl[0].used = s->pl[0].hello = 1; scpy(s->pl[0].name, 32, s->name); s->pl[0].car = s->car; s->connected = 1;
        LG("session: host, UDP port %d", port);
    } else {
        s->host = enet_host_create(0, 1, 2, 0, 0);
        if (!s->host) { free(s); return 0; }
        s->my_id = -1;
    }
    return s;
}
void ses_destroy(Session* s) { if (!s) return; if (s->host) { enet_host_flush(s->host); enet_host_destroy(s->host); } free(s); if (--g_enet_refs == 0) enet_deinitialize(); }
int ses_mode(Session* s) { return s ? s->mode : SES_OFF; }
int ses_my_id(Session* s) { return s->my_id; }
int ses_connected(Session* s) { return s && s->connected; }
int ses_nplayers(Session* s) { int n = 0; for (int i = 0; i < SES_MAXP; i++) n += s->pl[i].used && s->pl[i].hello; return s->mode == SES_HOST ? n : (s->connected ? 1 : 0); }
double ses_time(Session* s, double now) { return now + s->off; }
static void check_go(Session* s, double now, int force) {
    if (!s->racing || s->go) return;
    for (int i = 0; i < SES_MAXP && !force; i++) if (s->pl[i].used && s->pl[i].hello && !s->pl[i].ready) return;
    MsgGo g; memset(&g, 0, sizeof g); g.type = M_GO; g.race_id = s->race.race_id; g.T = now + 0.5;
    bcast(s, 0, &g, sizeof g, 1, -1);
    s->go = 1; s->go_T = g.T; LG("oturum: GO, T=%.3f%s", g.T, force ? " (timeout)" : "");
}
int ses_host_start(Session* s, const RaceDesc* t, int powerups, double now) {
    if (s->mode != SES_HOST) return 0;
    NetRace* r = &s->race; memset(r, 0, sizeof *r);
    r->d = *t; r->d.nveh = 0; r->d.gridmode = 0; r->powerups = powerups; r->race_id = (uint32_t)(now*1000.0) | 1u;
    if (!powerups) r->d.nshared = 0;
    int nh = 0; for (int i = 0; i < SES_MAXP; i++) nh += s->pl[i].used && s->pl[i].hello;
    int total = t->nveh > 8 ? t->nveh : 8; if (total > SES_MAXSLOT) total = SES_MAXSLOT;
    for (int i = 0; i < t->nveh && r->d.nveh < total - nh; i++) {      // AIs first (in template order)
        if (t->v[i].input != 1) continue;
        int k = r->d.nveh++; r->d.v[k] = t->v[i]; r->owner[k] = 0; r->d.v[k].viewport = -1; r->d.v[k].ctrlmask = 0;
    }
    for (int i = 0; i < SES_MAXP && r->d.nveh < SES_MAXSLOT; i++) {    // then the players: they start at the back
        if (!(s->pl[i].used && s->pl[i].hello)) continue;
        int k = r->d.nveh++; r->d.v[k] = s->pl[i].car; r->owner[k] = (uint8_t)i; r->d.v[k].input = 0;
        scpy(r->d.v[k].name, sizeof r->d.v[k].name, s->pl[i].name);
        s->pl[i].ready = 0;
    }
    for (int k = 0; k < r->d.nveh; k++) { r->d.v[k].powerups = powerups ? 1 : 0; if (!powerups) r->d.v[k].ndeck = 0; }
    MsgStart m; memset(&m, 0, sizeof m); m.type = M_START; m.r = *r;
    bcast(s, 0, &m, sizeof m, 1, -1);
    for (int k = 0; k < SES_MAXSLOT; k++) hist_reset(&s->hist[k]);
    s->have_start = 1; s->racing = 1; s->go = 0; s->start_at = now;
    LG("session: race %u started: %d cars, %d players, track %s", r->race_id, r->d.nveh, nh, r->d.track);
    return 1;
}
int ses_take_start(Session* s, NetRace* out) { if (!s->have_start) return 0; s->have_start = 0; *out = s->race; return 1; }
void ses_ready(Session* s, double now) {
    if (!s->racing) return;
    if (s->mode == SES_HOST) { s->pl[0].ready = 1; check_go(s, now, 0); }
    else { MsgReady m; memset(&m, 0, sizeof m); m.type = M_READY; m.race_id = s->race.race_id; send_to(s->server, 0, &m, sizeof m, 1); }
}
int ses_go_time(Session* s, double* T) { if (!s->go) return 0; *T = s->go_T; return 1; }
void ses_end_race(Session* s) { if (s) { s->racing = 0; s->go = 0; s->have_start = 0; } }
void ses_send_snap(Session* s, int slot, const Snap* sn) {
    if (!s->racing || slot < 0 || slot >= SES_MAXSLOT) return;
    MsgSnap m; memset(&m, 0, sizeof m); m.type = M_SNAP; m.slot = (uint8_t)slot; m.race_id = s->race.race_id; m.s = *sn;
    if (s->mode == SES_HOST) bcast(s, 1, &m, sizeof m, 0, -1); else send_to(s->server, 1, &m, sizeof m, 0);
}
const Snap* ses_latest(Session* s, int slot) { return (slot >= 0 && slot < SES_MAXSLOT) ? hist_pick(&s->hist[slot], 1e300) : 0; }
static void on_packet_host(Session* s, int id, const uint8_t* d, size_t n, double now) {
    Player* p = &s->pl[id];
    switch (d[0]) {
    case M_HELLO: {
        if (n != sizeof(MsgHello)) return;
        const MsgHello* h = (const MsgHello*)d; MsgWelcome w; memset(&w, 0, sizeof w);
        if (h->proto != SES_PROTO || h->build != s->build || h->vsize != sizeof(VehDesc)) {
            w.type = M_REJECT; w.reason = h->proto != SES_PROTO ? 1 : 2; send_to(p->peer, 0, &w, sizeof w, 1);
            LG("session: player refused (version mismatch: proto %u, exe %08x)", h->proto, h->build); return;
        }
        p->hello = 1; p->ready = 1; scpy(p->name, 32, h->name); p->car = h->car;
        w.type = M_WELCOME; w.id = (uint8_t)id; w.nplayers = (uint8_t)ses_nplayers(s); send_to(p->peer, 0, &w, sizeof w, 1);
        LG("session: '%s' joined (player %d, car %s, driver %s)", p->name, id, p->car.vehicle, p->car.driver); break; }
    case M_READY: if (n == sizeof(MsgReady) && ((const MsgReady*)d)->race_id == s->race.race_id) { p->ready = 1; LG("session: '%s' ready", p->name); check_go(s, now, 0); } break;
    case M_SNAP: {
        if (n != sizeof(MsgSnap)) return;
        const MsgSnap* m = (const MsgSnap*)d;
        if (!s->racing || m->race_id != s->race.race_id || m->slot >= SES_MAXSLOT || s->race.owner[m->slot] != id || s->race.d.v[m->slot].input != 0) return;
        hist_push(&s->hist[m->slot], &m->s); bcast(s, 1, d, n, 0, id); break; }
    case M_PING: if (n == sizeof(MsgPing)) { MsgPing r = *(const MsgPing*)d; r.type = M_PONG; r.th = now; send_to(p->peer, 1, &r, sizeof r, 0); } break;
    }
}
static void on_packet_client(Session* s, const uint8_t* d, size_t n, double now) {
    switch (d[0]) {
    case M_WELCOME: if (n == sizeof(MsgWelcome)) { s->my_id = ((const MsgWelcome*)d)->id; s->connected = 1; LG("session: connected, player number %d", s->my_id); } break;
    case M_REJECT: LG("ERROR: refused by the host (%s)", n >= 4 && d[3] == 1 ? "different mod version" : "different game version"); break;
    case M_START: if (n == sizeof(MsgStart)) {
        s->race = ((const MsgStart*)d)->r; s->have_start = 1; s->racing = 1; s->go = 0;
        for (int k = 0; k < SES_MAXSLOT; k++) hist_reset(&s->hist[k]);
        LG("session: race recipe received (%u): %d cars, track %s", s->race.race_id, s->race.d.nveh, s->race.d.track); } break;
    case M_GO: if (n == sizeof(MsgGo) && ((const MsgGo*)d)->race_id == s->race.race_id) { s->go = 1; s->go_T = ((const MsgGo*)d)->T; LG("session: GO received, T=%.3f (now %.3f)", s->go_T, ses_time(s, now)); } break;
    case M_SNAP: if (n == sizeof(MsgSnap)) {
        const MsgSnap* m = (const MsgSnap*)d;
        if (s->racing && m->race_id == s->race.race_id && m->slot < SES_MAXSLOT) hist_push(&s->hist[m->slot], &m->s); } break;
    case M_PONG: if (n == sizeof(MsgPing)) {
        const MsgPing* m = (const MsgPing*)d; double rtt = now - m->tc; if (rtt < 0 || rtt > 2) return;
        int k = s->nping++ & 7; s->rtts[k] = rtt; s->offs[k] = m->th + rtt*0.5 - now;
        int b = 0, cnt = s->nping < 8 ? s->nping : 8; for (int i = 1; i < cnt; i++) if (s->rtts[i] < s->rtts[b]) b = i;
        s->off = s->offs[b]; s->best_rtt = s->rtts[b];
        if (s->nping == 8) LG("session: clock sync done, round trip %.0f ms", s->best_rtt*1000); } break;
    }
}
void ses_poll(Session* s, double now) {
    if (!s || !s->host) return;
    if (s->mode == SES_JOIN) {
        if (!s->server && now >= s->next_conn) {
            ENetAddress a; a.port = (enet_uint16)s->port; s->next_conn = now + 3.0;
            if (enet_address_set_host(&a, s->addr) == 0) { s->server = enet_host_connect(s->host, &a, 2, 0); LG("session: connecting to %s:%d", s->addr, s->port); }
            else LG("ERROR: could not resolve address: %s", s->addr);
        }
        if (s->connected && now >= s->next_ping) {
            MsgPing p; memset(&p, 0, sizeof p); p.type = M_PING; p.tc = now; send_to(s->server, 1, &p, sizeof p, 0);
            s->next_ping = now + (s->nping < 8 ? 0.2 : 2.0);
        }
    } else if (s->racing && !s->go && now - s->start_at > 60.0) check_go(s, now, 1);
    ENetEvent ev;
    while (enet_host_service(s->host, &ev, 0) > 0) {
        switch (ev.type) {
        case ENET_EVENT_TYPE_CONNECT:
            if (s->mode == SES_HOST) {
                int id = -1; for (int i = 1; i < SES_MAXP; i++) if (!s->pl[i].used) { id = i; break; }
                if (id < 0) { enet_peer_disconnect(ev.peer, 0); break; }
                memset(&s->pl[id], 0, sizeof s->pl[id]); s->pl[id].used = 1; s->pl[id].peer = ev.peer; ev.peer->data = (void*)(intptr_t)id;
            } else {
                MsgHello h; memset(&h, 0, sizeof h); h.type = M_HELLO; h.proto = SES_PROTO; h.build = s->build; h.vsize = sizeof(VehDesc);
                scpy(h.name, 32, s->name); h.car = s->car; send_to(s->server, 0, &h, sizeof h, 1);
            }
            break;
        case ENET_EVENT_TYPE_RECEIVE:
            if (ev.packet->dataLength >= 1) {
                if (s->mode == SES_HOST) { int id = (int)(intptr_t)ev.peer->data; if (id > 0 && id < SES_MAXP && s->pl[id].used) on_packet_host(s, id, ev.packet->data, ev.packet->dataLength, now); }
                else on_packet_client(s, ev.packet->data, ev.packet->dataLength, now);
            }
            enet_packet_destroy(ev.packet); break;
        case ENET_EVENT_TYPE_DISCONNECT:
            if (s->mode == SES_HOST) {
                int id = (int)(intptr_t)ev.peer->data;
                if (id > 0 && id < SES_MAXP && s->pl[id].used) { LG("session: '%s' left", s->pl[id].name); s->pl[id].used = 0; check_go(s, now, 0); }
            } else {
                if (s->connected) LG("session: connection to the host lost");
                s->connected = 0; s->server = 0; s->my_id = -1; s->nping = 0; s->next_conn = now + 3.0;
            }
            break;
        default: break;
        }
    }
    enet_host_flush(s->host);
}
