// Reads fastc.c output with an exact copy of the game's 0x428930 lookup algorithm and checks it.
#include <stdio.h>
#include "../src/fastc.h"
static const uint32_t g_null[2] = {0, 0};
static const void* member(const void* o, const char* key) {     // 0x428930
    const uint32_t* n = o; if (n[0] != 6) return g_null;
    uint64_t h = 0xcbf29ce484222325ull; for (; *key; key++) { h ^= (uint8_t)*key; h *= 0x100000001b3ull; }
    uint32_t hi = n[1], lo = 0;
    while (lo < hi) { uint32_t mid = (hi + lo) >> 1; uint64_t k = *(const uint64_t*)((const uint8_t*)o + 8 + 16*(uint64_t)mid);
        if (h < k) hi = mid; else if (h > k) lo = mid + 1; else return (const uint8_t*)o + *(const uint32_t*)((const uint8_t*)o + 16*(uint64_t)mid + 0x14); }
    return g_null;
}
static const char* str(const void* n) { const uint32_t* p = n; return p[0] == 4 ? (const char*)n + p[1] : ""; }
static int num(const void* n) { const uint32_t* p = n; return p[0] == 1 ? (int)p[1] : -999; }
#define CHECK(c) do { if (!(c)) { printf("ERROR: %s\n", #c); bad = 1; } } while (0)
int main(void) {
    int bad = 0; static uint8_t buf[32768];
    fc_reset(); Fc* r = fc_obj();
    fc_set(r, "GamePath", fc_str("QuickEvent")); fc_set(r, "GameType", fc_str("Race")); fc_set(r, "Track", fc_str("Beach_A"));
    fc_set(r, "LapCount", fc_int(3)); fc_set(r, "MirrorTrack", fc_bool(1));
    Fc* vs = fc_arr();
    const char* keys[] = {"Vehicle","Driver","DriverSkin","PaintColor","DecalColor","Decal","InputType"};
    for (int i = 0; i < 8; i++) { Fc* v = fc_obj(); char t[32];
        for (int k = 0; k < 7; k++) { snprintf(t, sizeof t, "%s_%d", keys[k], i); fc_set(v, keys[k], fc_str(t)); }
        fc_set(v, "Level", fc_int(i)); fc_set(v, "ViewportIndex", fc_int(i ? -1 : 0));
        Fc* d = fc_arr(); fc_push(d, fc_str("Missile")); fc_push(d, fc_str("Oil")); fc_set(v, "PowerUpDeck", d);
        fc_push(vs, v); }
    fc_set(r, "Vehicles", vs);
    uint32_t n = fc_build(r, buf, sizeof buf); CHECK(n > 0);
    CHECK(!strcmp(str(member(buf, "Track")), "Beach_A")); CHECK(!strcmp(str(member(buf, "GamePath")), "QuickEvent"));
    CHECK(num(member(buf, "LapCount")) == 3); CHECK(((const uint32_t*)member(buf, "MirrorTrack"))[0] == 3 && ((const uint8_t*)member(buf, "MirrorTrack"))[4] == 1);
    CHECK(((const uint32_t*)member(buf, "Yok"))[0] == 0);
    const uint32_t* a = member(buf, "Vehicles"); CHECK(a[0] == 5 && a[1] == 8);
    for (uint32_t i = 0; i < a[1]; i++) { const void* v = (const uint8_t*)a + a[2 + i]; char t[32];     // game: element = array + offset[i]
        for (int k = 0; k < 7; k++) { snprintf(t, sizeof t, "%s_%d", keys[k], (int)i); CHECK(!strcmp(str(member(v, keys[k])), t)); }
        CHECK(num(member(v, "Level")) == (int)i); CHECK(num(member(v, "ViewportIndex")) == (i ? -1 : 0));
        const uint32_t* d = member(v, "PowerUpDeck"); CHECK(d[0] == 5 && d[1] == 2); CHECK(!strcmp(str((const uint8_t*)d + d[3]), "Oil")); }
    printf("fastc: %u bayt, %s\n", n, bad ? "ERROR" : "OK"); return bad;
}
