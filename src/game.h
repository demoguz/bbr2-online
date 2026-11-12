// Game structures. Verified only for the Steam build with PE timestamp 0x67C624B1.
#pragma once
#include "core.h"
#define BUILD_TS        0x67C624B1u
// VuVehicleInput* object
#define INPUT_VEH       0x08   // VuVehicleEntity*
#define INPUT_PAD       0x10   // gamepad number
#define INPUT_SLOT_UPDATE 1    // void update(float dt)
// VuVehicleEntity
#define VEH_ACTOR       0x1b8  // physx::PxRigidDynamic*
#define VEH_CTL         0x480  // control block: steering/throttle/flags
#define VEH_CTL_SIZE    24
// PxRigidDynamic vtable slots (verified from 0x14db60 and 0x14e6c0)
#define PX_GET_POSE     18
#define PX_SET_POSE     19
#define PX_GET_LINVEL   38
#define PX_SET_LINVEL   39
#define PX_GET_ANGVEL   40
#define PX_SET_ANGVEL   41
typedef struct { float q[4]; float p[3]; } PxT;   // PxTransform: quat(x,y,z,w) + konum
typedef PxT*   (MSABI *fn_get_pose)(void* self, PxT* out);
typedef void   (MSABI *fn_set_pose)(void* self, const PxT* t, bool wake);
typedef float* (MSABI *fn_get_vec)(void* self, float* out);
typedef void   (MSABI *fn_set_vec)(void* self, const float* v, bool wake);
#define VT(obj) (*(void***)(obj))
static inline void* veh_actor(void* veh) { return veh ? *(void**)((char*)veh + VEH_ACTOR) : 0; }
static inline void px_get_pose(void* a, PxT* o)        { ((fn_get_pose)VT(a)[PX_GET_POSE])(a, o); }
static inline void px_set_pose(void* a, const PxT* t)  { ((fn_set_pose)VT(a)[PX_SET_POSE])(a, t, true); }
static inline void px_get_lin(void* a, float* o)       { float t[4]; float* r = ((fn_get_vec)VT(a)[PX_GET_LINVEL])(a, t); memcpy(o, r, 12); }
static inline void px_get_ang(void* a, float* o)       { float t[4]; float* r = ((fn_get_vec)VT(a)[PX_GET_ANGVEL])(a, t); memcpy(o, r, 12); }
static inline void px_set_lin(void* a, const float* v) { ((fn_set_vec)VT(a)[PX_SET_LINVEL])(a, v, true); }
static inline void px_set_ang(void* a, const float* v) { ((fn_set_vec)VT(a)[PX_SET_ANGVEL])(a, v, true); }

// ---- Race setup (A2) ----
// Global game settings object: *(base + G_GAMECFG). 0x1cb660 fills it from a fast-container tree.
#define G_GAMECFG       0xd6aa60
#define FN_CFG_PARSE    0x1cb660   // void parse(cfg*, const void* fastRoot)
#define FN_STR_ASSIGN   0x16790    // std::string* assign(std::string*, const char*, size_t)
#define TBL_GAMEPATH    0xcbcfc0   // char*[15]: Dev, RaceSlot, ..., Simple, QuickEvent, ...
#define TBL_GAMEPATH_N  15
#define TBL_GAMETYPE    0xcbd040   // char*[12]: Race, Tutorial, Drift, ...
#define TBL_GAMETYPE_N  12
#define CFG_GAMEPATH    0x08
#define CFG_GAMETYPE    0x0c
#define CFG_GRIDMODE    0x14       // grid: 0 list order, 1 reversed, 2 random, 4 humans at the back (game default), 5 random + humans at the back
#define CFG_TRACK       0x18       // std::string
#define CFG_SETTING     0x38       // std::string
#define CFG_MIRROR      0x58
#define CFG_LAPS        0xc0
#define CFG_PLAYERS     0x2a8      // std::vector<player settings>, element 0x100; +0 gamepad number. The pause menu's input mask comes from here (0x178e30)
#define CFG_PLAYER_TMPL 0x2c0      // local player template (same 0x100 struct)
#define PL_SIZE         0x100
#define FN_PLAYERS_RESIZE 0xfa500  // void resize(vector*, size_t n)
#define FN_PLAYER_COPY  0xf8f00    // void copy(player* dst, const player* src)
#define CFG_SHAREDDECK  0x4c8      // std::vector<{std::string name; int level}>, element 0x28; the parser clears it first
#define CFG_VEHICLES    0x4e8      // std::vector<vehicle settings>, element 0x250 bytes
#define VC_SIZE         0x250
#define VC_VEHICLE      0x00       // std::strings
#define VC_DRIVER       0x20
#define VC_DRIVERSKIN   0x40
#define VC_PAINT        0x60
#define VC_DECALCOLOR   0x80
#define VC_DECAL        0xa0
#define VC_NAME         0xd8       // std::string: player name
#define VC_LEVEL        0x118
#define VC_INPUT        0x11c      // 0 human, 1 AI
#define VC_CTRLMASK     0x120
#define VC_VIEWPORT     0x124      // -1: no viewport
#define VC_SCRIPT       0x178      // std::string (RaceScript)
#define VC_AILEVEL      0x19c
#define VC_DECK         0x1a0      // std::vector, element 0x28 (starts with a std::string)
#define VC_DECK_ELEM    0x28       // {std::string name; int level}; in the recipe "Name:level" (0x3ff1d0)
#define VC_POWERUPS     0x1b9      // bool: create a power-up controller for the car (0x146d91)
#define VC_ABILITY      0x1bc
// Game mode objects (VuGameMode): slot 2 enter, 3 exit, 4 tick
#define GM_SLOT_ENTER   2
#define GM_SLOT_EXIT    3
#define GM_SLOT_TICK    4
#define FE_NEXTMODE     0x98       // std::string: next mode ("Action")
#define FE_GAMEPATH     0xb8       // std::string: StartActionGame path; if empty the front end does not rebuild the settings

// ---- Online (A3) ----
#define VEH_CFG         0x190      // VuVehicleEntity -> its own vehicle settings row (written by the constructor 0x145c80)
// Player struct (settings+0x2a8 vector element and the +0x2c0 template), 0x100 bytes
#define PL_PAD          0x00
#define PL_VEHICLE      0x08
#define VPC_VIEWPORT    0x108      // VuViewportContainerEntity: Viewport property
#define VPC_PAD         0x10c      // gamepad (children read it with 0x50830)
#define VPC_SLOT_INIT   5          // gamepad = player[viewport].gamepad (when viewport < player count)
#define PL_JSON         0xe0       // per-player JSON container ("SplitScreen" -> "State"/"Progress")
#define PL_DRIVER       0x28
#define PL_SKIN         0x48
#define PL_PAINT        0x68
#define PL_DECALCOLOR   0x88
#define PL_DECAL        0xa8
// State machine condition setter: void setCondition(fsm*, const char* name, bool value)
#define FN_SETCOND      0x42dea0
#define SETCOND_LEN     14
#define SETCOND_SIG     { 0x0f,0xb6,0x02, 0x4c,0x8b,0xd1, 0x41,0xb9,0xc5,0x9d,0x1c,0x81, 0x84,0xc0 }
#define STR_CINEMATIC   0xa79a08   // "CinematicActive": Intro -> PreGame transition on !CinematicActive
#define STR_SKIPINTRO   0xa799b8   // "SkipIntro": written once while the race loads (0x178bdf)
#define STR_FORCEINTRO  0xa79d08   // "ForceIntro"
#define STR_STARTGAME   0xa51648   // "GameFlowStartGame": countdown finished (PreGame -> Game), 0x17f250
#define STR_ENDGAME     0xa516f8   // "GameFlowEndGame": VuRaceGame slot 15 (0x1847a0), every tick = all local humans have finished
#define VEH_FINISHED    0x730      // byte: finished the race (the end-of-game estimate 0x179890 skips these)
#define VEH_TIME        0x748      // double: race time; dt is added every tick until the car finishes (0x17a880)
#define VEH_DNF         0x758      // byte: did not finish / estimated (the sort comparator 0x173560 checks this first)

// ---- Effects / power-ups (A4) ----
// EVERY effect applied to a car goes through here: uint32 apply(VuVehicleEffectController*, const char* effectName, const EffectParams*)
// Using a power-up: 0x16b600 -> 0x142bc0(ctrl, VuPowerUp*, level) -> here (effect name = power-up name).
// Hit: the projectile applies its own copy of the parameters to the victim's controller (e.g. 0x11b9cc, VictimEffect).
#define VEH_EFFECTS     0x280      // VuVehicleEffectController*; controller+0 = owning vehicle
#define FN_APPLYEFFECT  0x1423f0
#define APPLYEFFECT_LEN 14
#define APPLYEFFECT_SIG { 0x48,0x89,0x5c,0x24,0x20, 0x55, 0x56, 0x57, 0x41,0x54, 0x41,0x55, 0x41,0x56 }
#define EP_SIZE         0x38       // VuVehicleEffectParams
#define EP_LEVEL        0x10       // int
#define EP_SOURCE       0x18       // VuVehicleEntity* (vehicle that used/fired it, or 0)
#define EP_POWERUP      0x20       // VuPowerUp* (+0 std::string name), or 0
#define EP_TIME         0x28       // VuSys raw counter (local)
#define EP_FLAG         0x30       // 8 bayt (0x142c60'ta 1)
#define G_SYS           0xd6da20   // VuSys*; sanal slot 8 = ham sayac
#define G_POWERUPMGR    0xd6a948   // VuPowerUpManager*; unordered_map<fnv1a32(name), VuPowerUp*>: +0x58 end, +0x68 buckets, +0x80 mask

// ---- Race lifecycle ----
// UI actions (VuUIAction subclasses) vtable slot 17 = execute: void* exec(this, VuRetVal* ret, const VuParams* p)
#define UIACT_SLOT_EXEC 17
// VuRestartActionGameUIAction (0xd2030) -> event "OnRestartActionGame"; VuStartFrontEndUIAction (0xd2670) -> "OnStartFrontEnd"
// VuShowTipUIAction (0xd24c0) -> VuTipManager 0x1a0430
#define G_EVENTMGR      0xd6df70   // VuEventManager*
#define FN_BROADCAST    0x42a810   // void broadcast(mgr, const char* event, VuParams* p); VuParams 0x108 bytes, +0x100 count

// ---- In-game UI (native lobby) ----
#define FN_ADDPARAM     0x4143d0   // VuParams::add(this, type, data, length): type 3 = bool (1 byte), 4 = string (including NUL)
#define G_STRINGDB      0xd6e060   // VuStringDB*
#define FN_GETSTRING    0x44fb80   // const char* VuStringDB::getString(this, const char* id)
#define KB_TEXT         0x168      // VuVirtualKeyboardEntity: typed text (std::string)
#define KB_MAXLEN       0x15c      // int maximum length
#define KB_SLOT_INIT    12         // VuGenericKeyboardEntity: sets up the characters-remaining macro (at start)
#define UIACT_SCREEN    0x100      // VuPushScreenUIAction: screen name (std::string); VuStartFrontEndUIAction: "Screen Stack"
#define DB_SLOT_LOAD    3          // VuDBAsset::load(this, reader{data u64, size i32, pos i32}); root = data+8
#define G_SETTINGS      0xd6da18   // *(base+G_SETTINGS)+8: global settings container ("InitialScreenStack" is here)
#define FN_JSON_GET     0x426590   // node* get(kap, const char* anahtar)
#define FN_JSON_SETSTR  0x427a30   // void set(node*, const std::string*)
#define PROJ_SLOT_LOAD  3          // VuProjectAsset::load(this, VuBinaryDataReader*)
#define PROJ_ROOT       0x40       // fast-container root node (screen tree)
#define ASSET_NAME      0x08       // VuAsset: std::string name
#define FN_GETMACRO     0x4752a0   // bool VuFontMacros::get(this, const char* name, std::string* out)
#define GETMACRO_LEN    15
#define GETMACRO_SIG    { 0x48,0x89,0x5c,0x24,0x10, 0x48,0x89,0x74,0x24,0x18, 0x57, 0x48,0x83,0xec,0x20 }
#define FN_BROADCASTH   0x42a6b0   // void VuEventManager::broadcast(this, uint32 fnv1a32(event), VuParams*)
#define BROADCASTH_LEN  15
#define BROADCASTH_SIG  { 0x48,0x89,0x5c,0x24,0x18, 0x48,0x89,0x6c,0x24,0x20, 0x57, 0x48,0x83,0xec,0x20 }
// A VuGenericEventEntity in the UI with "Event Name"=X: its Broadcast input sends the event "OnGenericEvent:X",
// and when the same event arrives it fires its Trigger output (both directions).
