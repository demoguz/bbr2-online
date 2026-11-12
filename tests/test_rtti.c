// Exercises rtti.c on the real exe image on Linux (imitates the loader's relocation).
#include <stdio.h>
#include <stdlib.h>
#include "../src/core.h"
int main(int argc, char** argv) {
    FILE* f = fopen(argv[1], "rb"); fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f);
    uint8_t* d = malloc(n); if (fread(d, 1, n, f) != (size_t)n) return 2;
    uint8_t* nt = d + *(uint32_t*)(d + 0x3c);
    uint32_t img = *(uint32_t*)(nt + 24 + 56); uint64_t pref = *(uint64_t*)(nt + 24 + 24);
    uint8_t* m = calloc(img, 1); memcpy(m, d, 0x1000);
    int ns = *(uint16_t*)(nt + 6); uint8_t* s = nt + 24 + *(uint16_t*)(nt + 20);
    for (int i = 0; i < ns; i++, s += 40) {
        uint32_t vs = *(uint32_t*)(s + 8), va = *(uint32_t*)(s + 12), rs = *(uint32_t*)(s + 16), ro = *(uint32_t*)(s + 20);
        memcpy(m + va, d + ro, rs < vs ? rs : vs);
        if (!memcmp(s, ".rdata", 7))
            for (uint32_t o = 0; o + 8 <= vs; o += 8) { uint64_t* q = (uint64_t*)(m + va + o); if (*q >= pref && *q < pref + img) *q = *q - pref + (uint64_t)(uintptr_t)m; }
    }
    printf("ts=%08x\n", pe_timestamp(m));
    int bad = 0;
    for (int i = 2; i + 1 < argc; i += 2) {
        void** vt = rtti_vtable(m, argv[i]);
        unsigned long rva = vt ? (unsigned long)((uint8_t*)vt - m) : 0, want = strtoul(argv[i + 1], 0, 16);
        printf("%-28s rva=%lx expected=%lx %s\n", argv[i], rva, want, rva == want ? "OK" : "ERROR"); bad |= rva != want;
    }
    return bad;
}
