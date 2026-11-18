#!/data/data/com.termux/files/usr/bin/bash
# lp.sh - alat bantu LightingPlus di Termux: lihat log/crash, ubah config, cari hook penyebab crash.
#
# Pemakaian:  ./lp.sh <perintah> [argumen]      (atau: bash lp.sh help)
# Saran      : alias lp='bash ~/AML_LightingPlus/lp.sh'
#
# Catatan Android 11+: Termux tidak bisa membuka /sdcard/Android/data. Di sana pakai file manager
# (MT Manager / ZArchiver) untuk membuka folder log; log salinan di /sdcard/Documents/AMLModLogs/ bisa dibaca Termux.

PKG="${LP_PKG:-com.rockstargames.gtasa}"
BASE="${LP_BASE:-/sdcard}"
GUID="net.lightingplus.gtasa"

DATA_DIR="$BASE/Android/data/$PKG"
LOG_DIR="${LP_DIR:-$DATA_DIR/files/LightingPlus}"
DOC_LOG_DIR="$BASE/Documents/AMLModLogs/$GUID"

c_ok()  { printf '\033[32m%s\033[0m\n' "$*"; }
c_err() { printf '\033[31m%s\033[0m\n' "$*"; }
c_inf() { printf '\033[36m%s\033[0m\n' "$*"; }

need_storage() {
    if [ ! -d "$BASE" ] || [ ! -r "$BASE" ]; then
        c_err "Folder $BASE tidak bisa dibaca. Jalankan dulu:  termux-setup-storage"
        exit 1
    fi
}

# ---------- lokasi file ----------
where_get() { # kunci dari where.txt
    [ -f "$LOG_DIR/where.txt" ] && sed -n "s/^$1=//p" "$LOG_DIR/where.txt" | head -n1
}

find_log() { # $1 = nama file; pilih yang paling baru dari folder data & Documents
    local best="" f
    for f in "$LOG_DIR/$1" "$DOC_LOG_DIR/$1"; do
        [ -f "$f" ] || continue
        if [ -z "$best" ] || [ "$f" -nt "$best" ]; then best="$f"; fi
    done
    [ -n "$best" ] && echo "$best"
}

find_ini() {
    [ -n "$LP_INI" ] && [ -f "$LP_INI" ] && { echo "$LP_INI"; return; }
    local cfg f
    cfg="$(where_get config)"
    if [ -n "$cfg" ]; then
        [ -f "$cfg" ] && { echo "$cfg"; return; }
        if [ -d "$cfg" ]; then
            f="$(ls "$cfg"/*[Ll]ighting[Pp]lus*.ini "$cfg/$GUID.ini" 2>/dev/null | head -n1)"
            [ -n "$f" ] && { echo "$f"; return; }
        fi
    fi
    f="$(find "$DATA_DIR" "$BASE/AMLMods" "$BASE/AML" -maxdepth 5 \( -iname "*lightingplus*.ini" -o -iname "$GUID*.ini" \) 2>/dev/null | head -n1)"
    [ -n "$f" ] && echo "$f"
}

need_ini() {
    INI="$(find_ini)"
    if [ -z "$INI" ]; then
        c_err "File config belum ketemu. Jalankan game sekali (mod harus sempat termuat), atau set manual:"
        c_err "  LP_INI=/path/ke/$GUID.ini ./lp.sh $CMD ..."
        exit 1
    fi
}

# ---------- ubah ini (aman untuk format 'Key = Value' maupun 'Key=Value') ----------
ini_set() { # file section key value
    awk -v sec="$2" -v key="$3" -v val="$4" '
        function trim(s) { gsub(/^[ \t\r]+|[ \t\r]+$/, "", s); return s }
        BEGIN { insec = 0; done = 0; found = 0 }
        /^[ \t]*\[/ {
            if (insec && !done) { print key " = " val; done = 1 }
            name = $0; gsub(/^[ \t]*\[|\][ \t\r]*$/, "", name)
            insec = (tolower(name) == tolower(sec)); if (insec) found = 1
            print; next
        }
        insec && !done {
            k = $0; sub(/[=:].*/, "", k); k = trim(k)
            if (tolower(k) == tolower(key)) { print k " = " val; done = 1; next }
        }
        { print }
        END {
            if (!done) { if (!found) print "[" sec "]"; print key " = " val }
        }
    ' "$1" > "$1.lp_tmp" && cat "$1.lp_tmp" > "$1" && rm -f "$1.lp_tmp"
}

ini_get() { # file section key
    awk -v sec="$2" -v key="$3" '
        function trim(s) { gsub(/^[ \t\r]+|[ \t\r]+$/, "", s); return s }
        /^[ \t]*\[/ { name = $0; gsub(/^[ \t]*\[|\][ \t\r]*$/, "", name); insec = (tolower(name) == tolower(sec)); next }
        insec { k = $0; sub(/[=:].*/, "", k); k = trim(k)
                if (tolower(k) == tolower(key)) { v = $0; sub(/^[^=:]*[=:]/, "", v); print trim(v); exit } }
    ' "$1"
}

set_and_show() { # section key value
    need_ini
    cp -f "$INI" "$INI.bak" 2>/dev/null
    ini_set "$INI" "$1" "$2" "$3"
    c_ok "[$1] $2 = $3   ($INI)"
}

stop_game_hint() { c_inf "Tutup game sepenuhnya (swipe dari recent apps) sebelum menjalankan ulang."; }

# ---------- perintah ----------
CMD="${1:-help}"; shift 2>/dev/null

case "$CMD" in
help|-h|--help)
cat <<EOF
LightingPlus - alat bantu Termux

  Lihat hasil
    lp.sh where             lokasi log/config yang ditemukan
    lp.sh log [N]           N baris terakhir log run ini (default 80)
    lp.sh prev [N]          log run sebelumnya
    lp.sh crash             laporan crash + guard + penghitung
    lp.sh report            kumpulkan semuanya ke /sdcard/Download/LightingPlus_report.txt (kirim file ini)

  Ubah config (tutup game dulu)
    lp.sh get               tampilkan config
    lp.sh set <Section> <Key> <Value>      contoh: lp.sh set Vehicle HeadIntensity 0.4
    lp.sh probe             ProbeOnly=true  (hanya cek simbol, TANPA hook)
    lp.sh full              ProbeOnly=false (pasang hook)
    lp.sh on | off          Enabled=true/false (matikan semua mod)
    lp.sh hook <timer|ped|head|tail> <on|off>

  Cari hook penyebab crash (bertahap, tiap langkah jalankan game lalu 'lp.sh log')
    lp.sh step 1   hanya cek simbol (tanpa hook)
    lp.sh step 2   hook Timer saja
    lp.sh step 3   Timer + Ped
    lp.sh step 4   Timer + Head (lampu depan)
    lp.sh step 5   semua hook

  Lain-lain
    lp.sh reset             hapus guard/crash/penghitung (hook nyala kembali sesuai config)
    lp.sh install [path]    salin .so (out/ atau path) ke folder mods game
EOF
;;

where)
    need_storage
    echo "Paket game  : $PKG"
    echo "Folder log  : $LOG_DIR $([ -d "$LOG_DIR" ] && echo '(ada)' || echo '(TIDAK ada / tidak terbaca)')"
    echo "Log salinan : $DOC_LOG_DIR $([ -d "$DOC_LOG_DIR" ] && echo '(ada)' || echo '(tidak ada)')"
    echo "Config      : $(find_ini || true)"
    [ -f "$LOG_DIR/where.txt" ] && { echo "--- where.txt ---"; cat "$LOG_DIR/where.txt"; }
    if [ ! -d "$LOG_DIR" ] && [ ! -d "$DOC_LOG_DIR" ]; then
        c_err "Belum ada log sama sekali. Kemungkinan: (1) mod belum sempat dimuat AML, (2) Android 11+ memblokir Termux."
        c_err "Cek di file manager: Android/data/$PKG/files/LightingPlus/"
    fi
;;

log|prev)
    need_storage
    name="lightingplus.log"; [ "$CMD" = prev ] && name="lightingplus.prev.log"
    f="$(find_log "$name")"
    if [ -z "$f" ]; then c_err "$name tidak ditemukan. Jalankan: lp.sh where"; exit 1; fi
    c_inf "== $f =="
    tail -n "${1:-80}" "$f"
    echo
    c_inf "Baris TERAKHIR menunjukkan sampai mana mod berjalan sebelum crash ([stage] ... = tahap)."
;;

crash)
    need_storage
    shown=0
    for n in crash.txt crash.prev.txt guard.txt run.txt trying.txt; do
        f="$LOG_DIR/$n"
        [ -f "$f" ] || continue
        shown=1
        c_inf "== $n =="
        cat "$f"
        echo
    done
    [ $shown -eq 0 ] && echo "Tidak ada crash.txt / guard.txt. (Crash di luar jangkauan handler -> lihat baris terakhir: lp.sh log)"
;;

report)
    need_storage
    out="$BASE/Download/LightingPlus_report.txt"
    {
        echo "LightingPlus report - $(date)"
        echo "Android: $(getprop ro.build.version.release 2>/dev/null) SDK $(getprop ro.build.version.sdk 2>/dev/null), $(getprop ro.product.model 2>/dev/null)"
        for n in lightingplus.log lightingplus.prev.log crash.txt crash.prev.txt guard.txt run.txt trying.txt where.txt; do
            f="$(find_log "$n")"
            [ -n "$f" ] || continue
            echo; echo "################ $n ($f)"; cat "$f"
        done
        INI="$(find_ini)"
        [ -n "$INI" ] && { echo; echo "################ config ($INI)"; cat "$INI"; }
    } > "$out" 2>/dev/null
    c_ok "Laporan: $out"
    echo "Kirim isi file itu (atau filenya) untuk dianalisis."
;;

get)
    need_ini
    c_inf "== $INI =="
    cat "$INI"
;;

set)
    [ $# -ge 3 ] || { c_err "Pakai: lp.sh set <Section> <Key> <Value>"; exit 1; }
    set_and_show "$1" "$2" "$3"; stop_game_hint
;;

probe) set_and_show General ProbeOnly true;  stop_game_hint ;;
full)  set_and_show General ProbeOnly false; stop_game_hint ;;
on)    set_and_show General Enabled true;    stop_game_hint ;;
off)   set_and_show General Enabled false;   stop_game_hint ;;

hook)
    k="$(echo "$1" | tr 'A-Z' 'a-z')"; v="$(echo "$2" | tr 'A-Z' 'a-z')"
    case "$k" in timer) K=Timer;; ped) K=Ped;; head) K=Head;; tail) K=Tail;;
        *) c_err "Pakai: lp.sh hook <timer|ped|head|tail> <on|off>"; exit 1;; esac
    case "$v" in on|true|1) V=true;; off|false|0) V=false;;
        *) c_err "Pakai: lp.sh hook <timer|ped|head|tail> <on|off>"; exit 1;; esac
    set_and_show Hooks "$K" "$V"; stop_game_hint
;;

step)
    need_ini
    cp -f "$INI" "$INI.bak" 2>/dev/null
    ini_set "$INI" General Enabled true
    case "$1" in
        1) P=true;  T=true;  D=true;  H=true;  L=true ;;
        2) P=false; T=true;  D=false; H=false; L=false ;;
        3) P=false; T=true;  D=true;  H=false; L=false ;;
        4) P=false; T=true;  D=false; H=true;  L=false ;;
        5) P=false; T=true;  D=true;  H=true;  L=true ;;
        *) c_err "Pakai: lp.sh step <1-5>   (lihat: lp.sh help)"; exit 1 ;;
    esac
    ini_set "$INI" General ProbeOnly "$P"
    ini_set "$INI" Hooks Timer "$T"; ini_set "$INI" Hooks Ped "$D"
    ini_set "$INI" Hooks Head "$H";  ini_set "$INI" Hooks Tail "$L"
    c_ok "Langkah $1: ProbeOnly=$P  Timer=$T Ped=$D Head=$H Tail=$L"
    if [ -d "$LOG_DIR" ]; then
        rm -f "$LOG_DIR/guard.txt" "$LOG_DIR/run.txt" "$LOG_DIR/trying.txt"
        echo "(guard & penghitung crash di-reset supaya langkah ini diuji bersih)"
    fi
    stop_game_hint
    echo "Lalu buka game, tunggu +30 detik / sampai crash, dan jalankan:  lp.sh log   dan   lp.sh crash"
;;

reset)
    if [ -d "$LOG_DIR" ]; then
        rm -f "$LOG_DIR/guard.txt" "$LOG_DIR/run.txt" "$LOG_DIR/trying.txt" "$LOG_DIR/crash.txt" "$LOG_DIR/crash.prev.txt"
        c_ok "Guard, penghitung, dan crash.txt dihapus."
    else
        c_inf "Folder log tidak terbaca dari Termux (Android 11+?). Hapus guard.txt/run.txt/trying.txt lewat file manager."
    fi
;;

install)
    need_storage
    src="${1:-}"
    here="$(cd "$(dirname "$0")" && pwd)"
    files=()
    if [ -z "$src" ]; then
        for f in "$here"/out/libLightingPlus.so "$here"/out/libLightingPlus64.so \
                 "$here"/libs/armeabi-v7a/libLightingPlus.so "$here"/libs/arm64-v8a/libLightingPlus64.so; do
            [ -f "$f" ] && files+=("$f")
        done
    elif [ -d "$src" ]; then
        for f in "$src"/libLightingPlus*.so "$src"/mods/libLightingPlus*.so; do [ -f "$f" ] && files+=("$f"); done
    elif [ -f "$src" ]; then
        files+=("$src")
    fi
    [ ${#files[@]} -gt 0 ] || { c_err "File .so tidak ketemu. Pakai: lp.sh install /path/libLightingPlus64.so"; exit 1; }

    MODS="${LP_MODS:-}"
    if [ -z "$MODS" ]; then
        for d in "$DATA_DIR/mods" "$BASE/AMLMods/$PKG" "$BASE/AMLMods"; do
            if [ -d "$d" ]; then MODS="$d"; break; fi
        done
    fi
    [ -n "$MODS" ] || { c_err "Folder mods tidak ketemu. Set: LP_MODS=/folder/mods ./lp.sh install"; exit 1; }
    for f in "${files[@]}"; do cp -f "$f" "$MODS/" && c_ok "disalin: $(basename "$f") -> $MODS"; done
;;

*)
    c_err "Perintah tidak dikenal: $CMD"
    echo "Coba: lp.sh help"
    exit 1
;;
esac
