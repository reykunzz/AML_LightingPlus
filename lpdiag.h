// lpdiag.h - diagnostik LightingPlus (tahan crash)
//
//  1. Log mentah lewat fd + fsync  -> baris terakhir di file = tempat crash terjadi
//  2. Breadcrumb (Stage/Where)      -> dipakai laporan crash & crash-guard
//  3. Laporan crash (ON_GAME_CRASH) -> crash.txt: sinyal, alamat PC/LR (offset libGTASA & lib ini), hook tersangka
//  4. Crash-guard                   -> hook tersangka otomatis dimatikan di run berikutnya (guard.txt)
//
// Semua file ada di:  <AndroidDataPath>/LightingPlus/
//   biasanya /sdcard/Android/data/com.rockstargames.gtasa/files/LightingPlus/
//
//   lightingplus.log        log run ini        lightingplus.prev.log  log run sebelumnya
//   crash.txt               laporan crash      crash.prev.txt         laporan crash sebelumnya
//   guard.txt               hook yang dimatikan otomatis (hapus file ini / `lp reset` untuk reset)
//   run.txt, trying.txt     penanda internal   where.txt              lokasi config & folder log
#pragma once
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <time.h>
#include <dlfcn.h>
#include <signal.h>
#include <ucontext.h>

namespace Diag
{
    static int         g_fd        = -1;
    static bool        g_sync      = true;      // fsync tiap baris (dimatikan setelah run dianggap sehat)
    static bool        g_inited    = false;
    static char        g_dir[400]  = {0};
    static char        g_pLog[460], g_pPrev[460], g_pCrash[460], g_pCrashPrev[460];
    static char        g_pGuard[460], g_pRun[460], g_pTrying[460], g_pWhere[460];
    static uintptr_t   g_selfBase  = 0;
    static const char* volatile g_where = "idle";   // posisi kode saat ini (string literal)
    static const char* volatile g_stage = "init";   // tahap load terakhir

    // ---- guard ----
    struct Guard
    {
        bool timer = false, ped = false, head = false, tail = false, addlight = false;
        bool safe  = false;     // terlalu sering tidak sehat -> jangan pasang hook sama sekali
    };

    static inline void Join(char* out, size_t n, const char* name)
    {
        snprintf(out, n, "%s/%s", g_dir, name);
    }

    static inline void WriteFile(const char* path, const char* text)
    {
        int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if(fd < 0) return;
        ssize_t r = write(fd, text, strlen(text)); (void)r;
        fsync(fd);
        close(fd);
    }
    static inline void AppendFile(const char* path, const char* text)
    {
        int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if(fd < 0) return;
        ssize_t r = write(fd, text, strlen(text)); (void)r;
        fsync(fd);
        close(fd);
    }
    static inline bool ReadFile(const char* path, char* buf, size_t n)
    {
        buf[0] = 0;
        int fd = open(path, O_RDONLY);
        if(fd < 0) return false;
        ssize_t r = read(fd, buf, n - 1);
        close(fd);
        if(r < 0) r = 0;
        buf[r] = 0;
        return true;
    }

    // ---- log ----
    static inline void Raw(const char* fmt, ...)
    {
        if(g_fd < 0) return;
        char b[1100];
        time_t t = time(NULL);
        struct tm tmv;
        localtime_r(&t, &tmv);
        int n = snprintf(b, sizeof(b), "[%02d:%02d:%02d] ", tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
        va_list ap;
        va_start(ap, fmt);
        int m = vsnprintf(b + n, sizeof(b) - (size_t)n - 2, fmt, ap);
        va_end(ap);
        if(m < 0) m = 0;
        n += m;
        if(n > (int)sizeof(b) - 2) n = (int)sizeof(b) - 2;
        b[n++] = '\n';
        ssize_t r = write(g_fd, b, (size_t)n); (void)r;
        if(g_sync) fsync(g_fd);
    }

    static inline void Where(const char* w) { g_where = w; }

    // tandai tahap load: ditulis + di-fsync SEBELUM kode berisiko dijalankan
    static inline void Stage(const char* s)
    {
        g_stage = s;
        g_where = s;
        Raw("[stage] %s", s);
    }

    // trying.txt: ada isi = proses mati saat memasang hook itu (dihapus begitu pemasangan selesai)
    static inline void TryBegin(const char* key) { if(g_inited) WriteFile(g_pTrying, key); }
    static inline void TryEnd()                  { if(g_inited) unlink(g_pTrying); }

    static inline const char* KeyFromWhere(const char* w)
    {
        if(!w) return NULL;
        if(strstr(w, "Timer"))    return "timer";
        if(strstr(w, "AddLight")) return "addlight";
        if(strstr(w, "Ped"))      return "ped";
        if(strstr(w, "Tail"))     return "tail";
        if(strstr(w, "Head"))     return "head";
        return NULL;
    }

    static inline void ApplyKey(Guard& g, const char* key)
    {
        if(!key) return;
        if(!strcmp(key, "timer"))    g.timer    = true;
        if(!strcmp(key, "ped"))      g.ped      = true;
        if(!strcmp(key, "head"))     g.head     = true;
        if(!strcmp(key, "tail"))     g.tail     = true;
        if(!strcmp(key, "addlight")) g.addlight = true;
        if(!strcmp(key, "safe"))     g.safe     = true;
    }

    static inline void LoadGuard(Guard& g)
    {
        if(!g_inited) return;

        // proses sebelumnya mati saat memasang hook -> matikan hook itu
        char tr[64];
        if(ReadFile(g_pTrying, tr, sizeof(tr)) && tr[0])
        {
            char line[96];
            snprintf(line, sizeof(line), "disable=%s\n", tr);
            AppendFile(g_pGuard, line);
            Raw("[guard] run sebelumnya mati saat memasang '%s' -> dimatikan otomatis", tr);
        }
        unlink(g_pTrying);

        char buf[512];
        if(!ReadFile(g_pGuard, buf, sizeof(buf))) return;
        for(char* p = buf; *p; )
        {
            char* e = strchr(p, '\n');
            if(e) *e = 0;
            if(!strncmp(p, "disable=", 8)) ApplyKey(g, p + 8);
            else if(!strncmp(p, "safe=1", 6)) g.safe = true;
            if(!e) break;
            p = e + 1;
        }
    }

    // jumlah run berturut-turut yang tidak pernah "sehat"
    static inline int  ReadRun()
    {
        char b[32];
        if(!ReadFile(g_pRun, b, sizeof(b))) return 0;
        return atoi(b);
    }
    static inline void WriteRun(int n)
    {
        char b[32];
        snprintf(b, sizeof(b), "%d\n", n);
        WriteFile(g_pRun, b);
    }

    // dipanggil dari hook; run dianggap sehat setelah >=20 dtk dan >=600 tick
    static uint32_t g_startMs = 0;
    static uint32_t g_ticks   = 0;
    static bool     g_healthy = false;
    static inline uint32_t MsNow()
    {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
    }
    static inline void Tick()
    {
        if(g_healthy) return;
        if(++g_ticks < 600) return;
        if((MsNow() - g_startMs) < 20000u) return;
        g_healthy = true;
        WriteRun(0);
        Raw("[sehat] run stabil (%u tick) -> penghitung crash di-reset", (unsigned)g_ticks);
        g_sync = false;
    }

    // ---- Init ----
    static inline bool Init(const char* androidDataPath, const char* cfgPath, const char* pkg)
    {
        if(g_inited) return true;
        if(!androidDataPath || !androidDataPath[0]) return false;

        size_t L = strlen(androidDataPath);
        snprintf(g_dir, sizeof(g_dir), "%s%sLightingPlus", androidDataPath,
                 (androidDataPath[L - 1] == '/') ? "" : "/");
        aml->CreateDirRecursive(g_dir);

        Join(g_pLog,       sizeof(g_pLog),       "lightingplus.log");
        Join(g_pPrev,      sizeof(g_pPrev),      "lightingplus.prev.log");
        Join(g_pCrash,     sizeof(g_pCrash),     "crash.txt");
        Join(g_pCrashPrev, sizeof(g_pCrashPrev), "crash.prev.txt");
        Join(g_pGuard,     sizeof(g_pGuard),     "guard.txt");
        Join(g_pRun,       sizeof(g_pRun),       "run.txt");
        Join(g_pTrying,    sizeof(g_pTrying),    "trying.txt");
        Join(g_pWhere,     sizeof(g_pWhere),     "where.txt");

        // simpan log & crash run sebelumnya
        rename(g_pLog, g_pPrev);
        rename(g_pCrash, g_pCrashPrev);

        g_fd = open(g_pLog, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if(g_fd < 0) return false;
        g_inited  = true;
        g_startMs = MsNow();

        Dl_info di;
        if(dladdr((void*)&Init, &di)) g_selfBase = (uintptr_t)di.dli_fbase;

        char w[900];
        snprintf(w, sizeof(w), "pkg=%s\nlogdir=%s\nconfig=%s\n", pkg ? pkg : "?", g_dir, cfgPath ? cfgPath : "?");
        WriteFile(g_pWhere, w);

        Raw("=== LightingPlus diag dimulai ===");
        Raw("folder log : %s", g_dir);
        Raw("folder cfg : %s", cfgPath ? cfgPath : "?");
        return true;
    }

    // ---- laporan crash (async-signal-safe: hanya write() + formatter sendiri) ----
    struct Buf { char b[1200]; size_t n = 0; };
    static inline void Put(Buf& o, const char* s)
    {
        while(*s && o.n < sizeof(o.b) - 1) o.b[o.n++] = *s++;
        o.b[o.n] = 0;
    }
    static inline void PutHex(Buf& o, uintptr_t v)
    {
        char t[2 + sizeof(uintptr_t) * 2 + 1];
        int  i = 0;
        t[i++] = '0'; t[i++] = 'x';
        bool started = false;
        for(int s = (int)sizeof(uintptr_t) * 8 - 4; s >= 0; s -= 4)
        {
            int d = (int)((v >> s) & 0xF);
            if(d || started || s == 0) { t[i++] = "0123456789abcdef"[d]; started = true; }
        }
        t[i] = 0;
        Put(o, t);
    }
    static inline void PutDec(Buf& o, long v)
    {
        char t[24];
        int  i = 0;
        if(v < 0) { Put(o, "-"); v = -v; }
        if(v == 0) t[i++] = '0';
        while(v > 0 && i < 22) { t[i++] = (char)('0' + (v % 10)); v /= 10; }
        char r[24];
        int  j = 0;
        while(i > 0) r[j++] = t[--i];
        r[j] = 0;
        Put(o, r);
    }
    static inline void PutRel(Buf& o, const char* label, uintptr_t addr, uintptr_t base, uintptr_t maxLen)
    {
        Put(o, label);
        if(base && addr >= base && (addr - base) < maxLen) { Put(o, "+"); PutHex(o, addr - base); }
        else Put(o, "-");
        Put(o, " ");
    }

    static inline void CrashDump(const char* lib, int sig, int code, uintptr_t libaddr,
                                 uintptr_t pc, uintptr_t lr, uintptr_t sp, uintptr_t gameBase)
    {
        if(!g_inited) return;
        Buf o;
        Put(o, "LightingPlus crash report\n");
        Put(o, "sinyal="); PutDec(o, sig);
        Put(o, " kode=");  PutDec(o, code);
        Put(o, " lib=");   Put(o, lib ? lib : "?");
        Put(o, " libaddr="); PutHex(o, libaddr);
        Put(o, "\npc="); PutHex(o, pc);
        Put(o, "  lr="); PutHex(o, lr);
        Put(o, "  sp="); PutHex(o, sp);
        Put(o, "\n");
        PutRel(o, "pc@libGTASA ", pc, gameBase, 0x8000000);
        PutRel(o, "lr@libGTASA ", lr, gameBase, 0x8000000);
        PutRel(o, "pc@LightingPlus ", pc, g_selfBase, 0x200000);
        PutRel(o, "lr@LightingPlus ", lr, g_selfBase, 0x200000);
        Put(o, "\nwhere="); Put(o, (const char*)g_where);
        Put(o, "\nstage="); Put(o, (const char*)g_stage);
        Put(o, "\n");

        const char* key = KeyFromWhere((const char*)g_where);
        if(key)
        {
            Put(o, "tersangka="); Put(o, key);
            Put(o, "  (otomatis dimatikan di run berikutnya)\n");
        }
        else Put(o, "tersangka=tidak ada (crash di luar kode LightingPlus)\n");

        int fd = open(g_pCrash, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if(fd >= 0)
        {
            ssize_t r = write(fd, o.b, o.n); (void)r;
            fsync(fd);
            close(fd);
        }
        if(g_fd >= 0)
        {
            ssize_t r = write(g_fd, o.b, o.n); (void)r;
            fsync(g_fd);
        }
        if(key)
        {
            Buf gl;
            Put(gl, "disable="); Put(gl, key); Put(gl, "\n");
            int gfd = open(g_pGuard, O_WRONLY | O_CREAT | O_APPEND, 0644);
            if(gfd >= 0)
            {
                ssize_t r = write(gfd, gl.b, gl.n); (void)r;
                fsync(gfd);
                close(gfd);
            }
        }
    }
}
