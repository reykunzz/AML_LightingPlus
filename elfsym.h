// elfsym.h - pencari simbol .dynsym libGTASA.so (tanpa perlu tahu signature lengkap)
// Dipakai supaya plugin tidak bergantung pada offset hardcode per versi game.
#pragma once
#include <link.h>
#include <elf.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <initializer_list>

namespace ElfSym
{
    struct Table
    {
        uintptr_t         base   = 0;
        const char*       strtab = nullptr;
        const ElfW(Sym)*  symtab = nullptr;
        size_t            count  = 0;
        bool              ready  = false;
    };

    static Table g_tab;

    struct FindCtx { const char* libName; Table* out; };

    static inline uintptr_t Fix(uintptr_t base, uintptr_t p) { return (p >= base) ? p : (p + base); }

    static size_t GnuHashCount(const uint32_t* h)
    {
        const uint32_t nbuckets   = h[0];
        const uint32_t symoffset  = h[1];
        const uint32_t bloomSize  = h[2];
        const ElfW(Addr)* bloom   = (const ElfW(Addr)*)(h + 4);
        const uint32_t* buckets   = (const uint32_t*)(bloom + bloomSize);
        const uint32_t* chain     = buckets + nbuckets;

        uint32_t maxIdx = 0;
        for(uint32_t i = 0; i < nbuckets; ++i) if(buckets[i] > maxIdx) maxIdx = buckets[i];
        if(maxIdx < symoffset) return symoffset;
        while(!(chain[maxIdx - symoffset] & 1u)) ++maxIdx;
        return (size_t)maxIdx + 1;
    }

    static int PhdrCb(struct dl_phdr_info* info, size_t, void* data)
    {
        FindCtx* c = (FindCtx*)data;
        if(!info->dlpi_name || !strstr(info->dlpi_name, c->libName)) return 0;

        const ElfW(Dyn)* dyn = nullptr;
        for(int i = 0; i < (int)info->dlpi_phnum; ++i)
        {
            if(info->dlpi_phdr[i].p_type == PT_DYNAMIC)
            {
                dyn = (const ElfW(Dyn)*)(info->dlpi_addr + info->dlpi_phdr[i].p_vaddr);
                break;
            }
        }
        if(!dyn) return 0;

        const uintptr_t base = (uintptr_t)info->dlpi_addr;
        const uint32_t* hash = nullptr;
        const uint32_t* gnu  = nullptr;
        Table* t = c->out;

        for(; dyn->d_tag != DT_NULL; ++dyn)
        {
            switch(dyn->d_tag)
            {
                case DT_SYMTAB:   t->symtab = (const ElfW(Sym)*)Fix(base, dyn->d_un.d_ptr); break;
                case DT_STRTAB:   t->strtab = (const char*)Fix(base, dyn->d_un.d_ptr);      break;
                case DT_HASH:     hash      = (const uint32_t*)Fix(base, dyn->d_un.d_ptr);  break;
                case DT_GNU_HASH: gnu       = (const uint32_t*)Fix(base, dyn->d_un.d_ptr);  break;
                default: break;
            }
        }
        if(!t->symtab || !t->strtab) return 0;

        if(hash)     t->count = hash[1];
        else if(gnu) t->count = GnuHashCount(gnu);
        else         return 0;

        t->base  = base;
        t->ready = (t->count > 0);
        return 1; // stop iterasi
    }

    // libName contoh: "libGTASA.so"
    static bool Init(const char* libName)
    {
        g_tab = Table();
        FindCtx ctx = { libName, &g_tab };
        dl_iterate_phdr(PhdrCb, &ctx);
        return g_tab.ready;
    }

    static inline bool ContainsAll(const char* s, std::initializer_list<const char*> tokens)
    {
        for(const char* tk : tokens) if(!strstr(s, tk)) return false;
        return true;
    }

    // Cari simbol pertama yang namanya memuat SEMUA token.
    //  func=true  -> hanya STT_FUNC,  func=false -> hanya STT_OBJECT
    //  outName    -> nama simbol yang ketemu (untuk log), matches -> jumlah kandidat
    static uintptr_t Find(std::initializer_list<const char*> tokens, bool func,
                          char* outName = nullptr, size_t outLen = 0, size_t* matches = nullptr)
    {
        if(matches) *matches = 0;
        if(!g_tab.ready) return 0;

        uintptr_t first = 0;
        size_t n = 0;
        for(size_t i = 0; i < g_tab.count; ++i)
        {
            const ElfW(Sym)* s = &g_tab.symtab[i];
            if(s->st_shndx == SHN_UNDEF || s->st_value == 0 || s->st_name == 0) continue;

            const unsigned type = (unsigned)(s->st_info & 0xF);
            if(func  && type != STT_FUNC)   continue;
            if(!func && type != STT_OBJECT) continue;

            const char* name = g_tab.strtab + s->st_name;
            if(!ContainsAll(name, tokens)) continue;

            if(n == 0)
            {
                first = g_tab.base + (uintptr_t)s->st_value; // bit Thumb (32-bit) ikut terbawa, sama seperti dlsym
                if(outName && outLen)
                {
                    strncpy(outName, name, outLen - 1);
                    outName[outLen - 1] = 0;
                }
            }
            ++n;
        }
        if(matches) *matches = n;
        return first;
    }

    // Daftar simbol (nama + alamat) yang memuat semua token; untuk diagnosa
    static size_t List(std::initializer_list<const char*> tokens, bool func,
                       char names[][160], uintptr_t* addrs, size_t maxN)
    {
        size_t n = 0;
        if(!g_tab.ready) return 0;
        for(size_t i = 0; i < g_tab.count && n < maxN; ++i)
        {
            const ElfW(Sym)* s = &g_tab.symtab[i];
            if(s->st_shndx == SHN_UNDEF || s->st_value == 0 || s->st_name == 0) continue;
            const unsigned type = (unsigned)(s->st_info & 0xF);
            if(func && type != STT_FUNC) continue;
            if(!func && type != STT_OBJECT) continue;
            const char* name = g_tab.strtab + s->st_name;
            if(!ContainsAll(name, tokens)) continue;
            strncpy(names[n], name, 159); names[n][159] = 0;
            addrs[n] = g_tab.base + (uintptr_t)s->st_value;
            ++n;
        }
        return n;
    }
}
