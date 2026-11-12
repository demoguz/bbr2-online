// A1 shadow car test: one opponent becomes a puppet that follows the local player's delayed/lossy state stream.
// No real network; netsim exercises the same replication code (repl.c) on one PC.
#include <math.h>
#include "core.h"
#include "game.h"
#include "repl.h"
static void *g_hself, *g_hveh, *g_puppet;
static void* g_ai[16]; static int g_nai;
static double g_t, g_last_send, g_last_stat, g_last_diag;
static uint64_t g_last_ms;
static int g_on, g_pick, g_sent, g_recv;
static double g_send_dt;
static ReplCfg g_rc; static ReplStats g_st;
static NetSim g_net; static SnapHist g_hist;
static void reset_race(void* self, void* veh) {
    cfg_load();
    g_hself = self; g_hveh = veh; g_puppet = 0; g_nai = 0;
    g_t = 0; g_last_send = -1; g_last_stat = 0; g_last_diag = 0; g_sent = g_recv = 0;
    g_on = cfg_num("shadow", 1) != 0;
    g_pick = (int)cfg_num("shadow_ai", 0);
    double hz = cfg_num("send_hz", 30); g_send_dt = hz > 0 ? 1.0/hz : 1.0/30;
    g_rc.pos_gain = (float)cfg_num("pos_gain", 0.15); g_rc.vel_gain = (float)cfg_num("vel_gain", 0.4);
    g_rc.rot_gain = (float)cfg_num("rot_gain", 0.15); g_rc.ang_gain = (float)cfg_num("ang_gain", 0.4);
    g_rc.snap_dist = (float)cfg_num("snap_dist", 6); g_rc.max_extrap = (float)cfg_num("max_extrap", 0.5);
    g_rc.trail = (float)cfg_num("trail_s", 0); g_rc.copy_ctl = cfg_num("copy_ctl", 1) != 0;
    cfg_vec3("offset", g_rc.offset, 3.5f, 0, 0);
    netsim_reset(&g_net, cfg_num("delay_ms", 100)/1000.0, cfg_num("jitter_ms", 20)/1000.0, cfg_num("loss_pct", 5)/100.0);
    hist_reset(&g_hist); memset(&g_st, 0, sizeof g_st);
    LOG("--- race: human car %p, shadow=%d ai=%d delay=%.0fms jitter=%.0fms loss=%.0f%% hz=%.0f offset=%.1f,%.1f,%.1f trail=%.2f",
        veh, g_on, g_pick, g_net.delay*1000, g_net.jitter*1000, g_net.loss*100, 1.0/g_send_dt,
        g_rc.offset[0], g_rc.offset[1], g_rc.offset[2], g_rc.trail);
}
void shadow_human(void* self, void* veh, float dt) {
    uint64_t ms = now_ms();
    if (self != g_hself || veh != g_hveh || ms - g_last_ms > 500) reset_race(self, veh);
    g_last_ms = ms;
    g_t += dt;
    void* a = veh_actor(veh);
    if (!g_on || !a) return;
    Snap s; PxT t;
    s.t = g_t; px_get_pose(a, &t); memcpy(s.q, t.q, 16); memcpy(s.p, t.p, 12);
    px_get_lin(a, s.v); px_get_ang(a, s.w);
    memcpy(s.ctl, (char*)veh + VEH_CTL, VEH_CTL_SIZE);
    if (g_t - g_last_send >= g_send_dt - 1e-4) { g_last_send = g_t; g_sent++; netsim_send(&g_net, &s, g_t); }
    if (g_t - g_last_diag >= 5.0) {   // axis diagnostics: velocity in local axes -> forward axis; control block dump
        g_last_diag = g_t;
        float lv[3]; q_inv_rot(s.q, s.v, lv);
        const uint8_t* c = s.ctl; const float* f = (const float*)c;
        LOG("diag t=%.1f pos=%.1f,%.1f,%.1f local_vel=%.1f,%.1f,%.1f ctl f=%.2f,%.2f,%.2f,%.2f b=%02x %02x %02x %02x %02x %02x %02x %02x",
            g_t, s.p[0], s.p[1], s.p[2], lv[0], lv[1], lv[2], f[0], f[1], f[2], f[3],
            c[16], c[17], c[18], c[19], c[20], c[21], c[22], c[23]);
    }
}
int shadow_ai(void* self, void* veh, float dt) {
    (void)self;
    if (!g_on || !g_hveh) return 0;
    if (!g_puppet) {
        int i; for (i = 0; i < g_nai; i++) if (g_ai[i] == veh) break;
        if (i == g_nai && g_nai < 16) { g_ai[g_nai++] = veh; if (i == g_pick) { g_puppet = veh; LOG("puppet chosen: car %p (ai #%d)", veh, i); } }
    }
    if (veh != g_puppet) return 0;
    Snap s;
    while (netsim_recv(&g_net, g_t, &s)) { g_recv++; hist_push(&g_hist, &s); }
    const Snap* cur = hist_pick(&g_hist, g_t - g_rc.trail);
    if (!cur) return 0;      // the AI drives until the first packet arrives
    repl_apply(veh, cur, g_t, dt, &g_rc, &g_st);
    if (g_t - g_last_stat >= 1.0 && g_st.n) {
        LOG("t=%6.1f error avg=%.3f m max=%.3f m teleports=%d age=%.0f ms packets %d/%d",
            g_t, g_st.sum_err/g_st.n, g_st.max_err, g_st.snaps, 1000.0*g_st.sum_age/g_st.n, g_recv, g_sent);
        g_last_stat = g_t; memset(&g_st, 0, sizeof g_st);
    }
    return 1;
}
