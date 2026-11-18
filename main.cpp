// LightingPlus - plugin AML untuk GTA San Andreas Android (32-bit & 64-bit)
//
//  1. Ped Lighting     : CJ & NPC diberi "night fill" (tidak gelap pekat saat malam) + kena cahaya lampu kendaraan
//  2. Vehicle Lighting : lampu depan menerangi jalan, lampu belakang + rem (deteksi dari perlambatan)
//  3. Dynamic Light    : budget jumlah cahaya, fade berdasarkan jarak, API untuk mod lain
//
// Semua alamat dicari lewat .dynsym libGTASA.so (elfsym.h). Kalau simbol tidak ada, fitur terkait
// otomatis nonaktif (tidak crash) dan alasannya ditulis ke log. Offset manual bisa diisi di [Offsets].

#include <mod/amlmod.h>
#include <mod/logger.h>
#include <mod/flogger.h>
#include <mod/config.h>
#include <time.h>
#include <stdint.h>
#include <math.h>
#include "elfsym.h"
#include "ilightingplus.h"

MYMODCFG(net.lightingplus.gtasa, Lighting Plus, 1.0.0, LightingPlus)
NEEDGAME(com.rockstargames.gtasa)

/* ------------------------------------------------------------------ */
/*  Log (logcat + /sdcard/Documents/AMLModLogs/net.lightingplus.gtasa/) */
/* ------------------------------------------------------------------ */
static FLogger g_flog("lightingplus.log");
#define LOGI(...) do { logger->Info(__VA_ARGS__);  g_flog.Info(__VA_ARGS__);  } while(0)
#define LOGE(...) do { logger->Error(__VA_ARGS__); g_flog.Error(__VA_ARGS__); } while(0)

/* ------------------------------------------------------------------ */
/*  Tipe & util kecil                                                  */
/* ------------------------------------------------------------------ */
struct CVec3      { float x, y, z; };
struct RwRGBAReal { float r, g, b, a; };

static inline float Clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }
static inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }
static inline float Smooth(float a, float b, float x)
{
    float t = Clamp01((x - a) / (b - a));
    return t * t * (3.f - 2.f * t);
}
static inline bool  FiniteF(float f) { return f == f && fabsf(f) < 1.0e7f; }

static uint32_t NowMs()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

/* ------------------------------------------------------------------ */
/*  Pengaturan (LightingPlus .ini lewat AML Config)                    */
/* ------------------------------------------------------------------ */
struct Settings
{
    // [General]
    bool  enabled        = true;
    bool  probeOnly      = false;   // hanya cek simbol & tulis log, tanpa hook
    float forceDarkness  = -1.0f;   // 0..1 untuk tes di siang hari, -1 = pakai jam game
    float interiorScale  = 0.0f;
    // [Features]
    bool  pedLighting    = true;
    bool  vehicleLighting= true;
    bool  dynamicLights  = true;
    // [Ped]
    float playerFill     = 0.32f;
    float npcFill        = 0.28f;
    float pedLightGain   = 0.90f;
    float ambientCap     = 0.85f;
    // [Vehicle]
    int   headTrigger    = 0;       // 0 = DoHeadLightBeam, 1 = DoHeadLightEffect, 2 = CVehicle::PreRender
    float headDistance   = 5.5f;
    float headRadius     = 11.0f;
    float headIntensity  = 0.55f;
    float tailDistance   = 2.6f;
    float tailRadius     = 3.5f;
    float tailIntensity  = 0.25f;
    float brakeIntensity = 0.55f;
    float brakeDecel     = 5.0f;    // m/s^2 -> rem dianggap "penuh"
    int   maxVehicles    = 4;
    // [Dynamic]
    int   maxLights      = 8;       // jatah cahaya milik plugin per frame
    int   maxTotalLights = 28;      // plugin berhenti menambah bila NumLights game sudah segini (batas game = 32)
    float fadeNear       = 18.0f;
    float maxDistance    = 55.0f;
    float minIntensity   = 0.03f;
};
static Settings g_set;

static bool CfgBool(const char* sec, const char* key, bool def)
{
    ConfigEntry* e = cfg->Bind(key, def, sec);
    if(!e) return def;
    bool v = e->GetBool();
    delete e;
    return v;
}
static float CfgFloat(const char* sec, const char* key, float def, float mn, float mx)
{
    ConfigEntry* e = cfg->Bind(key, def, sec);
    if(!e) return def;
    e->Clamp(mn, mx);
    float v = e->GetFloat();
    delete e;
    return v;
}
static int CfgInt(const char* sec, const char* key, int def, int mn, int mx)
{
    ConfigEntry* e = cfg->Bind(key, def, sec);
    if(!e) return def;
    e->Clamp(mn, mx);
    int v = e->GetInt();
    delete e;
    return v;
}

static void LoadSettings()
{
    Settings& s = g_set;
    s.enabled         = CfgBool ("General", "Enabled",        s.enabled);
    s.probeOnly       = CfgBool ("General", "ProbeOnly",      s.probeOnly);
    s.forceDarkness   = CfgFloat("General", "ForceDarkness",  s.forceDarkness, -1.0f, 1.0f);
    s.interiorScale   = CfgFloat("General", "InteriorScale",  s.interiorScale, 0.0f, 1.0f);

    s.pedLighting     = CfgBool ("Features", "PedLighting",     s.pedLighting);
    s.vehicleLighting = CfgBool ("Features", "VehicleLighting", s.vehicleLighting);
    s.dynamicLights   = CfgBool ("Features", "DynamicLights",   s.dynamicLights);

    s.playerFill      = CfgFloat("Ped", "PlayerNightFill", s.playerFill,   0.0f, 1.0f);
    s.npcFill         = CfgFloat("Ped", "NpcNightFill",    s.npcFill,      0.0f, 1.0f);
    s.pedLightGain    = CfgFloat("Ped", "VehicleLightGain",s.pedLightGain, 0.0f, 3.0f);
    s.ambientCap      = CfgFloat("Ped", "AmbientCap",      s.ambientCap,   0.1f, 1.0f);

    s.headTrigger     = CfgInt  ("Vehicle", "HeadTrigger",    s.headTrigger, 0, 2);
    s.headDistance    = CfgFloat("Vehicle", "HeadDistance",   s.headDistance,   1.0f, 20.0f);
    s.headRadius      = CfgFloat("Vehicle", "HeadRadius",     s.headRadius,     2.0f, 30.0f);
    s.headIntensity   = CfgFloat("Vehicle", "HeadIntensity",  s.headIntensity,  0.0f, 2.0f);
    s.tailDistance    = CfgFloat("Vehicle", "TailDistance",   s.tailDistance,   0.5f, 8.0f);
    s.tailRadius      = CfgFloat("Vehicle", "TailRadius",     s.tailRadius,     1.0f, 15.0f);
    s.tailIntensity   = CfgFloat("Vehicle", "TailIntensity",  s.tailIntensity,  0.0f, 2.0f);
    s.brakeIntensity  = CfgFloat("Vehicle", "BrakeIntensity", s.brakeIntensity, 0.0f, 2.0f);
    s.brakeDecel      = CfgFloat("Vehicle", "BrakeDecel",     s.brakeDecel,     1.0f, 20.0f);
    s.maxVehicles     = CfgInt  ("Vehicle", "MaxVehicles",    s.maxVehicles,    1, 16);

    s.maxLights       = CfgInt  ("Dynamic", "MaxLights",      s.maxLights,      1, 24);
    s.maxTotalLights  = CfgInt  ("Dynamic", "MaxTotalLights", s.maxTotalLights, 4, 32);
    s.fadeNear        = CfgFloat("Dynamic", "FadeNear",       s.fadeNear,       1.0f, 200.0f);
    s.maxDistance     = CfgFloat("Dynamic", "MaxDistance",    s.maxDistance,    5.0f, 300.0f);
    s.minIntensity    = CfgFloat("Dynamic", "MinIntensity",   s.minIntensity,   0.0f, 0.5f);
    if(s.maxDistance <= s.fadeNear) s.maxDistance = s.fadeNear + 1.0f;
}

/* ------------------------------------------------------------------ */
/*  Resolusi simbol / alamat game                                      */
/* ------------------------------------------------------------------ */
static uintptr_t g_pGame = 0;

static uintptr_t ParseHex(const char* s)
{
    if(!s || !*s) return 0;
    return (uintptr_t)strtoull(s, NULL, 16);
}

// 1) offset manual dari [Offsets] (hex, relatif ke base libGTASA; tambahkan +1 untuk fungsi Thumb 32-bit)
// 2) kalau 0 -> cari di .dynsym
static uintptr_t Resolve(const char* what, const char* offKey,
                         std::initializer_list<const char*> tokens, bool func)
{
    uintptr_t off = 0;
    if(ConfigEntry* e = cfg->Bind(offKey, "0", "Offsets"))
    {
        off = ParseHex(e->GetString());
        delete e;
    }
    if(off)
    {
        LOGI("[sym] %-22s = offset manual +0x%lx", what, (unsigned long)off);
        return g_pGame + off;
    }

    char   name[200] = {0};
    size_t n = 0;
    uintptr_t a = ElfSym::Find(tokens, func, name, sizeof(name), &n);
    if(a) LOGI("[sym] %-22s = %s (+0x%lx)%s", what, name, (unsigned long)(a - g_pGame), n > 1 ? "  [ada kandidat lain]" : "");
    else  LOGI("[sym] %-22s = TIDAK DITEMUKAN", what);
    return a;
}

typedef void (*AddLight_t)(unsigned char type, CVec3 pos, CVec3 dir, float radius,
                           float r, float g, float b, unsigned char fogType,
                           bool extraShadows, void* castShadowFrom);
typedef void* (*FindPlayerPed_t)(int);

static AddLight_t       g_AddLight      = nullptr;
static FindPlayerPed_t  g_FindPlayerPed = nullptr;
static unsigned int*    g_pNumLights    = nullptr;
static unsigned char*   g_pHours        = nullptr;
static unsigned char*   g_pMinutes      = nullptr;
static unsigned int*    g_pFrameCounter = nullptr;
static unsigned int*    g_pCurrArea     = nullptr;
static RwRGBAReal*      g_pAmbPeds      = nullptr;

/* ------------------------------------------------------------------ */
/*  Waktu / gelap-terang                                               */
/* ------------------------------------------------------------------ */
static bool g_warnedNoClock = false;

static float GetDarkness()
{
    float dark;
    if(g_set.forceDarkness >= 0.0f)
    {
        dark = g_set.forceDarkness;
    }
    else if(g_pHours)
    {
        float h = (float)(*g_pHours);
        if(g_pMinutes) h += (float)(*g_pMinutes) / 60.0f;
        if(h >= 12.0f) dark = Smooth(19.0f, 21.5f, h);          // senja -> malam
        else           dark = 1.0f - Smooth(4.5f, 6.5f, h);     // malam -> fajar
    }
    else
    {
        if(!g_warnedNoClock) { g_warnedNoClock = true; LOGI("[warn] CClock tidak ada, darkness diasumsikan 0.5 (atur ForceDarkness)"); }
        dark = 0.5f;
    }

    if(g_pCurrArea && *g_pCurrArea != 0) dark *= g_set.interiorScale; // di dalam interior
    return Clamp01(dark);
}

/* ------------------------------------------------------------------ */
/*  Entity: posisi & arah depan                                        */
/*  CPlaceable: vtable, CSimpleTransform(pos+heading), CMatrix*        */
/* ------------------------------------------------------------------ */
static bool ValidMatrix(const float* m)
{
    if(!m || ((uintptr_t)m & 3u) || (uintptr_t)m < 0x10000) return false;
    const int idx[] = { 4, 5, 6, 12, 13, 14 };
    for(int i : idx) if(!FiniteF(m[i])) return false;
    float l = m[4] * m[4] + m[5] * m[5] + m[6] * m[6];
    return l > 0.8f && l < 1.2f;
}

static bool EntityTransform(void* ent, CVec3* pos, CVec3* fwd)
{
    if(!ent) return false;
    const uintptr_t e = (uintptr_t)ent;

    const float* m = *(const float* const*)(e + BYBIT(0x14, 0x18));
    if(ValidMatrix(m))
    {
        if(pos) { pos->x = m[12]; pos->y = m[13]; pos->z = m[14]; }
        if(fwd) { fwd->x = m[4];  fwd->y = m[5];  fwd->z = m[6]; }
        return true;
    }

    const float* p = (const float*)(e + BYBIT(4, 8));
    if(FiniteF(p[0]) && FiniteF(p[1]) && FiniteF(p[2]) && FiniteF(p[3]))
    {
        if(pos) { pos->x = p[0]; pos->y = p[1]; pos->z = p[2]; }
        if(fwd) { fwd->x = -sinf(p[3]); fwd->y = cosf(p[3]); fwd->z = 0.0f; }
        return true;
    }
    return false;
}

/* ------------------------------------------------------------------ */
/*  State per-frame                                                    */
/* ------------------------------------------------------------------ */
struct ActiveLight { CVec3 p; float radius, r, g, b; };

static const int MAX_ACTIVE = 32;
static const int MAX_SEEN   = 32;

static ActiveLight g_active[MAX_ACTIVE];
static int         g_activeCount = 0;
static int         g_nSubmitted  = 0;     // jumlah AddLight milik plugin di frame ini
static int         g_nHeadVeh    = 0;
static int         g_nTailVeh    = 0;

static void*       g_headCur [MAX_SEEN]; static int g_headCurN  = 0;
static void*       g_headPrev[MAX_SEEN]; static int g_headPrevN = 0;
static void*       g_tailDone[MAX_SEEN]; static int g_tailDoneN = 0;

static bool        g_playerCached = false;
static void*       g_playerPed    = nullptr;
static bool        g_playerPosOk  = false;
static CVec3       g_playerPos    = {0, 0, 0};

static bool        g_timerHooked  = false;
static uint32_t    g_lastTimerMs  = 0;
static uint32_t    g_lastFrameKey = 0xFFFFFFFFu;

static void ResetFrame()
{
    g_activeCount = 0;
    g_nSubmitted  = 0;
    g_nHeadVeh    = 0;
    g_nTailVeh    = 0;

    memcpy(g_headPrev, g_headCur, sizeof(g_headCur));
    g_headPrevN = g_headCurN;
    g_headCurN  = 0;
    g_tailDoneN = 0;

    g_playerCached = false;
    g_playerPosOk  = false;
}

static void SyncFrame()
{
    // 1) hook CTimer::Update aktif & masih "berdetak" -> reset dilakukan di sana
    if(g_timerHooked && (NowMs() - g_lastTimerMs) < 250u) return;

    // 2) CTimer::m_FrameCounter, 3) bucket waktu 8 ms
    uint32_t key = g_pFrameCounter ? (uint32_t)(*g_pFrameCounter) : (NowMs() / 8u);
    if(key != g_lastFrameKey)
    {
        g_lastFrameKey = key;
        ResetFrame();
    }
}

static bool SeenIn(void* const* list, int n, void* p)
{
    for(int i = 0; i < n; ++i) if(list[i] == p) return true;
    return false;
}

static void* GetPlayerPed()
{
    if(!g_playerCached)
    {
        g_playerCached = true;
        g_playerPed = g_FindPlayerPed ? g_FindPlayerPed(-1) : nullptr;
    }
    return g_playerPed;
}

static bool GetPlayerPos(CVec3* out)
{
    if(!g_playerPosOk)
    {
        void* ped = GetPlayerPed();
        g_playerPosOk = (ped && EntityTransform(ped, &g_playerPos, nullptr));
        if(!g_playerPosOk) return false;
    }
    *out = g_playerPos;
    return true;
}

// 1.0 dekat pemain -> 0.0 di MaxDistance. Tanpa posisi pemain -> 1.0 (tidak ada culling jarak).
static float DistFade(const CVec3& p)
{
    CVec3 pp;
    if(!GetPlayerPos(&pp)) return 1.0f;
    float dx = p.x - pp.x, dy = p.y - pp.y, dz = p.z - pp.z;
    float d  = sqrtf(dx * dx + dy * dy + dz * dz);
    if(d <= g_set.fadeNear)    return 1.0f;
    if(d >= g_set.maxDistance) return 0.0f;
    return 1.0f - Smooth(g_set.fadeNear, g_set.maxDistance, d);
}

/* ------------------------------------------------------------------ */
/*  Kirim cahaya ke game (dengan budget)                               */
/* ------------------------------------------------------------------ */
static void SubmitLight(const CVec3& p, float radius, float r, float g, float b)
{
    if(!g_set.dynamicLights) return;
    if((r + g + b) < g_set.minIntensity) return;     // terlalu redup -> buang
    if(g_nSubmitted >= g_set.maxLights) return;      // jatah plugin habis
    if(g_pNumLights && *g_pNumLights >= (unsigned)g_set.maxTotalLights) return; // sisakan slot untuk game

    if(g_AddLight)
    {
        CVec3 zero = {0, 0, 0};
        g_AddLight(0 /*PLTYPE_POINTLIGHT*/, p, zero, radius, r, g, b, 0 /*FOGTYPE_NONE*/, false, nullptr);
    }
    ++g_nSubmitted;

    if(g_activeCount < MAX_ACTIVE)   // dipakai ped lighting (cahaya mengenai tubuh)
    {
        ActiveLight& a = g_active[g_activeCount++];
        a.p = p; a.radius = radius * 0.8f; a.r = r; a.g = g; a.b = b;
    }
}

/* ------------------------------------------------------------------ */
/*  Vehicle lighting                                                   */
/* ------------------------------------------------------------------ */
struct VehTrack { void* v; CVec3 pos; float speed; float brake; uint32_t ms; };
static VehTrack g_track[16];

static float UpdateBrake(void* veh, const CVec3& pos)
{
    const uint32_t now = NowMs();
    VehTrack* t = nullptr;
    VehTrack* oldest = &g_track[0];
    for(VehTrack& x : g_track)
    {
        if(x.v == veh) { t = &x; break; }
        if(x.ms < oldest->ms) oldest = &x;
    }
    if(!t || (now - t->ms) > 2000u)
    {
        if(!t) t = oldest;
        t->v = veh; t->pos = pos; t->speed = 0.f; t->brake = 0.f; t->ms = now;
        return 0.f;
    }

    const float dt = (float)(now - t->ms) / 1000.0f;
    if(dt < 0.02f) return t->brake;                  // jangan update lebih sering dari ~50 Hz

    float dx = pos.x - t->pos.x, dy = pos.y - t->pos.y, dz = pos.z - t->pos.z;
    float sp = sqrtf(dx * dx + dy * dy + dz * dz) / dt;
    if(sp > 90.0f) sp = t->speed;                    // teleport / respawn -> abaikan

    const float smooth = Lerp(t->speed, sp, 0.5f);
    const float decel  = (t->speed - smooth) / dt;
    float target = Clamp01(decel / g_set.brakeDecel);
    if(smooth < 0.3f) target = 0.f;

    t->brake = Lerp(t->brake, target, 0.3f);
    t->speed = smooth;
    t->pos   = pos;
    t->ms    = now;
    return t->brake;
}

static inline uintptr_t RegThis(GlossRegisters* r)
{
#ifdef AML32
    return (uintptr_t)r->regs.r0;
#else
    return (uintptr_t)r->regs.x0;
#endif
}

// dipanggil saat game memproses lampu depan sebuah kendaraan (this = CVehicle*)
static void OnHeadLight(GlossRegisters* regs, PHookHandle)
{
    if(!g_set.vehicleLighting) return;
    void* veh = (void*)RegThis(regs);
    if(!veh) return;

    SyncFrame();
    if(SeenIn(g_headCur, g_headCurN, veh)) return;   // 1 kendaraan = 1x per frame
    if(g_headCurN < MAX_SEEN) g_headCur[g_headCurN++] = veh;

    const float dark = GetDarkness();
    if(dark < 0.04f) return;
    if(g_nHeadVeh >= g_set.maxVehicles) return;

    CVec3 pos, fwd;
    if(!EntityTransform(veh, &pos, &fwd)) return;

    const float k = dark * DistFade(pos);
    if(k <= 0.0f) return;
    ++g_nHeadVeh;

    // satu "kolam" cahaya di depan kendaraan, sedikit di bawah pusat (jatuh ke jalan)
    CVec3 p = { pos.x + fwd.x * g_set.headDistance,
                pos.y + fwd.y * g_set.headDistance,
                pos.z + fwd.z * g_set.headDistance - 0.4f };
    const float i = g_set.headIntensity * k;
    SubmitLight(p, g_set.headRadius, 1.00f * i, 0.93f * i, 0.78f * i);
}

// dipanggil saat game memproses lampu belakang (this = CVehicle*)
static void OnTailLight(GlossRegisters* regs, PHookHandle)
{
    if(!g_set.vehicleLighting) return;
    void* veh = (void*)RegThis(regs);
    if(!veh) return;

    SyncFrame();
    if(SeenIn(g_tailDone, g_tailDoneN, veh)) return;
    if(g_tailDoneN < MAX_SEEN) g_tailDone[g_tailDoneN++] = veh;

    // hanya kendaraan yang lampu depannya menyala (frame ini atau sebelumnya)
    if(!SeenIn(g_headCur, g_headCurN, veh) && !SeenIn(g_headPrev, g_headPrevN, veh)) return;

    const float dark = GetDarkness();
    if(dark < 0.04f) return;
    if(g_nTailVeh >= g_set.maxVehicles) return;

    CVec3 pos, fwd;
    if(!EntityTransform(veh, &pos, &fwd)) return;

    const float k = dark * DistFade(pos);
    if(k <= 0.0f) return;
    ++g_nTailVeh;

    const float brake = UpdateBrake(veh, pos);
    CVec3 p = { pos.x - fwd.x * g_set.tailDistance,
                pos.y - fwd.y * g_set.tailDistance,
                pos.z - fwd.z * g_set.tailDistance + 0.2f };
    const float i = (g_set.tailIntensity + brake * g_set.brakeIntensity) * k;
    SubmitLight(p, g_set.tailRadius + 2.5f * brake, 1.00f * i, 0.06f * i, 0.03f * i);
}

static void OnTimerUpdate(GlossRegisters*, PHookHandle)
{
    g_lastTimerMs = NowMs();
    ResetFrame();
}

/* ------------------------------------------------------------------ */
/*  Ped lighting: night fill + cahaya lampu kendaraan                  */
/* ------------------------------------------------------------------ */
static inline float BoostChan(float base, float add, float cap)
{
    float v = base + add;
    if(v > cap) v = cap;
    return v < base ? base : v;   // tidak pernah menggelapkan
}

// CPed::SetupLighting(). Nilai return diteruskan apa adanya (void/bool sama-sama aman).
SDECL_HOOK(uintptr_t, PedSetupLighting, void* self)
{
    if(!g_set.pedLighting || !g_pAmbPeds) return PedSetupLighting(self);

    SyncFrame();
    const float dark = GetDarkness();
    const bool  isPlayer = (g_FindPlayerPed != nullptr) && (self == GetPlayerPed());
    const float fill = (isPlayer ? g_set.playerFill : g_set.npcFill) * dark;

    // sedikit kebiruan seperti cahaya bulan
    float ar = fill * 0.90f, ag = fill * 0.96f, ab = fill;

    // cahaya kendaraan di sekitar ped
    if(g_set.dynamicLights && g_activeCount > 0)
    {
        CVec3 pp;
        if(EntityTransform(self, &pp, nullptr))
        {
            for(int i = 0; i < g_activeCount; ++i)
            {
                const ActiveLight& L = g_active[i];
                float dx = pp.x - L.p.x, dy = pp.y - L.p.y, dz = pp.z - L.p.z;
                float a  = 1.0f - sqrtf(dx * dx + dy * dy + dz * dz) / L.radius;
                if(a <= 0.0f) continue;
                a *= a * g_set.pedLightGain;
                ar += L.r * a; ag += L.g * a; ab += L.b * a;
            }
        }
    }

    if(ar < 0.002f && ag < 0.002f && ab < 0.002f) return PedSetupLighting(self);

    const RwRGBAReal saved = *g_pAmbPeds;
    g_pAmbPeds->r = BoostChan(saved.r, ar, g_set.ambientCap);
    g_pAmbPeds->g = BoostChan(saved.g, ag, g_set.ambientCap);
    g_pAmbPeds->b = BoostChan(saved.b, ab, g_set.ambientCap);

    uintptr_t ret = PedSetupLighting(self);   // game membaca ambient yang sudah dinaikkan
    *g_pAmbPeds = saved;                      // kembalikan, supaya mobil/objek tidak ikut terang
    return ret;
}

/* ------------------------------------------------------------------ */
/*  API untuk mod lain                                                 */
/* ------------------------------------------------------------------ */
static void API_AddDynamicLight(float x, float y, float z, float radius, float r, float g, float b)
{
    SyncFrame();
    CVec3 p = { x, y, z };
    float k = DistFade(p);
    if(k <= 0.0f) return;
    SubmitLight(p, radius, r * k, g * k, b * k);
}
static float API_GetDarkness() { return GetDarkness(); }

static ILightingPlus g_api = { 1, API_AddDynamicLight, API_GetDarkness };

/* ------------------------------------------------------------------ */
/*  Pasang hook                                                        */
/* ------------------------------------------------------------------ */
static bool InstallInline(const char* what, uintptr_t addr, HookWithRegistersFn fn)
{
    if(!addr) { LOGI("[hook] %-22s : dilewati (alamat tidak ada)", what); return false; }
    PHookHandle h = aml->HookInline((void*)addr, fn, false);
    LOGI("[hook] %-22s : %s", what, h ? "OK" : "GAGAL");
    return h != nullptr;
}

ON_MOD_PRELOAD()
{
    logger->SetTag("LightingPlus");
}

ON_MOD_LOAD()
{
    logger->SetTag("LightingPlus");
    LOGI("LightingPlus 1.0.0 (%s) dimuat", BYBIT("32-bit", "64-bit"));

    g_pGame = aml->GetLib("libGTASA.so");
    if(!g_pGame) { LOGE("libGTASA.so tidak ditemukan, plugin berhenti"); return; }

    LoadSettings();
    cfg->Save();
    if(!g_set.enabled) { LOGI("Enabled=false -> plugin tidak aktif"); return; }

    if(!ElfSym::Init("libGTASA.so"))
        LOGI("[warn] .dynsym libGTASA.so tidak bisa dibaca; hanya [Offsets] manual yang bisa dipakai");

    /* ---- data game ---- */
    g_pHours        = (unsigned char*)Resolve("CClock hours",   "CClock_Hours",   { "6CClock", "ms_nGameClockHours"   }, false);
    g_pMinutes      = (unsigned char*)Resolve("CClock minutes", "CClock_Minutes", { "6CClock", "ms_nGameClockMinutes" }, false);
    g_pFrameCounter = (unsigned int*) Resolve("CTimer frame",   "CTimer_Frame",   { "6CTimer", "m_FrameCounter"       }, false);
    g_pCurrArea     = (unsigned int*) Resolve("CGame currArea", "CGame_CurrArea", { "5CGame", "currArea"              }, false);
    g_pNumLights    = (unsigned int*) Resolve("NumLights",      "NumLights",      { "12CPointLights", "9NumLights"    }, false);
    g_pAmbPeds      = (RwRGBAReal*)   Resolve("Ambient (ped)",  "AmbientPeds",    { "AmbientLightColourForFrame_PedsCarsAndObjects" }, false);

    /* ---- fungsi game ---- */
    g_FindPlayerPed = (FindPlayerPed_t)Resolve("FindPlayerPed", "FindPlayerPed", { "_Z13FindPlayerPedi" }, true);

    char addName[200] = {0};
    uintptr_t addAddr = 0;
    {
        size_t n = 0;
        uintptr_t off = 0;
        if(ConfigEntry* e = cfg->Bind("CPointLights_AddLight", "0", "Offsets")) { off = ParseHex(e->GetString()); delete e; }
        if(off) { addAddr = g_pGame + off; LOGI("[sym] %-22s = offset manual +0x%lx", "AddLight", (unsigned long)off); }
        else
        {
            addAddr = ElfSym::Find({ "_ZN12CPointLights8AddLight" }, true, addName, sizeof(addName), &n);
            if(addAddr)
            {
                // signature harus cocok dengan prototipe yang kita panggil
                if(strstr(addName, "AddLightEh7CVectorS0_ffffhbP7CEntity"))
                    LOGI("[sym] %-22s = %s (+0x%lx)", "AddLight", addName, (unsigned long)(addAddr - g_pGame));
                else
                {
                    LOGI("[sym] %-22s = %s  -> signature BEDA dari yang diharapkan, AddLight dinonaktifkan", "AddLight", addName);
                    addAddr = 0;
                }
            }
            else LOGI("[sym] %-22s = TIDAK DITEMUKAN", "AddLight");
        }
    }
    g_AddLight = (AddLight_t)addAddr;

    uintptr_t aPedLight = Resolve("CPed::SetupLighting", "CPed_SetupLighting", { "_ZN5CPed13SetupLighting" }, true);
    uintptr_t aTimer    = Resolve("CTimer::Update",      "CTimer_Update",      { "_ZN6CTimer6UpdateE" },     true);
    uintptr_t aHead = 0;
    switch(g_set.headTrigger)
    {
        case 1:  aHead = Resolve("Head trigger (effect)",  "CVehicle_DoHeadLightEffect", { "_ZN8CVehicle17DoHeadLightEffect" }, true); break;
        case 2:  aHead = Resolve("Head trigger (prerender)","CVehicle_PreRender",        { "_ZN8CVehicle9PreRender" },         true); break;
        default: aHead = Resolve("Head trigger (beam)",    "CVehicle_DoHeadLightBeam",   { "_ZN8CVehicle15DoHeadLightBeam" },  true); break;
    }
    uintptr_t aTail = Resolve("CVehicle::DoTailLight", "CVehicle_DoTailLightEffect", { "_ZN8CVehicle17DoTailLightEffect" }, true);

    /* ---- ringkasan kemampuan ---- */
    LOGI("[ringkas] ped lighting     : %s", (g_pAmbPeds && aPedLight) ? "SIAP" : "TIDAK SIAP (butuh Ambient + CPed::SetupLighting)");
    LOGI("[ringkas] vehicle lighting : %s", (aHead ? (g_AddLight ? "SIAP" : "SIAP tanpa AddLight (hanya memengaruhi ped)") : "TIDAK SIAP (butuh trigger lampu depan)"));
    LOGI("[ringkas] jam game         : %s", g_pHours ? "ada" : "tidak ada");

    if(g_set.probeOnly) { LOGI("ProbeOnly=true -> hook tidak dipasang. Cek log lalu set ProbeOnly=false"); return; }

    /* ---- hook ---- */
    if(aTimer) g_timerHooked = InstallInline("CTimer::Update", aTimer, OnTimerUpdate);

    if(g_pAmbPeds && aPedLight)
    {
        if(aml->Hook((void*)aPedLight, (void*)&HookOf_PedSetupLighting, (void**)&PedSetupLighting))
            LOGI("[hook] %-22s : OK", "CPed::SetupLighting");
        else
        {
            LOGE("[hook] CPed::SetupLighting : GAGAL");
            g_set.pedLighting = false;
        }
    }
    else g_set.pedLighting = false;

    if(aHead)
    {
        bool ok = InstallInline("Head light", aHead, OnHeadLight);
        if(ok && aTail) InstallInline("Tail light", aTail, OnTailLight);
        if(!ok) g_set.vehicleLighting = false;
    }
    else g_set.vehicleLighting = false;

    RegisterInterface("ILightingPlus", &g_api);
    LOGI("Selesai. PedLighting=%d VehicleLighting=%d DynamicLights=%d", g_set.pedLighting, g_set.vehicleLighting, g_set.dynamicLights);
}
