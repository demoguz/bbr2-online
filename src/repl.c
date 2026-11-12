#include "repl.h"
#include "game.h"
#include <math.h>
static float dot3(const float* a, const float* b) { return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]; }
static void cross3(const float* a, const float* b, float* o) {
    float x = a[1]*b[2] - a[2]*b[1], y = a[2]*b[0] - a[0]*b[2], z = a[0]*b[1] - a[1]*b[0];
    o[0] = x; o[1] = y; o[2] = z;
}
static void q_mul(const float* a, const float* b, float* o) {   // o = a*b, (x,y,z,w)
    float x = a[3]*b[0] + a[0]*b[3] + a[1]*b[2] - a[2]*b[1];
    float y = a[3]*b[1] - a[0]*b[2] + a[1]*b[3] + a[2]*b[0];
    float z = a[3]*b[2] + a[0]*b[1] - a[1]*b[0] + a[2]*b[3];
    float w = a[3]*b[3] - a[0]*b[0] - a[1]*b[1] - a[2]*b[2];
    o[0] = x; o[1] = y; o[2] = z; o[3] = w;
}
static void q_norm(float* q) {
    float l = sqrtf(q[0]*q[0] + q[1]*q[1] + q[2]*q[2] + q[3]*q[3]);
    if (l < 1e-9f) { q[0] = q[1] = q[2] = 0; q[3] = 1; return; }
    for (int i = 0; i < 4; i++) q[i] /= l;
}
void q_rot(const float q[4], const float v[3], float o[3]) {   // o = q v q^-1
    float t[3], u[3];
    cross3(q, v, t); t[0] *= 2; t[1] *= 2; t[2] *= 2;
    cross3(q, t, u);
    for (int i = 0; i < 3; i++) o[i] = v[i] + q[3]*t[i] + u[i];
}
void q_inv_rot(const float q[4], const float v[3], float o[3]) {
    float c[4] = { -q[0], -q[1], -q[2], q[3] };
    q_rot(c, v, o);
}
static void q_from_rotvec(const float* r, float* q) {          // r = axis*angle
    float a = sqrtf(dot3(r, r));
    if (a < 1e-6f) { q[0] = r[0]*0.5f; q[1] = r[1]*0.5f; q[2] = r[2]*0.5f; q[3] = 1; q_norm(q); return; }
    float s = sinf(a*0.5f) / a;
    q[0] = r[0]*s; q[1] = r[1]*s; q[2] = r[2]*s; q[3] = cosf(a*0.5f);
}
void hist_reset(SnapHist* h) { h->n = 0; h->head = 0; }
void hist_push(SnapHist* h, const Snap* s) {
    if (h->n) { const Snap* last = &h->h[(h->head + 255) & 255]; if (s->t <= last->t) return; }
    h->h[h->head] = *s; h->head = (h->head + 1) & 255; if (h->n < 256) h->n++;
}
const Snap* hist_pick(const SnapHist* h, double t_max) {
    for (int i = 1; i <= h->n; i++) { const Snap* s = &h->h[(h->head - i) & 255]; if (s->t <= t_max) return s; }
    return 0;
}
void repl_target(const Snap* s, double now, const ReplCfg* c, float q[4], float p[3], float v[3], float w[3], float* age_out) {
    float age = (float)(now - s->t - c->trail);
    if (age < 0) age = 0;
    if (age > c->max_extrap) age = c->max_extrap;
    // extrapolation along an arc for a car rotating with angular velocity: the velocity vector rotates too
    float r[3] = { s->w[0]*age, s->w[1]*age, s->w[2]*age }, dq[4];
    q_from_rotvec(r, dq);
    q_rot(dq, s->v, v);
    for (int i = 0; i < 3; i++) { p[i] = s->p[i] + 0.5f*(s->v[i] + v[i])*age; w[i] = s->w[i]; }
    q_mul(dq, s->q, q); q_norm(q);
    if (c->offset[0] != 0 || c->offset[1] != 0 || c->offset[2] != 0) {
        float ow[3], wv[3];
        q_rot(q, c->offset, ow); cross3(w, ow, wv);
        for (int i = 0; i < 3; i++) { p[i] += ow[i]; v[i] += wv[i]; }
    }
    if (age_out) *age_out = age;
}
static float gain(float g, float dt) {   // convert a rate given for 60 Hz to the real dt
    if (g <= 0) return 0;
    if (g >= 1) return 1;
    return 1.0f - powf(1.0f - g, dt*60.0f);
}
void repl_apply(void* veh, const Snap* s, double now, float dt, const ReplCfg* c, ReplStats* st) {
    void* a = veh_actor(veh);
    if (!a) return;
    float tq[4], tp[3], tv[3], tw[3], age;
    repl_target(s, now, c, tq, tp, tv, tw, &age);
    PxT cur; float cv[3], cw[3];
    px_get_pose(a, &cur); px_get_lin(a, cv); px_get_ang(a, cw);
    float e[3] = { tp[0]-cur.p[0], tp[1]-cur.p[1], tp[2]-cur.p[2] };
    float d = sqrtf(dot3(e, e));
    float qd = cur.q[0]*tq[0] + cur.q[1]*tq[1] + cur.q[2]*tq[2] + cur.q[3]*tq[3];
    PxT nt; float nv[3], nw[3];
    bool snap = !(d <= c->snap_dist) || fabsf(qd) < 0.5f;   // NaN da buraya duser
    if (snap) {
        memcpy(nt.q, tq, 16); memcpy(nt.p, tp, 12); memcpy(nv, tv, 12); memcpy(nw, tw, 12);
    } else {
        float gp = gain(c->pos_gain, dt), gv = gain(c->vel_gain, dt), gr = gain(c->rot_gain, dt), ga = gain(c->ang_gain, dt);
        float sgn = qd < 0 ? -1.0f : 1.0f;
        for (int i = 0; i < 4; i++) nt.q[i] = cur.q[i] + (sgn*tq[i] - cur.q[i])*gr;
        q_norm(nt.q);
        for (int i = 0; i < 3; i++) {
            nt.p[i] = cur.p[i] + e[i]*gp;
            nv[i] = cv[i] + (tv[i] - cv[i])*gv;
            nw[i] = cw[i] + (tw[i] - cw[i])*ga;
        }
    }
    px_set_pose(a, &nt); px_set_lin(a, nv); px_set_ang(a, nw);
    if (c->copy_ctl) memcpy((char*)veh + VEH_CTL, s->ctl, VEH_CTL_SIZE);
    if (st) { st->n++; st->sum_err += d; st->sum_age += age; if (d > st->max_err) st->max_err = d; if (snap) st->snaps++; }
}
