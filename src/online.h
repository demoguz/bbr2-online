#pragma once
#include "core.h"
#include "race.h"
#ifdef __cplusplus
extern "C" {
#endif
void online_install(uint8_t* base);
int  online_active(void);                 // in a session (host or connected client)
int  online_is_pending_race(void);        // is the pending race request an online one
int  online_host_convert(const RaceDesc* base, RaceDesc* local_variant);
int  online_host_trim(const RaceDesc* base, RaceDesc* out);   // host alone: apply the bot count
int  online_local_pad(void);
void online_race_entered(void* cfg, int online);
void online_race_exit(void);
void online_menu_tick(void);
// race state for the UI
typedef struct { int in_race, holding, waiting_n, waiting_of, my_slot, nslot; uint32_t race_id; float clock; char note[160]; char toast[160]; uint32_t toast_age_ms; } OnlineStatus;
void online_toast(const char* fmt, ...);
void online_status(OnlineStatus* st);
void online_set_name(const char* name);
void online_get_name(char* out, int cap);
int  online_last_start_available(void);
void online_host_restart_last(void);
#ifdef __cplusplus
}
#endif
