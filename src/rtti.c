// Finds a primary vtable by class name from MSVC x64 RTTI. Uses no fixed RVAs.
#include "core.h"
typedef struct { char name[8]; uint32_t vsize, va, rsize, roff; uint32_t pad[3]; uint32_t flags; } Sec;
static const uint8_t* nt(const uint8_t* b) { return b + *(const uint32_t*)(b + 0x3c); }
uint32_t pe_timestamp(const uint8_t* b) { return *(const uint32_t*)(nt(b) + 8); }
static int secs(const uint8_t* b, const Sec** out) {
    const uint8_t* n = nt(b);
    *out = (const Sec*)(n + 24 + *(const uint16_t*)(n + 20));
    return *(const uint16_t*)(n + 6);
}
void** rtti_vtable(uint8_t* base, const char* cls) {
    char needle[160]; size_t nl = 0;
    const char* pre = ".?AV";
    while (*pre) needle[nl++] = *pre++;
    for (const char* c = cls; *c && nl < 150; c++) needle[nl++] = *c;
    needle[nl++] = '@'; needle[nl++] = '@'; needle[nl++] = 0;
    const Sec* s; int n = secs(base, &s);
    uint32_t td = 0;
    for (int i = 0; i < n && !td; i++) {
        if (memcmp(s[i].name, ".data", 6) && memcmp(s[i].name, ".rdata", 7)) continue;
        const uint8_t* p = base + s[i].va; uint32_t len = s[i].vsize;
        for (uint32_t o = 0x10; o + nl <= len; o++)
            if (p[o] == '.' && !memcmp(p + o, needle, nl)) { td = s[i].va + o - 0x10; break; }
    }
    if (!td) return 0;
    for (int i = 0; i < n; i++) {
        if (memcmp(s[i].name, ".rdata", 7)) continue;
        const uint8_t* p = base + s[i].va; uint32_t len = s[i].vsize;
        for (uint32_t o = 0; o + 0x18 <= len; o += 4) {
            const uint32_t* c = (const uint32_t*)(p + o);
            // CompleteObjectLocator: signature 1, this offset 0 (primary), type descriptor, its own RVA
            if (c[0] != 1 || c[1] != 0 || c[3] != td || c[5] != s[i].va + o) continue;
            uint64_t want = (uint64_t)(uintptr_t)(p + o);
            for (uint32_t k = 0; k + 8 <= len; k += 8)
                if (*(const uint64_t*)(p + k) == want) return (void**)(p + k + 8);
        }
    }
    return 0;
}
