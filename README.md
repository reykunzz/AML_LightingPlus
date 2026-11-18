# LightingPlus — plugin AML untuk GTA San Andreas Android (32-bit & 64-bit)

| Fitur | Isi |
|---|---|
| **Ped Lighting** | CJ & NPC mendapat "night fill" sehingga tidak gelap pekat saat malam, plus kena cahaya lampu kendaraan di dekatnya |
| **Vehicle Lighting** | Lampu depan menerangi jalan, lampu belakang merah, lampu rem lebih terang saat kendaraan melambat |
| **Dynamic Light** | Budget jumlah cahaya, fade berdasarkan jarak, API `ILightingPlus` untuk mod lain |

> **Status:** kode lolos cek sintaks untuk 32-bit dan 64-bit, tetapi **belum diuji di perangkat/game**.
> Plugin mencari alamat lewat simbol `.dynsym` `libGTASA.so`. Kalau simbol tidak ada di versi game kamu,
> fitur terkait otomatis mati (tidak crash) dan alasannya ditulis ke log. Lihat bagian "Run pertama".

## 1. Build lewat GitHub (disarankan, hasil 32-bit + 64-bit)

Di Termux:

```bash
pkg install -y git gh
gh auth login                      # login GitHub (atau pakai Personal Access Token)
cd AML_LightingPlus
chmod +x *.sh
./push_github_termux.sh https://github.com/USERNAME/LightingPlus.git
```

Lalu buka repo di GitHub → tab **Actions** → run terbaru → **Artifacts → LightingPlus**.
Isinya `libLightingPlus.so` (32-bit) dan `libLightingPlus64.so` (64-bit). Mau Release otomatis:
`git tag v1.0.0 && git push origin v1.0.0`.

## 2. Build langsung di Termux (opsional)

```bash
./build_termux.sh
```

- Kalau `ndk-build` ada (NDK versi Termux aarch64): hasil 32-bit + 64-bit di folder `out/`.
- Kalau tidak ada: hanya 64-bit dengan clang Termux. 32-bit ambil dari GitHub Actions.

## 3. Pasang

Salin `.so` ke folder mods game (sama seperti mod AML lain):
`/sdcard/Android/data/com.rockstargames.gtasa/mods/`
Game 32-bit memakai `libLightingPlus.so`, game 64-bit memakai `libLightingPlus64.so`.

## 4. Run pertama (penting)

Sejak v1.0.1 `ProbeOnly` **default true**: run pertama hanya mengecek simbol dan menulis log, tanpa memasang hook.

1. Jalankan game sekali (ProbeOnly sudah true).
2. Buka log (di Termux: `./lp.sh log`). Lokasi file:
   - `/sdcard/Android/data/com.rockstargames.gtasa/files/LightingPlus/lightingplus.log` (utama, selalu ditulis)
   - `/sdcard/Documents/AMLModLogs/net.lightingplus.gtasa/lightingplus.log` (salinan)
3. Cari baris `[ringkas]`:
   - `ped lighting : SIAP`, `vehicle lighting : SIAP` → `./lp.sh full` (ProbeOnly=false).
   - `TIDAK SIAP` → lihat baris `[sym]` di atasnya, simbol mana yang tidak ketemu.
4. Untuk tes di siang hari: `ForceDarkness=1.0`.

Kalau ada simbol tidak ketemu, kirim isi log-nya. Alamat juga bisa diisi manual di `[Offsets]`
(hex, relatif ke base `libGTASA.so`; untuk fungsi Thumb 32-bit tambahkan `+1`).

## 4b. Kalau game force close (cara melacak)

Folder log: `/sdcard/Android/data/com.rockstargames.gtasa/files/LightingPlus/`

| File | Isi |
|---|---|
| `lightingplus.log` | log run terakhir, ditulis + fsync per baris. **Baris terakhir = tempat berhenti.** `[stage]` = tahap load, `[dbg]` = panggilan hook pertama |
| `lightingplus.prev.log` | log run sebelumnya |
| `crash.txt` | laporan crash dari AML: sinyal, alamat `pc`/`lr` (offset di libGTASA dan di LightingPlus), hook tersangka |
| `guard.txt` | hook yang **dimatikan otomatis** karena tersangka crash / mati saat dipasang |
| `where.txt` | lokasi folder log dan file config |

Crash-guard: hook yang jadi tersangka crash otomatis dimatikan di run berikutnya. Kalau 2 run berturut-turut tidak stabil
(game ditutup/crash sebelum 20 detik), mod masuk SAFE MODE (hook tidak dipasang) sampai `./lp.sh reset`.

Sanity check baru: alamat fungsi harus di dalam libGTASA dan executable; nilai data (jam, NumLights, ambient ped)
dibaca dan dicek masuk akal. Kalau tidak, simbolnya dianggap salah dan fitur terkait dimatikan, bukan di-hook.

### Termux (`lp.sh`)

```bash
termux-setup-storage              # sekali saja
cd AML_LightingPlus && chmod +x lp.sh
./lp.sh log                       # 80 baris terakhir log
./lp.sh crash                     # crash.txt + guard.txt
./lp.sh report                    # kumpulkan semua ke /sdcard/Download/LightingPlus_report.txt
./lp.sh set Vehicle HeadIntensity 0.4
./lp.sh hook head off             # matikan satu hook (timer|ped|head|tail)
./lp.sh probe | full | on | off
./lp.sh reset                     # hapus guard/crash/penghitung
```

Mencari hook penyebab (tutup game total tiap langkah, jalankan, lalu `./lp.sh log` dan `./lp.sh crash`):
`./lp.sh step 1` (tanpa hook) → `step 2` (Timer) → `step 3` (Timer+Ped) → `step 4` (Timer+Head) → `step 5` (semua).
Langkah pertama yang crash menunjuk hook penyebab.

Android 11+: Termux tidak bisa membuka `/sdcard/Android/data`. Buka folder log lewat file manager, atau pakai salinan di
`/sdcard/Documents/AMLModLogs/...` (bisa dibaca Termux). Config juga harus diedit lewat file manager di sana.

## 5. Konfigurasi (file ini dibuat otomatis oleh AML)

| Section | Key | Default | Keterangan |
|---|---|---|---|
| General | Enabled | true | Matikan semua |
| General | ProbeOnly | true | Hanya cek simbol, tanpa hook (set false setelah log aman) |
| Hooks | Timer / Ped / Head / Tail | true | Saklar per-hook untuk mencari penyebab crash |
| General | ForceDarkness | -1 | 0..1 paksa tingkat gelap, -1 = ikut jam game |
| General | InteriorScale | 0.0 | Pengali efek malam di dalam interior |
| Features | PedLighting / VehicleLighting / DynamicLights | true | Saklar fitur |
| Ped | PlayerNightFill | 0.32 | Terang tambahan untuk CJ saat malam |
| Ped | NpcNightFill | 0.28 | Terang tambahan untuk NPC saat malam |
| Ped | VehicleLightGain | 0.9 | Kekuatan cahaya kendaraan pada ped |
| Ped | AmbientCap | 0.85 | Batas atas ambient ped |
| Vehicle | HeadTrigger | 0 | 0 DoHeadLightBeam, 1 DoHeadLightEffect, 2 CVehicle::PreRender |
| Vehicle | HeadDistance / HeadRadius / HeadIntensity | 5.5 / 11 / 0.55 | Kolam cahaya lampu depan |
| Vehicle | TailDistance / TailRadius / TailIntensity | 2.6 / 3.5 / 0.25 | Lampu belakang |
| Vehicle | BrakeIntensity / BrakeDecel | 0.55 / 5.0 | Rem (dideteksi dari perlambatan, m/s²) |
| Vehicle | MaxVehicles | 4 | Kendaraan ber-lampu per frame |
| Dynamic | MaxLights | 8 | Jatah cahaya plugin per frame |
| Dynamic | MaxTotalLights | 28 | Plugin berhenti menambah jika cahaya game sudah segini (batas game 32) |
| Dynamic | FadeNear / MaxDistance | 18 / 55 | Intensitas penuh sampai FadeNear, 0 di MaxDistance |

Kalau NPC masih terasa gelap: naikkan `NpcNightFill` (mis. 0.4) dan `AmbientCap`.

## 6. Batasan yang perlu diketahui

- Cahaya titik bawaan game memengaruhi ped, kendaraan, dan objek dinamis. Bangunan/jalan yang sudah
  "prelit" tidak ikut menyala; efek ke lingkungan terutama terlihat lewat objek dinamis di sekitar.
- Lampu belakang hanya muncul untuk kendaraan yang lampu depannya diproses game pada frame itu.
- Deteksi rem memakai perlambatan kendaraan, bukan input pedal.

## 7. Untuk developer mod lain

```cpp
#include "ilightingplus.h"
ILightingPlus* L = (ILightingPlus*)GetInterface("ILightingPlus");
if(L) L->AddDynamicLight(x, y, z, 10.0f, 1.0f, 0.4f, 0.1f);   // panggil tiap frame
```

## Lisensi
Header & logger AML di folder `mod/` berlisensi MIT (lihat `LICENSE-AML`).
