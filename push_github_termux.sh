#!/data/data/com.termux/files/usr/bin/bash
# Push project ini ke GitHub dari Termux supaya GitHub Actions membangun 32-bit + 64-bit.
#
# Cara pakai:
#   1. Buat repo kosong di GitHub (mis. LightingPlus).
#   2. Login:  pkg install -y git gh && gh auth login
#              (atau pakai Personal Access Token sebagai password saat git push)
#   3. Jalankan:  ./push_github_termux.sh https://github.com/USERNAME/LightingPlus.git
#   4. Buka tab Actions -> run terbaru -> Artifacts -> LightingPlus
set -e
cd "$(dirname "$0")"

REMOTE="$1"
if [ -z "$REMOTE" ]; then
    echo "Pakai: $0 https://github.com/USERNAME/REPO.git"
    exit 1
fi

pkg install -y git >/dev/null 2>&1 || true
git config user.name  >/dev/null 2>&1 || git config user.name  "LightingPlus"
git config user.email >/dev/null 2>&1 || git config user.email "lightingplus@users.noreply.github.com"

[ -d .git ] || git init -q
git add -A
git commit -m "LightingPlus: build 32-bit + 64-bit" || true
git branch -M main
git remote remove origin 2>/dev/null || true
git remote add origin "$REMOTE"
git push -u origin main

echo
echo "[OK] Terkirim. Lihat hasil build di: ${REMOTE%.git}/actions"
echo "     Mau buat Release otomatis?  git tag v1.0.0 && git push origin v1.0.0"
