// Small tree builder that writes the game's "fast container" (VuFastContainer) binary format. Portable C.
// Format (from 0x428930): node = u32 type + body. 1 int, 2 float, 3 bool, 4 string (u32 relative offset),
// 5 array (u32 count, u32 offset[]), 6 object (u32 count, {u64 FNV-1a(key), u32 keyOfs, u32 valueOfs}[] sorted by hash).
#pragma once
#include "core.h"
typedef struct Fc Fc;
void fc_reset(void);
Fc* fc_obj(void); Fc* fc_arr(void);
Fc* fc_str(const char* s); Fc* fc_int(int v); Fc* fc_float(float v); Fc* fc_bool(int v);
void fc_set(Fc* obj, const char* key, Fc* val);   // nesneye uye ekle
void fc_push(Fc* arr, Fc* val);                   // append an element to an array
uint32_t fc_build(Fc* root, uint8_t* out, uint32_t cap);   // 0: did not fit
