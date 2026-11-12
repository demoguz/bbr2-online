// Steam network, game side: starts the services, sends commands, turns replies into events. See steamlink.h, steamsvc.h
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include "core.h"
#include "game.h"
#include "session.h"
#include "steamsvc.h"
#include "steamlink.h"
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif
enum { SV_OFF, SV_STARTING, SV_READY, SV_FAILED };
typedef struct { int st; char err[320]; char pend[600]; int pend_kind, pend_seq; uint64_t t0; HANDLE proc; struct sockaddr_in addr; int has_addr; } Svc;
static Svc g_sv[2];
static SOCKET g_sock = INVALID_SOCKET; static unsigned short g_port;
static int g_seq = 1, g_cur_seq, g_cur_net = -1, g_cur_kind;   // active request: 1 host, 2 join
static SlEvent g_q[16]; static int g_qh, g_qt;
static SvcConfig g_acfg;
static const char* k_net[2] = { "Steam", "Spacewar" };
static const char* k_alpha = "ABCDEFGHJKMNPQRSTUVWXYZ23456789";

int sl_read_mode(char* name, int cap) {
    char p[MAX_PATH + 32], v[32] = "game"; snprintf(p, sizeof p, "%sonline.ini", mod_dir());
    FILE* f = fopen(p, "r");
    if (f) { char line[256]; while (fgets(line, sizeof line, f)) if (!strncmp(line, "steam=", 6)) sscanf(line + 6, "%31[^ \t\r\n#]", v); fclose(f); }
    if (name) snprintf(name, cap, "%s", v);
    if (!_stricmp(v, "off") || !_stricmp(v, "0") || !_stricmp(v, "no")) return -1;
    if (!_stricmp(v, "spacewar") || !_stricmp(v, "480")) return 1;
    return 0;
}
int sl_classify(const char* in, int* net, char* norm, int cap) {
    char t[80]; int n = 0;
    for (const char* p = in; *p && n < 79; p++) if (*p != ' ') t[n++] = *p;
    t[n] = 0;
    if (!_strnicmp(t, "steam:", 6)) {
        const char* d = t + 6; if (!*d) return 2;
        for (const char* q = d; *q; q++) if (*q < '0' || *q > '9') return 2;
        *net = sl_read_mode(0, 0) == 1 ? 1 : 0; snprintf(norm, cap, "id %s", d); return 1;
    }
    if (n == 5 && !strchr(t, '.') && !strchr(t, ':')) {
        char c[6];
        for (int i = 0; i < 5; i++) { char ch = t[i]; if (ch >= 'a' && ch <= 'z') ch -= 32; if (!strchr(k_alpha, ch)) return 2; c[i] = ch; }
        c[5] = 0;
        if (strchr("ABCDEFGHJKM", c[0])) *net = 0; else if (strchr("NPQRSTUVWXYZ", c[0])) *net = 1; else return 2;
        snprintf(norm, cap, "code %s", c); return 1;
    }
    return 0;
}
static void push(int type, int net, int port, const char* a, const char* b) {
    int nh = (g_qh + 1) % 16; if (nh == g_qt) return;
    SlEvent* e = &g_q[g_qh]; e->type = type; e->net = net; e->port = port;
    snprintf(e->a, sizeof e->a, "%s", a ? a : ""); snprintf(e->b, sizeof e->b, "%s", b ? b : ""); g_qh = nh;
}
static void sock_init(void) {
    if (g_sock != INVALID_SOCKET) return;
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP); if (s == INVALID_SOCKET) return;
    struct sockaddr_in a; memset(&a, 0, sizeof a); a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(s, (struct sockaddr*)&a, sizeof a)) { closesocket(s); return; }
    int l = sizeof a; getsockname(s, (struct sockaddr*)&a, &l); g_port = ntohs(a.sin_port);
    u_long nb = 1; ioctlsocket(s, FIONBIO, &nb);
    BOOL off = FALSE; DWORD br = 0; WSAIoctl(s, SIO_UDP_CONNRESET, &off, sizeof off, 0, 0, &br, 0, 0);
    g_sock = s;
}
static void raw_send(int i, const char* s) { if (g_sv[i].has_addr) sendto(g_sock, s, (int)strlen(s), 0, (struct sockaddr*)&g_sv[i].addr, sizeof g_sv[i].addr); }
static void fail_req(int i, int kind, int seq, const char* why) {
    if (seq != g_cur_seq || !kind) return;
    push(kind == 1 ? SLE_HOSTFAIL : SLE_JOINFAIL, i, 0, "", why);
}
static void svc_failed(int i, const char* why) {
    Svc* s = &g_sv[i]; s->st = SV_FAILED; snprintf(s->err, sizeof s->err, "%s", why);
    LOG("steam[%s]: unavailable: %s", k_net[i], why);
    if (s->pend[0]) { fail_req(i, s->pend_kind, s->pend_seq, why); s->pend[0] = 0; }
    else if (g_cur_net == i) fail_req(i, g_cur_kind, g_cur_seq, why);
}
static DWORD WINAPI a_thread(void* p) { return svc_run((SvcConfig*)p); }
static void svc_start(int i) {
    Svc* s = &g_sv[i];
    if (s->st == SV_STARTING || s->st == SV_READY) return;
    if (i == 0 && s->st == SV_FAILED) return;   // the game's Steam connection is tried once
    sock_init();
    if (g_sock == INVALID_SOCKET) { svc_failed(i, "Could not open a local network socket."); return; }
    s->has_addr = 0; s->err[0] = 0; s->t0 = now_ms();
    if (i == 0) {
        memset(&g_acfg, 0, sizeof g_acfg); g_acfg.net = SVC_NET_GAME; g_acfg.inproc = 1; g_acfg.ctl_port = g_port;
        HANDLE h = CreateThread(0, 0, a_thread, &g_acfg, 0, 0);
        if (!h) { svc_failed(i, "Could not start the Steam thread."); return; }
        CloseHandle(h); s->st = SV_STARTING; LOG("steam[Steam]: waiting for the game's Steam connection");
    } else {
        char dir[MAX_PATH + 32], exe[MAX_PATH + 64], cmd[MAX_PATH + 160];
        snprintf(dir, sizeof dir, "%ssteam", mod_dir()); snprintf(exe, sizeof exe, "%s\\bbr2steam.exe", dir);
        if (GetFileAttributesA(exe) == INVALID_FILE_ATTRIBUTES) { svc_failed(i, "bbr2mod\\steam\\bbr2steam.exe is missing. Reinstall the mod."); return; }
        snprintf(cmd, sizeof cmd, "\"%s\" --ctl %u --parent %lu", exe, (unsigned)g_port, (unsigned long)GetCurrentProcessId());
        STARTUPINFOA si; memset(&si, 0, sizeof si); si.cb = sizeof si; PROCESS_INFORMATION pi;
        if (!CreateProcessA(exe, cmd, 0, 0, FALSE, CREATE_NO_WINDOW, 0, dir, &si, &pi)) {
            char e[160]; snprintf(e, sizeof e, "Could not start the Spacewar helper (error %lu).", (unsigned long)GetLastError()); svc_failed(i, e); return; }
        CloseHandle(pi.hThread); if (s->proc) CloseHandle(s->proc); s->proc = pi.hProcess; s->st = SV_STARTING;
        LOG("steam[Spacewar]: helper started (pid %lu)", (unsigned long)pi.dwProcessId);
    }
}
static void svc_cmd(int i, int kind, const char* line) {
    Svc* s = &g_sv[i];
    if (s->st == SV_OFF || (s->st == SV_FAILED && i == 1)) svc_start(i);
    if (s->st == SV_FAILED) { fail_req(i, kind, g_cur_seq, s->err); return; }
    if (s->st == SV_READY) raw_send(i, line);
    else { snprintf(s->pend, sizeof s->pend, "%s", line); s->pend_kind = kind; s->pend_seq = g_cur_seq; }
}
void sl_init(void) {
    sock_init();
    char nm[32]; int m = sl_read_mode(nm, sizeof nm);
    LOG("steam: setting steam=%s", nm);
    if (m >= 0) svc_start(0);   // the game's Steam network is always listened to, for invites and joining by code
    if (m == 1) svc_start(1);   // spacewar: the helper runs from the start (so Spacewar invites arrive too)
}
void sl_host(int net, int enet_port, int maxp, const char* name) {
    g_cur_seq = ++g_seq; g_cur_net = net; g_cur_kind = 1;
    char nm[40]; snprintf(nm, sizeof nm, "%s", name && name[0] ? name : "Player");
    char line[300]; snprintf(line, sizeof line, "host %d %d %d %d %08X %s %s", g_cur_seq, enet_port, maxp, NET_PROTO, (unsigned)BUILD_TS, BBR2NET_SEMVER, nm);
    LOG("steam[%s]: creating a lobby", k_net[net]);
    svc_cmd(net, 1, line);
}
void sl_join(int net, const char* norm) {
    g_cur_seq = ++g_seq; g_cur_net = net; g_cur_kind = 2;
    if (sl_read_mode(0, 0) < 0) { push(SLE_JOINFAIL, net, 0, "", "Steam networking is turned off (steam=off in bbr2mod\\online.ini)."); return; }
    char line[200]; snprintf(line, sizeof line, "join %d %d %s", g_cur_seq, NET_PROTO, norm);
    LOG("steam[%s]: joining (%s)", k_net[net], norm);
    svc_cmd(net, 2, line);
}
void sl_invite(void) { if (g_cur_net >= 0 && g_sv[g_cur_net].st == SV_READY) raw_send(g_cur_net, "invite"); }
void sl_leave(void) {
    int i = g_cur_net; g_cur_seq = ++g_seq; g_cur_net = -1; g_cur_kind = 0;
    g_sv[0].pend[0] = g_sv[1].pend[0] = 0;
    if (i < 0) return;
    if (g_sv[i].st == SV_READY) raw_send(i, "leave");
    if (i == 1 && sl_read_mode(0, 0) != 1 && g_sv[1].st == SV_READY) {   // close the helper unless the setting is spacewar (so no "playing Spacewar" status stays)
        raw_send(1, "quit"); g_sv[1].st = SV_OFF; LOG("steam[Spacewar]: closing the helper");
    }
}
static char* tok(char** p) {
    while (**p == ' ') (*p)++;
    if (!**p) return 0;
    char* s = *p; while (**p && **p != ' ') (*p)++;
    if (**p) *(*p)++ = 0;
    return s;
}
static void handle(char* b, const struct sockaddr_in* from) {
    char* p = b; char* cmd = tok(&p); char* ns = tok(&p); if (!cmd || !ns) return;
    int i = atoi(ns); if (i < 0 || i > 1) return;
    Svc* s = &g_sv[i];
    if (!strcmp(cmd, "hello")) { s->addr = *from; s->has_addr = 1; return; }
    if (!s->has_addr || from->sin_port != s->addr.sin_port) return;
    if (!strcmp(cmd, "log")) { LOG("steam[%s] %s", k_net[i], p); return; }
    if (!strcmp(cmd, "ready")) {
        char* app = tok(&p); char* sid = tok(&p); s->st = SV_READY;
        LOG("steam[%s]: ready (AppID %s, %s, %s)", k_net[i], app ? app : "?", p, sid ? sid : "?");
        if (s->pend[0]) { raw_send(i, s->pend); s->pend[0] = 0; }
        return;
    }
    if (!strcmp(cmd, "fail")) { svc_failed(i, p); return; }
    if (!strcmp(cmd, "invited")) { char* lob = tok(&p); if (lob) { char a[64]; snprintf(a, sizeof a, "id %s", lob); push(SLE_INVITED, i, 0, a, p); } return; }
    if (!strcmp(cmd, "note")) { push(SLE_NOTE, i, 0, "", p); return; }
    char* sq = tok(&p);
    if (!sq || atoi(sq) != g_cur_seq) { LOG("steam[%s]: old reply ignored (%s)", k_net[i], cmd); return; }
    if (!strcmp(cmd, "hosted")) { char* code = tok(&p); push(SLE_HOSTED, i, 0, code ? code : "", p); }
    else if (!strcmp(cmd, "hostfail")) push(SLE_HOSTFAIL, i, 0, "", p);
    else if (!strcmp(cmd, "joined")) { char* port = tok(&p); char* code = tok(&p); push(SLE_JOINED, i, port ? atoi(port) : 0, code && strcmp(code, "-") ? code : "", p); }
    else if (!strcmp(cmd, "joinfail")) push(SLE_JOINFAIL, i, 0, "", p);
}
int sl_poll(SlEvent* out) {
    if (g_sock != INVALID_SOCKET) {
        char b[800];
        for (int k = 0; k < 64; k++) {
            struct sockaddr_in from; int fl = sizeof from;
            int r = recvfrom(g_sock, b, sizeof b - 1, 0, (struct sockaddr*)&from, &fl);
            if (r <= 0) break;
            b[r] = 0; handle(b, &from);
        }
    }
    for (int i = 0; i < 2; i++) {
        Svc* s = &g_sv[i];
        if (s->st == SV_STARTING && now_ms() - s->t0 > (i ? 20000u : 150000u))
            svc_failed(i, i ? "The Spacewar helper did not answer. Is Steam running?" : "The game's Steam connection did not become ready.");
        if (i == 1 && s->proc && WaitForSingleObject(s->proc, 0) == WAIT_OBJECT_0) {
            DWORD code = 0; GetExitCodeProcess(s->proc, &code); CloseHandle(s->proc); s->proc = 0;
            LOG("steam[Spacewar]: helper exited (code %lu)", (unsigned long)code);
            if (s->st == SV_READY || s->st == SV_STARTING) svc_failed(i, "The Spacewar helper stopped.");
        }
    }
    if (g_qt == g_qh) return 0;
    *out = g_q[g_qt]; g_qt = (g_qt + 1) % 16; return 1;
}
