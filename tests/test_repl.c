// Exercises repl.c + netsim.c with a fake PhysX actor: the owner drives a circle, the puppet follows the delayed stream.
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "../src/repl.h"
#include "../src/game.h"
typedef struct { void** vt; PxT t; float v[3], w[3]; } Actor;
static PxT* MSABI gp(void* s, PxT* o) { *o = ((Actor*)s)->t; return o; }
static void MSABI sp(void* s, const PxT* t, bool k) { (void)k; ((Actor*)s)->t = *t; }
static float* MSABI gl(void* s, float* o) { (void)o; return ((Actor*)s)->v; }
static float* MSABI ga(void* s, float* o) { (void)o; return ((Actor*)s)->w; }
static void MSABI sl(void* s, const float* v, bool k) { (void)k; memcpy(((Actor*)s)->v, v, 12); }
static void MSABI sa(void* s, const float* v, bool k) { (void)k; memcpy(((Actor*)s)->w, v, 12); }
static void* vt[64];
static void owner(double t, Snap* s) {   // 40 m radius, 30 m/s; hard braking between 8 and 12 s (10 m/s)
    double sp_ = (t > 8 && t < 12) ? 10 : 30, R = 40; static double th = 0, lt = 0;
    double w = sp_ / R; th += w * (t - lt); lt = t;
    s->t = t; s->p[0] = R*cos(th); s->p[1] = R*sin(th); s->p[2] = 0;
    s->v[0] = -sp_*sin(th); s->v[1] = sp_*cos(th); s->v[2] = 0;
    s->w[0] = s->w[1] = 0; s->w[2] = w;
    s->q[0] = s->q[1] = 0; s->q[2] = sin(th/2); s->q[3] = cos(th/2);
    memset(s->ctl, 0, 24);
}
int main(int argc, char** argv) {
    double fps = argc > 1 ? atof(argv[1]) : 60, delay = argc > 2 ? atof(argv[2]) : 0.1, loss = argc > 3 ? atof(argv[3]) : 0.05;
    vt[PX_GET_POSE] = gp; vt[PX_SET_POSE] = sp; vt[PX_GET_LINVEL] = gl; vt[PX_SET_LINVEL] = sl; vt[PX_GET_ANGVEL] = ga; vt[PX_SET_ANGVEL] = sa;
    static uint8_t veh[0x800]; Actor a = { vt, { {0,0,0,1}, {40,0,0} }, {0,0,0}, {0,0,0} };
    *(void**)(veh + VEH_ACTOR) = &a;
    ReplCfg c = { 0.15f, 0.4f, 0.15f, 0.4f, 6, 0.5f, 0, {0,0,0}, 1 };
    NetSim net; netsim_reset(&net, delay, 0.02, loss); SnapHist h; hist_reset(&h);
    double dt = 1/fps, last = -1, sum = 0, mx = 0, sumr = 0; int n = 0, snaps = 0; ReplStats st = {0};
    for (double t = dt; t < 30; t += dt) {
        Snap s, r; owner(t, &s);
        if (t > 2) {
            double e = sqrt(pow(a.t.p[0]-s.p[0],2) + pow(a.t.p[1]-s.p[1],2) + pow(a.t.p[2]-s.p[2],2));
            double qd = fabs(a.t.q[2]*s.q[2] + a.t.q[3]*s.q[3]); if (qd > 1) qd = 1;
            sum += e; sumr += 2*acos(qd)*57.3; n++; if (e > mx) mx = e;
            if (fabs(t - 20.25) < dt/2) printf("  error 0.25 s after the hit %.2f m\n", e);
        }
        if (t - last >= 1/30.0 - 1e-4) { last = t; netsim_send(&net, &s, t); }
        while (netsim_recv(&net, t, &r)) hist_push(&h, &r);
        const Snap* cur = hist_pick(&h, t);
        if (cur) repl_apply(veh, cur, t, (float)dt, &c, &st);
        if (fabs(t - 20) < dt/2) { a.v[0] += 6; a.v[2] += 3; }       // at 20 s: collision impulse
        for (int i = 0; i < 3; i++) a.t.p[i] += a.v[i]*(float)dt;      // fake physics step
        float hw = a.w[2]*(float)dt/2, qz = a.t.q[2], qw = a.t.q[3];
        a.t.q[2] = qz*cosf(hw) + qw*sinf(hw); a.t.q[3] = qw*cosf(hw) - qz*sinf(hw);
    }
    snaps = st.snaps;
    printf("fps=%.0f delay=%.0fms loss=%.0f%%: position error avg=%.3f m max=%.3f m, heading error avg=%.2f deg, teleports=%d\n",
           fps, delay*1000, loss*100, sum/n, mx, sumr/n, snaps);
    return 0;
}
