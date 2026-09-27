#!/bin/bash
# Builds iptv-player.nro in the devkitPro container, from the repository root:
#   docker run --rm -v "$PWD:/data" devkitpro/devkita64 bash /data/scripts/build_switch.sh
# The result is cmake-build-switch/iptv-player.nro
set -e
BUILD_DIR=cmake-build-switch

cd "$(dirname "$0")/.."
git config --global --add safe.directory "$(pwd)" || true

# The prebuilt libraries of wiliwili: mpv 0.36 and FFmpeg 6.1 with OpenGL
BASE_URL="https://github.com/xfangfang/wiliwili/releases/download/v0.1.0/"
PKGS=(
    "switch-libass-0.17.1-1-any.pkg.tar.zst"
    "switch-ffmpeg-6.1-5-any.pkg.tar.zst"
    "switch-libmpv-0.36.0-2-any.pkg.tar.zst"
    "switch-nspmini-48d4fc2-1-any.pkg.tar.xz"
    "hacBrewPack-3.05-1-any.pkg.tar.zst"
)
mkdir -p .packages
for PKG in "${PKGS[@]}"; do
    [ -s ".packages/${PKG}" ] || curl -fsSL --retry 3 -o ".packages/${PKG}" "${BASE_URL}${PKG}"
done
dkp-pacman -U --noconfirm "${PKGS[@]/#/.packages/}"

cmake -B ${BUILD_DIR} \
    -DCMAKE_BUILD_TYPE=Release \
    -DPLATFORM_SWITCH=ON \
    -DUSE_DEKO3D=OFF \
    -DBUILTIN_NSP=OFF \
    -DBRLS_UNITY_BUILD=OFF \
    -DTMDB_API_KEY="${TMDB_API_KEY:-}"
make -C ${BUILD_DIR} iptv-player.nro -j"$(nproc)"
echo "Built ${BUILD_DIR}/iptv-player.nro"
