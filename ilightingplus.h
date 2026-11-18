// ilightingplus.h - antarmuka publik LightingPlus untuk mod AML lain
//
// Pemakaian di mod lain:
//   #include "ilightingplus.h"
//   ILightingPlus* L = (ILightingPlus*)GetInterface("ILightingPlus");
//   if(L) L->AddDynamicLight(x, y, z, 10.0f, 1.0f, 0.4f, 0.1f);   // panggil setiap frame
#pragma once

struct ILightingPlus
{
    unsigned int version; // 1

    // Tambah 1 cahaya titik untuk frame ini. Otomatis kena budget jumlah cahaya + fade jarak.
    void  (*AddDynamicLight)(float x, float y, float z, float radius, float r, float g, float b);

    // 0.0 = siang, 1.0 = malam penuh (sudah memperhitungkan interior & ForceDarkness)
    float (*GetDarkness)();
};
