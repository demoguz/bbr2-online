// Steam tunnel (service). Description and message format: steamsvc.h
// The Steamworks "flat" API is taken from steam_api64.dll with GetProcAddress (no SDK headers needed).
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdbool.h>
#include <wchar.h>
#include "steamsvc.h"
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif

typedef uint64_t SteamAPICall_t;
static struct {
    int  (*InitFlat)(char* err1024);           // ESteamAPIInitResult, 0 = ok
    void (*RunCallbacks)(void);
    void (*RegisterCallback)(void* cb, int id);
    int32_t (*GetHSteamUser)(void);
    void (*Shutdown)(void);
    void* (*Mm)(void); void* (*Fr)(void); void* (*User)(void); void* (*Utils)(void); void* (*Msgs)(void); void* (*NUtils)(void);
    SteamAPICall_t (*CreateLobby)(void*, int type, int maxm);
    SteamAPICall_t (*JoinLobby)(void*, uint64_t);
    void (*LeaveLobby)(void*, uint64_t);
    bool (*SetLobbyData)(void*, uint64_t, const char*, const char*);
    const char* (*GetLobbyData)(void*, uint64_t, const char*);
    uint64_t (*GetLobbyOwner)(void*, uint64_t);
    SteamAPICall_t (*RequestLobbyList)(void*);
    void (*AddStringFilter)(void*, const char*, const char*, int);
    void (*AddDistanceFilter)(void*, int);
    void (*AddCountFilter)(void*, int);
    uint64_t (*LobbyByIndex)(void*, int);
    int (*NumMembers)(void*, uint64_t);
    uint64_t (*MemberByIndex)(void*, uint64_t, int);
    void (*InviteDialog)(void*, uint64_t);
    const char* (*PersonaName)(void*);
    const char* (*FriendName)(void*, uint64_t);
    uint64_t (*MySteamID)(void*);
    bool (*CallDone)(void*, SteamAPICall_t, bool*);
    bool (*CallResult)(void*, SteamAPICall_t, void*, int, int, bool*);
    uint32_t (*AppID)(void*);
    bool (*OverlayOn)(void*);
    int  (*Send)(void*, const void* id, const void* data, uint32_t len, int flags, int ch);   // EResult, 1 = ok
    int  (*Recv)(void*, int ch, void** out, int max);
    bool (*Accept)(void*, const void* id);
    bool (*CloseSess)(void*, const void* id);
    void (*MsgRelease)(void* msg);
    void (*IdSet)(void* id, uint64_t sid);
    uint64_t (*IdGet)(const void* id);
    void (*InitRelay)(void*);
} S;
#define F(f, n) { n, (void**)&S.f }
static const struct { const char* n; void** p; } k_fn[] = {
    F(InitFlat, "SteamAPI_InitFlat"), F(RunCallbacks, "SteamAPI_RunCallbacks"), F(RegisterCallback, "SteamAPI_RegisterCallback"),
    F(GetHSteamUser, "SteamAPI_GetHSteamUser"), F(Shutdown, "SteamAPI_Shutdown"),
    F(Mm, "SteamAPI_SteamMatchmaking_v009"), F(Fr, "SteamAPI_SteamFriends_v017"), F(User, "SteamAPI_SteamUser_v023"),
    F(Utils, "SteamAPI_SteamUtils_v010"), F(Msgs, "SteamAPI_SteamNetworkingMessages_SteamAPI_v002"), F(NUtils, "SteamAPI_SteamNetworkingUtils_SteamAPI_v004"),
    F(CreateLobby, "SteamAPI_ISteamMatchmaking_CreateLobby"), F(JoinLobby, "SteamAPI_ISteamMatchmaking_JoinLobby"),
    F(LeaveLobby, "SteamAPI_ISteamMatchmaking_LeaveLobby"), F(SetLobbyData, "SteamAPI_ISteamMatchmaking_SetLobbyData"),
    F(GetLobbyData, "SteamAPI_ISteamMatchmaking_GetLobbyData"), F(GetLobbyOwner, "SteamAPI_ISteamMatchmaking_GetLobbyOwner"),
    F(RequestLobbyList, "SteamAPI_ISteamMatchmaking_RequestLobbyList"),
    F(AddStringFilter, "SteamAPI_ISteamMatchmaking_AddRequestLobbyListStringFilter"),
    F(AddDistanceFilter, "SteamAPI_ISteamMatchmaking_AddRequestLobbyListDistanceFilter"),
    F(AddCountFilter, "SteamAPI_ISteamMatchmaking_AddRequestLobbyListResultCountFilter"),
    F(LobbyByIndex, "SteamAPI_ISteamMatchmaking_GetLobbyByIndex"), F(NumMembers, "SteamAPI_ISteamMatchmaking_GetNumLobbyMembers"),
    F(MemberByIndex, "SteamAPI_ISteamMatchmaking_GetLobbyMemberByIndex"),
    F(InviteDialog, "SteamAPI_ISteamFriends_ActivateGameOverlayInviteDialog"), F(PersonaName, "SteamAPI_ISteamFriends_GetPersonaName"),
    F(FriendName, "SteamAPI_ISteamFriends_GetFriendPersonaName"), F(MySteamID, "SteamAPI_ISteamUser_GetSteamID"),
    F(CallDone, "SteamAPI_ISteamUtils_IsAPICallCompleted"), F(CallResult, "SteamAPI_ISteamUtils_GetAPICallResult"),
    F(AppID, "SteamAPI_ISteamUtils_GetAppID"), F(OverlayOn, "SteamAPI_ISteamUtils_IsOverlayEnabled"),
    F(Send, "SteamAPI_ISteamNetworkingMessages_SendMessageToUser"), F(Recv, "SteamAPI_ISteamNetworkingMessages_ReceiveMessagesOnChannel"),
    F(Accept, "SteamAPI_ISteamNetworkingMessages_AcceptSessionWithUser"), F(CloseSess, "SteamAPI_ISteamNetworkingMessages_CloseSessionWithUser"),
    F(MsgRelease, "SteamAPI_SteamNetworkingMessage_t_Release"), F(IdSet, "SteamAPI_SteamNetworkingIdentity_SetSteamID64"),
    F(IdGet, "SteamAPI_SteamNetworkingIdentity_GetSteamID64"), F(InitRelay, "SteamAPI_ISteamNetworkingUtils_InitRelayNetworkAccess"),
};
#undef F

static SvcConfig C;
static SOCKET g_ctl = INVALID_SOCKET; static struct sockaddr_in g_dll;
static void *g_mm, *g_fr, *g_user, *g_utils, *g_msgs;
static uint64_t g_me; static int g_quit;
static uint64_t ms(void) { return GetTickCount64(); }
static void say(const char* fmt, ...) {
    char b[760]; va_list ap; va_start(ap, fmt); int n = vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    if (n < 0) return; if (n >= (int)sizeof b) n = (int)sizeof b - 1;
    sendto(g_ctl, b, n, 0, (struct sockaddr*)&g_dll, sizeof g_dll);
}
#define SLOG(...) do { char _t[640]; snprintf(_t, sizeof _t, __VA_ARGS__); say("log %d %s", C.net, _t); } while (0)

// ---------- callbacks: imitation of the C++ CCallbackBase ----------
// MSVC x64 layout: [vptr][uint8 flags][int32 id]. MSVC lays out overloaded virtual functions in reverse order:
// vtable[0] = Run(void*, bool, SteamAPICall_t), [1] = Run(void*), [2] = GetCallbackSizeBytes().
typedef struct CbObj CbObj;
typedef struct { void (*run_res)(CbObj*, void*, bool, SteamAPICall_t); void (*run)(CbObj*, void*); int (*size)(CbObj*); } CbVt;
struct CbObj { const CbVt* vt; uint8_t flags; int32_t id; int cb; };
typedef struct { int id; uint8_t d[280]; } CbEv;
static CbEv g_cq[64]; static int g_cqh, g_cqt; static CRITICAL_SECTION g_cqcs;
static void cb_run(CbObj* o, void* p) {   // in process this comes from the game's thread: only queue it
    EnterCriticalSection(&g_cqcs);
    int nh = (g_cqh + 1) % 64;
    if (nh != g_cqt && o->cb <= (int)sizeof g_cq[0].d) { g_cq[g_cqh].id = o->id; memcpy(g_cq[g_cqh].d, p, (size_t)o->cb); g_cqh = nh; }
    LeaveCriticalSection(&g_cqcs);
}
static void cb_run_res(CbObj* o, void* p, bool io, SteamAPICall_t c) { (void)io; (void)c; cb_run(o, p); }
static int cb_size(CbObj* o) { return o->cb; }
static const CbVt k_cbvt = { cb_run_res, cb_run, cb_size };
enum { CB_JOINREQ = 333, CB_CHATUPD = 506, CB_SESSREQ = 1251 };   // GameLobbyJoinRequested_t, LobbyChatUpdate_t, SteamNetworkingMessagesSessionRequest_t
static CbObj g_cbo[3] = { { &k_cbvt, 0, CB_JOINREQ, 16 }, { &k_cbvt, 0, CB_CHATUPD, 32 }, { &k_cbvt, 0, CB_SESSREQ, 136 } };

// ---------- tunnels: one local UDP socket per remote player ----------
typedef struct { uint8_t b[136]; } NetId;   // SteamNetworkingIdentity
typedef struct { uint64_t sid; NetId id; SOCKET s; struct sockaddr_in to; int has_to; uint64_t last; unsigned short port; } Tun;
#define MAXT 8
static Tun g_t[MAXT]; static int g_nt;
static struct sockaddr_in loopback(unsigned short port) {
    struct sockaddr_in a; memset(&a, 0, sizeof a); a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = htons(port); return a;
}
static SOCKET udp_local(unsigned short* port) {
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP); if (s == INVALID_SOCKET) return s;
    struct sockaddr_in a = loopback(0);
    if (bind(s, (struct sockaddr*)&a, sizeof a)) { closesocket(s); return INVALID_SOCKET; }
    u_long nb = 1; ioctlsocket(s, FIONBIO, &nb);
    BOOL off = FALSE; DWORD br = 0; WSAIoctl(s, SIO_UDP_CONNRESET, &off, sizeof off, 0, 0, &br, 0, 0);
    int bs = 1 << 18; setsockopt(s, SOL_SOCKET, SO_RCVBUF, (const char*)&bs, sizeof bs); setsockopt(s, SOL_SOCKET, SO_SNDBUF, (const char*)&bs, sizeof bs);
    if (port) { int l = sizeof a; getsockname(s, (struct sockaddr*)&a, &l); *port = ntohs(a.sin_port); }
    return s;
}
static void mkid(NetId* id, uint64_t sid) { memset(id, 0, sizeof *id); S.IdSet(id, sid); }
static const char* who(uint64_t sid) { const char* n = S.FriendName(g_fr, sid); return n && n[0] ? n : "?"; }
static Tun* tun_find(uint64_t sid) { for (int i = 0; i < g_nt; i++) if (g_t[i].sid == sid) return &g_t[i]; return 0; }
static Tun* tun_new(uint64_t sid) {
    if (g_nt >= MAXT) return 0;
    Tun* t = &g_t[g_nt]; memset(t, 0, sizeof *t);
    t->s = udp_local(&t->port); if (t->s == INVALID_SOCKET) return 0;
    t->sid = sid; mkid(&t->id, sid); t->last = ms(); g_nt++; return t;
}
static void tun_close(int i) {
    S.CloseSess(g_msgs, &g_t[i].id); closesocket(g_t[i].s);
    SLOG("tunnel closed: %s", who(g_t[i].sid));
    g_t[i] = g_t[--g_nt];
}

// ---------- lobby state ----------
enum { ST_IDLE, ST_CREATING, ST_HOSTING, ST_SEARCHING, ST_JOINING, ST_JOINED };
static int g_st, g_seq, g_proto; static uint64_t g_lobby, g_owner, g_join_lobby; static unsigned short g_enet_port;
static char g_code[8], g_name[48], g_ver[40], g_exe[16];
static SteamAPICall_t g_call; static int g_call_id, g_call_size; static uint64_t g_call_ms;
static uint64_t g_pend[8], g_pend_ms[8]; static int g_np;
static int g_senderr;
static const char* k_alpha = "ABCDEFGHJKMNPQRSTUVWXYZ23456789";             // no I, L, O, 0, 1
static const char* k_first[2] = { "ABCDEFGHJKM", "NPQRSTUVWXYZ" };          // the first letter shows the network: game AppID / Spacewar
static uint64_t g_rng;
static uint32_t rnd(void) {
    if (!g_rng) { LARGE_INTEGER q; QueryPerformanceCounter(&q); g_rng = (uint64_t)q.QuadPart ^ ((uint64_t)GetCurrentProcessId() << 32) ^ g_me ^ 0x9E3779B97F4A7C15ull; }
    g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17; return (uint32_t)(g_rng >> 16);
}
static void make_code(char* out) {
    const char* f = k_first[C.net ? 1 : 0]; out[0] = f[rnd() % strlen(f)];
    for (int i = 1; i < 5; i++) out[i] = k_alpha[rnd() % strlen(k_alpha)];
    out[5] = 0;
}
static int is_member(uint64_t sid) {
    if (!g_lobby) return 0;
    int n = S.NumMembers(g_mm, g_lobby);
    for (int i = 0; i < n; i++) if (S.MemberByIndex(g_mm, g_lobby, i) == sid) return 1;
    return 0;
}
static void set_call(SteamAPICall_t c, int id, int size) { g_call = c; g_call_id = id; g_call_size = size; g_call_ms = ms(); }
static void leave(void) {
    while (g_nt) tun_close(g_nt - 1);
    g_np = 0;
    if (g_lobby) { S.LeaveLobby(g_mm, g_lobby); SLOG("left the Steam lobby"); }
    else if (g_st == ST_JOINING && g_join_lobby) S.LeaveLobby(g_mm, g_join_lobby);
    g_lobby = g_owner = g_join_lobby = 0; g_call = 0; g_st = ST_IDLE; g_code[0] = 0;
}
static void join_fail(const char* fmt, ...) {
    char b[400]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    SLOG("join failed: %s", b);
    leave(); say("joinfail %d %d %s", C.net, g_seq, b);
}
static void start_join(uint64_t lob) {
    g_join_lobby = lob;
    SteamAPICall_t c = S.JoinLobby(g_mm, lob);
    if (!c) { join_fail("Steam could not start joining the lobby."); return; }
    set_call(c, 504, 24); g_st = ST_JOINING; SLOG("entering lobby %llu", (unsigned long long)lob);
}
static void on_call(uint8_t* r) {
    if (g_call_id == 513) {        // LobbyCreated_t { EResult; uint64 lobby }
        int32_t res = *(int32_t*)r; uint64_t lob = *(uint64_t*)(r + 8);
        if (res != 1 || !lob) { g_st = ST_IDLE; SLOG("could not create the lobby (EResult %d)", res); say("hostfail %d %d Steam refused to create a lobby (error %d).", C.net, g_seq, res); return; }
        g_lobby = lob; g_owner = g_me; make_code(g_code);
        char pr[16]; snprintf(pr, sizeof pr, "%d", g_proto);
        S.SetLobbyData(g_mm, lob, "bbr2", "1"); S.SetLobbyData(g_mm, lob, "bbr2code", g_code);
        S.SetLobbyData(g_mm, lob, "bbr2proto", pr); S.SetLobbyData(g_mm, lob, "bbr2exe", g_exe);
        S.SetLobbyData(g_mm, lob, "bbr2ver", g_ver); S.SetLobbyData(g_mm, lob, "bbr2name", g_name);
        S.SetLobbyData(g_mm, lob, "bbr2listed", "0");   // door for a public lobby list: lobbies with 1 will be listed
        g_st = ST_HOSTING; SLOG("lobby created: code %s, lobby %llu", g_code, (unsigned long long)lob);
        say("hosted %d %d %s %llu", C.net, g_seq, g_code, (unsigned long long)lob);
    } else if (g_call_id == 510) {   // LobbyMatchList_t { uint32 count }
        uint32_t n = *(uint32_t*)r;
        if (!n) { join_fail("No lobby with the code %s was found. Check the code; the host must still be in the lobby.", g_code); return; }
        uint64_t lob = S.LobbyByIndex(g_mm, 0);
        SLOG("code %s -> lobby %llu (%u results)", g_code, (unsigned long long)lob, n);
        start_join(lob);
    } else if (g_call_id == 504) {   // LobbyEnter_t { uint64 lobby; uint32 permissions; bool locked; uint32 response } = 24 bytes
        uint64_t lob = *(uint64_t*)r; uint32_t resp = *(uint32_t*)(r + 16);
        if (resp != 1) {
            g_join_lobby = 0;
            if (resp == 2) join_fail("The lobby no longer exists.");
            else if (resp == 4) join_fail("The Steam lobby is full.");
            else if (resp == 3) join_fail("You are not allowed to join this lobby.");
            else join_fail("Steam could not join the lobby (response %u).", resp);
            return;
        }
        g_lobby = lob; g_join_lobby = 0;
        const char* mk = S.GetLobbyData(g_mm, lob, "bbr2");
        if (!mk || strcmp(mk, "1")) { join_fail("This Steam lobby is not a BBR2 Online lobby."); return; }
        const char* pr = S.GetLobbyData(g_mm, lob, "bbr2proto"); const char* ver = S.GetLobbyData(g_mm, lob, "bbr2ver");
        if (!pr || atoi(pr) != g_proto) { join_fail("The host uses a different version of the mod (%s). Both players need the same version.", ver && ver[0] ? ver : "unknown"); return; }
        g_owner = S.GetLobbyOwner(g_mm, lob);
        if (!g_owner) { join_fail("The lobby has no host."); return; }
        if (g_owner == g_me) { join_fail("This is your own lobby."); return; }
        const char* code = S.GetLobbyData(g_mm, lob, "bbr2code"); snprintf(g_code, sizeof g_code, "%s", code && code[0] ? code : "-");
        const char* nm = S.GetLobbyData(g_mm, lob, "bbr2name"); if (!nm || !nm[0]) nm = who(g_owner);
        Tun* t = tun_new(g_owner);
        if (!t) { join_fail("Could not open a local network socket."); return; }
        g_st = ST_JOINED;
        SLOG("entered lobby %llu, host %s (%s), local tunnel 127.0.0.1:%u", (unsigned long long)lob, nm, who(g_owner), t->port);
        say("joined %d %d %u %s %s", C.net, g_seq, t->port, g_code, nm);
    }
}
static void poll_call(void) {
    if (!g_call) return;
    bool failed = false;
    if (!S.CallDone(g_utils, g_call, &failed)) {
        if (ms() - g_call_ms > 20000) { g_call = 0; SLOG("Steam call timed out (%d)", g_call_id);
            if (g_st == ST_CREATING) { g_st = ST_IDLE; say("hostfail %d %d Steam did not answer the lobby request.", C.net, g_seq); } else join_fail("Steam did not answer in time."); }
        return;
    }
    uint8_t r[64]; memset(r, 0, sizeof r); bool f2 = false;
    bool ok = S.CallResult(g_utils, g_call, r, g_call_size, g_call_id, &f2);
    g_call = 0;
    if (!ok || failed || f2) {
        SLOG("Steam call failed (expected %d, size %d, ok %d, error %d/%d)", g_call_id, g_call_size, ok, failed, f2);
        if (g_st == ST_CREATING) { g_st = ST_IDLE; say("hostfail %d %d Steam could not create the lobby.", C.net, g_seq); }
        else join_fail("Steam could not reach the lobby.");
        return;
    }
    on_call(r);
}
static void accept_sid(uint64_t sid) { NetId id; mkid(&id, sid); S.Accept(g_msgs, &id); SLOG("session accepted: %s", who(sid)); }
static void on_cb(CbEv* e) {
    if (e->id == CB_SESSREQ) {
        uint64_t sid = S.IdGet(e->d);
        if ((g_st == ST_HOSTING && is_member(sid)) || (g_st == ST_JOINED && sid == g_owner)) accept_sid(sid);
        else if (g_st == ST_HOSTING && g_np < 8) { g_pend[g_np] = sid; g_pend_ms[g_np++] = ms(); SLOG("session request on hold (not in the lobby yet): %s", who(sid)); }
        else SLOG("session request ignored: %s", who(sid));
    } else if (e->id == CB_JOINREQ) {
        uint64_t lob = *(uint64_t*)e->d, fr = *(uint64_t*)(e->d + 8);
        SLOG("join request from Steam: lobby %llu (%s)", (unsigned long long)lob, fr ? who(fr) : "-");
        say("invited %d %llu %s", C.net, (unsigned long long)lob, fr ? who(fr) : "");
    } else if (e->id == CB_CHATUPD) {
        uint64_t lob = *(uint64_t*)e->d, u = *(uint64_t*)(e->d + 8); uint32_t st = *(uint32_t*)(e->d + 24);
        if (lob != g_lobby) return;
        if (st & 1) SLOG("entered the lobby: %s", who(u));
        if (st & 0x1e) {
            SLOG("left the lobby: %s", who(u));
            if (g_st == ST_HOSTING) for (int i = 0; i < g_nt; i++) if (g_t[i].sid == u) { tun_close(i); break; }
        }
    }
}
static void pend_check(void) {
    for (int i = 0; i < g_np; ) {
        if (g_st == ST_HOSTING && is_member(g_pend[i])) { accept_sid(g_pend[i]); g_pend[i] = g_pend[--g_np]; g_pend_ms[i] = g_pend_ms[g_np]; }
        else if (g_st != ST_HOSTING || ms() - g_pend_ms[i] > 8000) { SLOG("session request dropped: %s", who(g_pend[i])); g_pend[i] = g_pend[--g_np]; g_pend_ms[i] = g_pend_ms[g_np]; }
        else i++;
    }
}

// ---------- packet pumps ----------
static void pump_steam(void) {
    void* m[64];
    for (int round = 0; round < 8; round++) {
        int n = S.Recv(g_msgs, 0, m, 64); if (n <= 0) break;
        for (int i = 0; i < n; i++) {
            uint8_t* msg = (uint8_t*)m[i];   // SteamNetworkingMessage_t: +0 data, +8 size, +16 sender identity
            const char* data = *(const char**)msg; int len = *(int*)(msg + 8); uint64_t sid = S.IdGet(msg + 16);
            Tun* t = tun_find(sid);
            if (!t && g_st == ST_HOSTING && is_member(sid)) {
                t = tun_new(sid);
                if (t) { t->to = loopback(g_enet_port); t->has_to = 1; SLOG("tunnel open: %s -> 127.0.0.1:%u (local %u)", who(sid), g_enet_port, t->port); }
            }
            if (t && t->has_to && len > 0) { sendto(t->s, data, len, 0, (struct sockaddr*)&t->to, sizeof t->to); t->last = ms(); }
            S.MsgRelease(msg);
        }
        if (n < 64) break;
    }
}
static void pump_local(fd_set* rd) {
    char buf[2048];
    for (int i = 0; i < g_nt; i++) {
        Tun* t = &g_t[i]; if (!FD_ISSET(t->s, rd)) continue;
        for (int k = 0; k < 64; k++) {
            struct sockaddr_in from; int fl = sizeof from;
            int r = recvfrom(t->s, buf, sizeof buf, 0, (struct sockaddr*)&from, &fl);
            if (r <= 0) break;
            if (g_st == ST_JOINED) { t->to = from; t->has_to = 1; }   // client: ENet's local address is learned from the first packet
            int er = S.Send(g_msgs, &t->id, buf, (uint32_t)r, 1 | 32, 0);   // unreliable + no Nagle + re-establish a dropped session
            if (er != 1 && g_senderr < 10) { g_senderr++; SLOG("Steam send error %d (%s)", er, who(t->sid)); }
        }
    }
}

// ---------- control messages ----------
static char* tok(char** p) {
    while (**p == ' ') (*p)++;
    if (!**p) return 0;
    char* s = *p; while (**p && **p != ' ') (*p)++;
    if (**p) *(*p)++ = 0;
    return s;
}
static void command(char* line) {
    char* p = line; char* c = tok(&p); if (!c) return;
    if (!strcmp(c, "host")) {
        char *sq = tok(&p), *port = tok(&p), *mx = tok(&p), *pr = tok(&p), *exe = tok(&p), *ver = tok(&p);
        if (!ver) return;
        leave(); g_seq = atoi(sq); g_enet_port = (unsigned short)atoi(port); g_proto = atoi(pr);
        snprintf(g_exe, sizeof g_exe, "%s", exe); snprintf(g_ver, sizeof g_ver, "%s", ver); snprintf(g_name, sizeof g_name, "%s", *p ? p : "Player");
        int maxp = atoi(mx); if (maxp < 2 || maxp > 8) maxp = 8;
        SteamAPICall_t call = S.CreateLobby(g_mm, 2 /* k_ELobbyTypePublic: searchable by code, friends can join */, maxp);
        if (!call) { say("hostfail %d %d Steam could not create a lobby.", C.net, g_seq); return; }
        set_call(call, 513, 16); g_st = ST_CREATING; SLOG("requesting a Steam lobby (ENet port %u)", g_enet_port);
    } else if (!strcmp(c, "join")) {
        char *sq = tok(&p), *pr = tok(&p), *kind = tok(&p), *val = tok(&p);
        if (!val) return;
        leave(); g_seq = atoi(sq); g_proto = atoi(pr);
        if (!strcmp(kind, "code")) {
            snprintf(g_code, sizeof g_code, "%s", val);
            S.AddStringFilter(g_mm, "bbr2", "1", 0); S.AddStringFilter(g_mm, "bbr2code", g_code, 0);
            S.AddDistanceFilter(g_mm, 3 /* worldwide */); S.AddCountFilter(g_mm, 4);
            SteamAPICall_t call = S.RequestLobbyList(g_mm);
            if (!call) { join_fail("Steam could not search for the lobby."); return; }
            set_call(call, 510, 4); g_st = ST_SEARCHING; SLOG("looking for code: %s", g_code);
        } else start_join(_strtoui64(val, 0, 10));
    } else if (!strcmp(c, "invite")) {
        if (g_lobby && (g_st == ST_HOSTING || g_st == ST_JOINED)) {
            if (S.OverlayOn(g_utils)) { S.InviteDialog(g_fr, g_lobby); SLOG("Steam invite window opened"); }
            else say("note %d The Steam overlay is off, so the invite window cannot open. Turn it on in Steam or share the lobby code %s.", C.net, g_code);
        } else say("note %d There is no Steam lobby to invite friends to.", C.net);
    } else if (!strcmp(c, "leave")) leave();
    else if (!strcmp(c, "quit")) { if (!C.inproc) { leave(); g_quit = 1; } }
}
static void ctl_read(void) {
    char b[800];
    for (int k = 0; k < 32; k++) {
        struct sockaddr_in from; int fl = sizeof from;
        int r = recvfrom(g_ctl, b, sizeof b - 1, 0, (struct sockaddr*)&from, &fl);
        if (r <= 0) break;
        if (from.sin_port != g_dll.sin_port) continue;   // the game side's socket only
        b[r] = 0; command(b);
    }
}

// ---------- startup ----------
static int steam_start(char* err, int cap) {
    HMODULE m = C.steam_dll;
    if (C.inproc) for (int i = 0; i < 600 && !(m = GetModuleHandleA("steam_api64.dll")); i++) Sleep(100);
    if (!m) { snprintf(err, cap, "%s", C.inproc ? "This copy of the game has no Steam connection." : "steam_api64.dll was not found (next to the game or in bbr2mod\\steam)."); return 0; }
    for (size_t i = 0; i < sizeof k_fn / sizeof k_fn[0]; i++) {
        *k_fn[i].p = (void*)GetProcAddress(m, k_fn[i].n);
        if (!*k_fn[i].p) { snprintf(err, cap, "steam_api64.dll has no %s (too old).", k_fn[i].n); return 0; }
    }
    if (C.inproc) {
        int i; for (i = 0; i < 1200 && !S.GetHSteamUser(); i++) Sleep(100);
        if (!S.GetHSteamUser()) { snprintf(err, cap, "The game did not start its Steam connection."); return 0; }
    } else {
        char e[1024] = ""; int r = S.InitFlat(e);
        if (r != 0) { snprintf(err, cap, "Steam could not start the Spacewar connection (%d: %s). Is Steam running and are you logged in?", r, e); return 0; }
    }
    g_mm = S.Mm(); g_fr = S.Fr(); g_user = S.User(); g_utils = S.Utils(); g_msgs = S.Msgs(); void* nu = S.NUtils();
    if (!g_mm || !g_fr || !g_user || !g_utils || !g_msgs || !nu) { snprintf(err, cap, "Steam interfaces are not available."); return 0; }
    S.InitRelay(nu);
    for (int i = 0; i < 3; i++) S.RegisterCallback(&g_cbo[i], g_cbo[i].id);
    g_me = S.MySteamID(g_user);
    return 1;
}
static void check_cmdline(void) {   // if an invite was clicked while the game was closed, Steam starts the game with "+connect_lobby <lobby>"
    const wchar_t* cl = GetCommandLineW(); const wchar_t* p = cl ? wcsstr(cl, L"+connect_lobby") : 0;
    if (!p) return;
    p += 14; while (*p == L' ' || *p == L'"' || *p == L'=') p++;
    unsigned long long id = _wcstoui64(p, 0, 10);
    if (id) { SLOG("lobby from the command line: %llu", id); say("invited %d %llu", C.net, id); }
}
DWORD svc_run(const SvcConfig* cfg) {
    C = *cfg; InitializeCriticalSection(&g_cqcs);
    WSADATA w; WSAStartup(MAKEWORD(2, 2), &w);
    g_ctl = udp_local(0); if (g_ctl == INVALID_SOCKET) return 1;
    g_dll = loopback(C.ctl_port);
    say("hello %d", C.net);
    char err[1200];
    if (!steam_start(err, sizeof err)) { say("fail %d %s", C.net, err); Sleep(200); return 2; }
    const char* pn = S.PersonaName(g_fr);
    say("ready %d %u %llu %s", C.net, S.AppID(g_utils), (unsigned long long)g_me, pn ? pn : "");
    if (C.inproc) check_cmdline();
    uint64_t t_cb = 0, t_par = 0, t_pend = 0, t_idle = 0;
    while (!g_quit) {
        fd_set rd; FD_ZERO(&rd); FD_SET(g_ctl, &rd);
        for (int i = 0; i < g_nt; i++) FD_SET(g_t[i].s, &rd);
        struct timeval tv = { 0, 1000 };
        int r = select(0, &rd, 0, 0, &tv);
        if (r > 0) { if (FD_ISSET(g_ctl, &rd)) ctl_read(); pump_local(&rd); }
        pump_steam();
        uint64_t now = ms();
        if (!C.inproc && now - t_cb >= 4) { t_cb = now; S.RunCallbacks(); }
        for (;;) {
            CbEv e; int have = 0;
            EnterCriticalSection(&g_cqcs); if (g_cqt != g_cqh) { e = g_cq[g_cqt]; g_cqt = (g_cqt + 1) % 64; have = 1; } LeaveCriticalSection(&g_cqcs);
            if (!have) break;
            on_cb(&e);
        }
        poll_call();
        if (g_np && now - t_pend >= 100) { t_pend = now; pend_check(); }
        if (now - t_idle >= 1000) {   // host: close a tunnel that has been silent for 60 s
            t_idle = now;
            if (g_st == ST_HOSTING) for (int i = 0; i < g_nt; i++) if (now - g_t[i].last > 60000) { tun_close(i); break; }
        }
        if (C.parent && now - t_par >= 500) { t_par = now; if (WaitForSingleObject(C.parent, 0) == WAIT_OBJECT_0) break; }
    }
    leave();
    if (!C.inproc) S.Shutdown();
    return 0;
}
