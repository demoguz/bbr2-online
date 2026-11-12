#include "repl.h"
static double rnd(NetSim* n) { n->rng = n->rng*1664525u + 1013904223u; return (n->rng >> 8) / 16777216.0; }
void netsim_reset(NetSim* n, double delay, double jitter, double loss) {
    memset(n, 0, sizeof *n); n->rng = 0x1234567u; n->delay = delay; n->jitter = jitter; n->loss = loss;
}
void netsim_send(NetSim* n, const Snap* s, double now) {
    if (rnd(n) < n->loss) return;
    double d = n->delay + n->jitter*(2.0*rnd(n) - 1.0); if (d < 0) d = 0;
    for (int i = 0; i < 128; i++) if (!n->q[i].used) { n->q[i].s = *s; n->q[i].arrive = now + d; n->q[i].used = 1; return; }
}
int netsim_recv(NetSim* n, double now, Snap* out) {
    for (int i = 0; i < 128; i++) if (n->q[i].used && n->q[i].arrive <= now) { *out = n->q[i].s; n->q[i].used = 0; return 1; }
    return 0;
}
