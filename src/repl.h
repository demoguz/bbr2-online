// Vehicle state replication: snapshots, extrapolation and soft correction. Portable C.
#pragma once
#include "core.h"
typedef struct { double t; float q[4], p[3], v[3], w[3]; uint8_t ctl[24]; } Snap;
typedef struct {
    float pos_gain, vel_gain, rot_gain, ang_gain; // per-frame blend rates for 60 Hz
    float snap_dist;      // error larger than this: teleport
    float max_extrap;     // longest extrapolation (s)
    float trail;          // test: follow the owner this many seconds behind
    float offset[3];      // test: offset in the owner's local axes (m)
    int copy_ctl;
} ReplCfg;
typedef struct { double sum_err, sum_age; float max_err; int n, snaps; } ReplStats;
typedef struct { Snap h[256]; int n, head; } SnapHist;
void hist_reset(SnapHist* h);
void hist_push(SnapHist* h, const Snap* s);                 // drops old/duplicate packets
const Snap* hist_pick(const SnapHist* h, double t_max);     // newest with t <= t_max
void repl_target(const Snap* s, double now, const ReplCfg* c, float q[4], float p[3], float v[3], float w[3], float* age);
void repl_apply(void* veh, const Snap* s, double now, float dt, const ReplCfg* c, ReplStats* st);
// netsim.c: loopback line with simulated delay/jitter/loss (to be replaced by real UDP later)
typedef struct { Snap s; double arrive; int used; } Pkt;
typedef struct { Pkt q[128]; uint32_t rng; double delay, jitter, loss; } NetSim;
void netsim_reset(NetSim* n, double delay, double jitter, double loss);
void netsim_send(NetSim* n, const Snap* s, double now);
int  netsim_recv(NetSim* n, double now, Snap* out);
// vector/quaternion
void q_rot(const float q[4], const float v[3], float o[3]);
void q_inv_rot(const float q[4], const float v[3], float o[3]);
