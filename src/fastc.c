#include "fastc.h"
struct Fc { int type, n; const char* key; union { int i; float f; const char* s; } v; Fc *child, *last, *next; uint64_t hash; };
static Fc g_nodes[2048]; static int g_nn;
static char g_strs[32768]; static int g_ns;
void fc_reset(void) { g_nn = 0; g_ns = 0; }
static Fc* node(int type) { if (g_nn >= 2048) return 0; Fc* n = &g_nodes[g_nn++]; memset(n, 0, sizeof *n); n->type = type; return n; }
static const char* dup(const char* s) {
    int l = (int)strlen(s) + 1; if (g_ns + l > (int)sizeof g_strs) return "";
    char* d = g_strs + g_ns; memcpy(d, s, l); g_ns += l; return d;
}
static uint64_t fnv(const char* s) { uint64_t h = 0xcbf29ce484222325ull; for (; *s; s++) { h ^= (uint8_t)*s; h *= 0x100000001b3ull; } return h; }
Fc* fc_obj(void) { return node(6); }
Fc* fc_arr(void) { return node(5); }
Fc* fc_str(const char* s) { Fc* n = node(4); if (n) n->v.s = dup(s); return n; }
Fc* fc_int(int v) { Fc* n = node(1); if (n) n->v.i = v; return n; }
Fc* fc_float(float v) { Fc* n = node(2); if (n) n->v.f = v; return n; }
Fc* fc_bool(int v) { Fc* n = node(3); if (n) n->v.i = v != 0; return n; }
void fc_push(Fc* a, Fc* v) {
    if (!a || !v) return;
    if (a->last) a->last->next = v; else a->child = v;
    a->last = v; a->n++;
}
void fc_set(Fc* o, const char* key, Fc* v) {      // insert sorted by hash
    if (!o || !v) return;
    v->key = dup(key); v->hash = fnv(key);
    Fc** p = &o->child;
    while (*p && (*p)->hash < v->hash) p = &(*p)->next;
    v->next = *p; *p = v; o->n++;
}
static uint32_t al8(uint32_t x) { return (x + 7) & ~7u; }
static uint32_t emit(const Fc* n, uint8_t* out, uint32_t pos, uint32_t cap) {   // returns: new end position, 0 on error
#define NEED(sz) if (pos + (sz) > cap) return 0
#define W32(at, val) do { uint32_t t_ = (uint32_t)(val); memcpy(out + (at), &t_, 4); } while (0)
    uint32_t base = pos;
    NEED(8); W32(base, n->type);
    switch (n->type) {
    case 1: case 3: W32(base + 4, n->v.i); return base + 8;
    case 2: memcpy(out + base + 4, &n->v.f, 4); return base + 8;
    case 4: { uint32_t l = (uint32_t)strlen(n->v.s) + 1; NEED(8 + l + 8); W32(base + 4, 8); memcpy(out + base + 8, n->v.s, l); return al8(base + 8 + l); }
    case 5: {
        uint32_t hdr = al8(8 + 4*n->n); NEED(hdr); W32(base + 4, n->n); pos = base + hdr; int i = 0;
        for (const Fc* c = n->child; c; c = c->next, i++) { W32(base + 8 + 4*i, pos - base); pos = emit(c, out, pos, cap); if (!pos) return 0; }
        return pos; }
    case 6: {
        uint32_t hdr = 8 + 16*n->n; NEED(hdr); W32(base + 4, n->n); pos = base + hdr; int i = 0;
        for (const Fc* c = n->child; c; c = c->next, i++) {
            uint32_t e = base + 8 + 16*i, l = (uint32_t)strlen(c->key) + 1;
            memcpy(out + e, &c->hash, 8);
            NEED(l + 8); W32(e + 8, pos - base); memcpy(out + pos, c->key, l); pos = al8(pos + l);
            W32(e + 12, pos - base); pos = emit(c, out, pos, cap); if (!pos) return 0;
        }
        return pos; }
    }
    return 0;
}
uint32_t fc_build(Fc* root, uint8_t* out, uint32_t cap) { if (!root) return 0; memset(out, 0, cap); return emit(root, out, 0, cap); }
