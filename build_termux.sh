#!/data/data/com.termux/files/usr/bin/bash
# Build lokal di Termux.
#  - Jalur utama dan paling andal untuk 32-bit + 64-bit: push ke GitHub (lihat push_github_termux.sh),
#    lalu ambil hasilnya dari tab Actions -> Artifacts.
#  - Skrip ini mencoba build langsung di HP.
set -e
cd "$(dirname "$0")"
mkdir -p out

# 1) Kalau ndk-build tersedia (mis. NDK versi Termux aarch64), pakai itu: hasilnya 32-bit + 64-bit
if command -v ndk-build >/dev/null 2>&1; then
    echo "[+] ndk-build ditemukan"
    ndk-build -j"$(nproc)" NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=./Android.mk NDK_APPLICATION_MK=./Application.mk
    cp -v libs/armeabi-v7a/libLightingPlus.so out/ || true
    cp -v libs/arm64-v8a/libLightingPlus64.so out/ || true
    echo "[OK] hasil ada di: $(pwd)/out"
    exit 0
fi

# 2) Tanpa NDK: pakai clang bawaan Termux (target-nya native arm64) -> hanya 64-bit
echo "[!] ndk-build tidak ada -> build 64-bit saja dengan clang Termux"
pkg install -y clang >/dev/null 2>&1 || true

SRC="main.cpp mod/logger.cpp mod/config.cpp mod/flogger.cpp"
FLAGS="-shared -fPIC -O2 -DNDEBUG -std=c++17 -I. -Imod"

if clang++ $FLAGS $SRC -llog -static-libstdc++ -o out/libLightingPlus64.so 2>/tmp/lp_err.txt; then
    echo "[OK] out/libLightingPlus64.so (static libc++)"
elif clang++ $FLAGS $SRC -llog -o out/libLightingPlus64.so; then
    echo "[OK] out/libLightingPlus64.so (memakai libc++_shared bawaan Termux)"
    echo "[!] Library ini bisa butuh libc++_shared.so di HP/game. Untuk hasil paling aman pakai build GitHub Actions."
else
    cat /tmp/lp_err.txt
    echo "[X] Gagal. Pakai GitHub Actions (push_github_termux.sh)."
    exit 1
fi
echo "[i] 32-bit tidak bisa dibuat di sini tanpa NDK -> ambil dari GitHub Actions."
