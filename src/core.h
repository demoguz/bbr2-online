// bbr2net: shared declarations
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#define BBR2NET_SEMVER "0.10.0"
#define BBR2NET_VERSION "BBR2 Online " BBR2NET_SEMVER
#if defined(__GNUC__)
#define MSABI __attribute__((ms_abi))
#else
#define MSABI
#endif
// util.c
void core_init(void);
void logf_(const char* fmt, ...);
#define LOG(...) logf_(__VA_ARGS__)
void logf_raw(const char* line);                       // not forwarded (chat)
void logf_remote(const char* who, const char* text);   // line from a remote peer, not forwarded
void log_ring_push(const char* line);                  // net.c
const char* mod_dir(void);
extern int g_instance;                                // which copy of the game on this PC
void* inline_hook(uint8_t* target, int len, const uint8_t* expect, void* detour);   // hook.c
// (overlay removed; the in-game UI is ui.c)
uint64_t now_ms(void);
void cfg_load(void);
double cfg_num(const char* key, double def);
void cfg_vec3(const char* key, float* out, float dx, float dy, float dz);
int cfg_str(const char* key, char* out, int cap);   // 1: found and not empty
void* vt_hook(void** vt, int slot, void* fn);
// rtti.c (portable: works on a memory image only)
uint32_t pe_timestamp(const uint8_t* base);
void** rtti_vtable(uint8_t* base, const char* cls);
// proxy.c
void proxy_init(void);
// shadow.c (A1 test; only when not online)
void shadow_human(void* self, void* veh, float dt);
int  shadow_ai(void* self, void* veh, float dt);
// race.c
void race_install(uint8_t* base);
// ui.c
void ui_install(uint8_t* base);
void ui_send(const char* ev);
void ui_menu_tick(void);   // front-end tick
void ui_race_ended(void);  // online race ended
void* ui_frontend_wrap(void* self, void* ret, const void* params, void* (MSABI *orig)(void*, void*, const void*));   // StartFrontEnd: lobby screen stack when in a session
void ui_prepare_frontend(void);
void ui_fe_exit(void);   // when leaving the front end (race starting): fake lobby players are removed  // before returning to the front end from code (same purpose)
