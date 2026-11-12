// Race recipe: read from / written to the game's settings object. Sent over the network as a raw struct.
#pragma once
#include "core.h"
typedef struct {
    char vehicle[48], driver[48], skin[48], paint[48], decalcolor[48], decal[48];
    char name[48];
    int level, input, viewport, ailevel, ability, powerups; uint32_t ctrlmask;
    int ndeck; char deck[16][40];
} VehDesc;
typedef struct {
    char gamepath[24], gametype[24], track[64], setting[64], script[64];
    int mirror, laps, gridmode, nveh; VehDesc v[12];
    int nshared; char shared[64][40];
    int nplayers, pad0;      // local player list: count and the first player's gamepad number
} RaceDesc;
typedef struct { char vehicle[48], driver[48], skin[48], paint[48], decalcolor[48], decal[48]; } CarSel;
int  race_ready(void);                         // are the hooks installed
int  race_get_capture(RaceDesc* out);          // last captured normal race (1: available)
void race_request(const RaceDesc* d);          // start a race with this recipe while in the menu
int  race_pending(void);
int  race_in_menu(void);                       // is the front end (menu) running now
int  race_read_local_car(CarSel* car, int* pad, char* name, int namecap);
void* race_cfg(void);
