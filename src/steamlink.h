// Steam network, game side (steamlink.c). Services: [0] the game's own AppID (in process), [1] Spacewar (bbr2steam.exe).
// sl_read_mode and sl_classify are pure (callable from any thread); the rest only from the network thread.
#pragma once
enum { SLE_NONE, SLE_HOSTED, SLE_HOSTFAIL, SLE_JOINED, SLE_JOINFAIL, SLE_INVITED, SLE_NOTE };
typedef struct { int type, net, port; char a[64], b[320]; } SlEvent;   // HOSTED: a = code; JOINED: port, a = code, b = host; INVITED: a = "id <lobby>"
int  sl_read_mode(char* name, int cap);   // online.ini steam=: -1 off, 0 game, 1 spacewar
int  sl_classify(const char* target, int* net, char* norm, int cap);   // 0: IP, 1: Steam target (norm = "code K" / "id N"), 2: invalid code
void sl_init(void);
void sl_host(int net, int enet_port, int maxp, const char* name);
void sl_join(int net, const char* norm);
void sl_invite(void);
void sl_leave(void);
int  sl_poll(SlEvent* out);
