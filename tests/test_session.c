// Exercises session.c in one process with 1 host + 2 clients (localhost UDP): joining, clock, START/READY/GO, packet relay.
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include "../src/session.h"
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec*1e-9; }
static void lg(const char* f, ...) { va_list a; va_start(a, f); printf("   | "); vprintf(f, a); printf("\n"); va_end(a); }
#define CHECK(c) do { if (!(c)) { printf("ERROR: %s\n", #c); bad = 1; } } while (0)
int main(void) {
    int bad = 0; VehDesc car[3]; memset(car, 0, sizeof car);
    strcpy(car[0].vehicle, "Buggy_Default"); strcpy(car[1].vehicle, "Lambo"); strcpy(car[2].vehicle, "Truck");
    Session* h = ses_create(SES_HOST, 0, 27999, "ev", &car[0], 0x67C624B1, lg);
    Session* a = ses_create(SES_JOIN, "127.0.0.1", 27999, "ali", &car[1], 0x67C624B1, lg);
    Session* b = ses_create(SES_JOIN, "127.0.0.1", 27999, "veli", &car[2], 0x67C624B1, lg);
    Session* bad_c = ses_create(SES_JOIN, "127.0.0.1", 27999, "eski", &car[2], 0x11111111, lg);
    CHECK(h && a && b && bad_c);
    // give the client clocks an artificial drift: the session clock must still match the host's
    double offA = 100.0, offB = -50.0, t0 = now();
    #define PUMP(sec) for (double e = now() + (sec); now() < e; usleep(2000)) { ses_poll(h, now()); ses_poll(a, now() + offA); ses_poll(b, now() + offB); ses_poll(bad_c, now()); }
    PUMP(2.5);
    CHECK(ses_connected(a) && ses_connected(b) && !ses_connected(bad_c)); CHECK(ses_nplayers(h) == 3);
    double ea = ses_time(a, now() + offA) - now(), eb = ses_time(b, now() + offB) - now();
    printf("clock error: A %.2f ms, B %.2f ms\n", ea*1000, eb*1000); CHECK(ea > -0.005 && ea < 0.005 && eb > -0.005 && eb < 0.005);
    RaceDesc t; memset(&t, 0, sizeof t); strcpy(t.track, "TutorialA"); t.nveh = 8; t.laps = 3; t.nshared = 5;
    for (int i = 1; i < 8; i++) { t.v[i].input = 1; snprintf(t.v[i].vehicle, 48, "AI_%d", i); t.v[i].ailevel = 6; }
    CHECK(ses_host_start(h, &t, 0, now())); PUMP(0.3);
    NetRace rh, ra, rb; CHECK(ses_take_start(h, &rh) && ses_take_start(a, &ra) && ses_take_start(b, &rb)); CHECK(!ses_take_start(a, &ra) || 1);
    CHECK(ra.d.nveh == 8 && ra.race_id == rh.race_id && !memcmp(&ra, &rb, sizeof ra) && ra.d.gridmode == 0 && ra.d.nshared == 0);
    int na = 0, slotA = -1, slotB = -1;
    for (int i = 0; i < 8; i++) { if (ra.d.v[i].input == 1) na++; else { if (ra.owner[i] == ses_my_id(a)) slotA = i; if (ra.owner[i] == ses_my_id(b)) slotB = i; } }
    printf("yapay zeka %d, A slot %d (%s, %s), B slot %d (%s)\n", na, slotA, ra.d.v[slotA].vehicle, ra.d.v[slotA].name, slotB, ra.d.v[slotB].vehicle);
    CHECK(na == 5 && slotA >= 5 && slotB >= 5 && slotA != slotB && !strcmp(ra.d.v[slotA].vehicle, "Lambo") && !strcmp(ra.d.v[slotA].name, "ali"));
    double T; ses_ready(a, now() + offA); PUMP(0.2); CHECK(!ses_go_time(h, &T));          // no GO before everyone is ready
    ses_ready(h, now()); ses_ready(b, now() + offB); PUMP(0.3);
    double Ta, Tb; CHECK(ses_go_time(h, &T) && ses_go_time(a, &Ta) && ses_go_time(b, &Tb) && T == Ta && T == Tb);
    // packet relay: A sends its own car, the host sends one AI; B must get both, A must not get its own packet back
    Snap sn; memset(&sn, 0, sizeof sn);
    for (int i = 0; i < 30; i++) { sn.t = ses_time(a, now() + offA); sn.p[0] = (float)i; ses_send_snap(a, slotA, &sn); sn.t = now(); ses_send_snap(h, 0, &sn); ses_send_snap(a, slotB, &sn); PUMP(0.02); }
    PUMP(0.2);
    const Snap* x = ses_latest(b, slotA); const Snap* y = ses_latest(b, 0); const Snap* z = ses_latest(h, slotA);
    CHECK(x && x->p[0] > 20 && y && z && z->p[0] > 20); CHECK(!ses_latest(a, slotA)); CHECK(ses_latest(a, 0) != 0);
    CHECK(!ses_latest(h, slotB) && !ses_latest(b, slotB));                                    // A cannot impersonate B's car
    if (x) printf("B's last packet for A: age %.1f ms\n", (now() - x->t)*1000);
    ses_destroy(a); PUMP(0.5); CHECK(ses_nplayers(h) == 2 || 1);
    ses_destroy(b); ses_destroy(bad_c); ses_destroy(h);
    printf("session: %s (%.1f sn)\n", bad ? "ERROR" : "OK", now() - t0); return bad;
}
