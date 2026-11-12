// Network session: ENet thread + lobby state. All functions are thread safe (internal lock).
#pragma once
#include "core.h"
#include "race.h"
#ifdef __cplusplus
extern "C" {
#endif
#define NET_PROTO        8
#define NET_PORT_DEFAULT 27420
#define MAXP             8
enum { ROLE_NONE, ROLE_HOST, ROLE_CLIENT };
enum { CS_IDLE, CS_HOSTING, CS_CONNECTING, CS_CONNECTED };
enum { PS_MENU, PS_LOADING, PS_WAITING, PS_RACING, PS_AWAY };
// Lobby slots: 8 slots, each open/closed/bot/player. A player slot = (connection, local player on that connection).
// The "local" field is for several players on one PC (split screen) later; always 0 for now.
enum { SL_OPEN, SL_CLOSED, SL_BOT, SL_PLAYER };
typedef struct { uint8_t kind, conn, local, pad; } LobbySlot;
typedef struct {
    uint8_t used, id, ready, state;
    char name[32]; CarSel car;
    int ping, pingvar; float loss; uint32_t last_rx_ms; char addr[48];
} PeerInfo;
typedef struct {
    int role, cs; uint8_t my_id;
    uint8_t bots, equal, effects, maxp;   // bots: number of AI cars (at most 8 - humans), maxp: lobby size
    char track[48]; int laps;             // track chosen by the host in the lobby
    char setting[48];                     // the track's setting (for the image)
    LobbySlot slot[MAXP];                 // slots; bots/maxp are derived from them
    char chat[6][100]; uint32_t chat_serial;
    char status[200], error[600];
    PeerInfo p[MAXP];
    int port; char joined_host[64];
    char code[12]; uint8_t steam_net, spad[3];   // Steam lobby code (empty: no Steam lobby); network: 0 the game's AppID, 1 Spacewar
    char steam_note[160];                        // host: short note if the Steam lobby could not be created
    char note[200]; uint32_t note_serial;        // notice for the UI (shown when the serial changes)
    char join_name[32]; uint32_t join_serial;    // host: player who joined the lobby during a race
    uint32_t since_ms;
    uint32_t tx_bytes, rx_bytes, tx_rate, rx_rate;
    int selftest_state; char selftest[200];
} Lobby;
// Online race: built by the host and sent unchanged to everyone. Each PC derives its own local variant.
typedef struct {
    uint32_t race_id; uint8_t nslot, effects, pad[2];   // effects: 1 = effect/power-up sync on
    uint8_t owner[12];       // 0 = host (bots included), 1..7 client
    uint8_t human[12];
    RaceDesc desc;
} OnlineRace;
// The host's final results table: per slot, finished or not and the finish time (on the owner's race clock).
typedef struct { uint32_t race_id; uint8_t n, fin[12], pad[3]; double time[12]; } RaceResult;
enum { RC_RESTART = 1, RC_QUIT, RC_LEFT, RC_OWNER };
typedef struct { uint8_t cmd, slot, owner, pad; uint32_t race_id, new_id; } RaceCmd;
typedef struct { uint8_t tslot, sslot; int32_t level; uint64_t flag; char name[48], powerup[48]; } WireEffect;
typedef struct { uint8_t slot; float t; float q[4], p[3], v[3], w[3], ctl[4]; } WireSnap;
// --- UI commands ---
void net_start(void);
void net_cmd_host(int port);
void net_cmd_join(const char* host, int port);
void net_cmd_leave(void);
void net_cmd_ready(int ready);
void net_cmd_chat(const char* text);
void net_cmd_settings(int bots, int equal, int effects, int maxp);
void net_cmd_track(const char* track, const char* setting, int laps);
void net_cmd_slot(int i);   // host: cycle an empty slot Open -> Bot -> Closed -> Open
void net_cmd_selftest(void);
void net_cmd_invite(void);   // Steam invite window (the game's AppID network)
void net_set_local(const char* name, const CarSel* car, int state);
void net_lobby_copy(Lobby* out);
// --- Game side ---
int  net_role(void);
uint8_t net_my_id(void);
int  net_connected(void);
void net_host_ready_peers(uint8_t* ids, int* n, PeerInfo* infos);   // clients that are ready and in the menu
void net_send_start(const OnlineRace* r);
int  net_take_start(OnlineRace* out);
void net_send_loaded(uint32_t race_id);
void net_set_host_waiting(uint32_t race_id, int waiting);
int64_t net_take_release(uint32_t race_id);   // >0: the hold ends at this QPC time
void net_send_snaps(uint32_t race_id, const WireSnap* s, int n);
int  net_get_snap(int slot, WireSnap* out, uint32_t* age_ms);
void net_race_reset(uint32_t race_id);
void net_send_effect(uint32_t race_id, const WireEffect* e);
int  net_take_effect(WireEffect* out);
void net_send_racecmd(const RaceCmd* c);        // host -> everyone, client -> host
int  net_take_racecmd(RaceCmd* out);
void net_race_restart(uint32_t new_id);          // on restart, reset the network-side race state
void net_race_peer_left(uint8_t id);
// --- Common clock (the host's clock, microseconds) ---
int64_t net_host_now_us(void);
int  net_clock_ok(void);
int64_t net_clock_offset_us(void);
// --- Results: each owner reports its car's finish, the host is the referee ---
void net_report_finish(uint32_t race_id, int slot, double t);
int  net_finish_table(uint32_t race_id, uint8_t* has, double* t);   // host
void net_send_result(const RaceResult* r);                          // host -> everyone
int  net_take_result(uint32_t race_id, RaceResult* out);            // client
int  net_take_join(char* name, int cap);                            // host: someone joined during a race (1: new)
// --- Diagnostics ---
int  net_local_ips(char (*out)[96], int max);   // "name: ip" lines, ZeroTier first
void net_peer_stats(char* out, int cap);       // detailed connection statistics text
// --- Gunluk ---
void net_forward_log(const char* line);
int  log_ring_copy(char (*lines)[200], int max, uint32_t* serial);
#ifdef __cplusplus
}
#endif
