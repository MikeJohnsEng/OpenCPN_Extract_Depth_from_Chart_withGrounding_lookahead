#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# cm93depth_pi - build on Linux (Debian, Ubuntu, Raspberry Pi OS, Mint ...)
# for the OpenCPN installed from the distribution or the OpenCPN PPA, and
# package it as a plugin tarball that OpenCPN installs itself:
#   Options > Plugins > Import plugin...
#
# Works on x86_64 PCs and on Raspberry Pi (arm64 or armhf). Run from this
# folder or anywhere:
#     bash linux/build_linux.sh            build and package
#     bash linux/build_linux.sh --install  also copy it into this user's
#                                          plugin folder and enable it
#
# Result: linux/dist/cm93depth_pi-<version>-0_<distro>-<release>-<arch>.tar.gz
# Install it on any PC with the same distribution release and architecture.
#
# Needs OpenCPN 5.8 or later (plugin API 1.18). Raspberry Pi OS / Debian 12
# "bookworm" ship OpenCPN 5.6, which is too old: use Debian 13 "trixie" (or
# newer) or the Flatpak OpenCPN; see README-linux.txt for the Flatpak.
# ---------------------------------------------------------------------------
set -euo pipefail

INSTALL=0
for a in "$@"; do
  case "$a" in
    --install) INSTALL=1 ;;
    -h|--help) sed -n '2,21p' "$0"; exit 0 ;;
    *) echo "Unknown option: $a"; exit 1 ;;
  esac
done

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$(cd "$HERE/.." && pwd)"
WORK="$HERE/work"
DIST="$HERE/dist"
step() { printf '\n==> %s\n' "$*"; }
fail() { printf '\nERROR: %s\n' "$*" >&2; exit 1; }

# ---- 1. OpenCPN --------------------------------------------------------------
step "Looking for OpenCPN"
if command -v opencpn >/dev/null 2>&1; then
  echo "    $(command -v opencpn)"
  OCPN_VER="$(dpkg-query -W -f='${Version}' opencpn 2>/dev/null || true)"
  [ -n "$OCPN_VER" ] && echo "    package version $OCPN_VER"
  case "$OCPN_VER" in
    5.[0-7].*|[0-4].*) fail "OpenCPN $OCPN_VER is too old: this plugin needs 5.8 or later." ;;
  esac
elif command -v flatpak >/dev/null 2>&1 && flatpak info org.opencpn.OpenCPN >/dev/null 2>&1; then
  fail "Only the Flatpak OpenCPN is installed. A plugin for it has to be built
       inside the Flatpak SDK; see README-linux.txt (Flatpak)."
else
  echo "    opencpn not found on PATH - building anyway (install OpenCPN 5.8+ to use it)"
fi

# ---- 2. Build tools and libraries ---------------------------------------------
step "Checking build tools"
need=()
command -v cmake >/dev/null || need+=(cmake)
command -v g++ >/dev/null || need+=(build-essential)
command -v lsb_release >/dev/null || need+=(lsb-release)
if ! command -v wx-config >/dev/null; then
  # wxWidgets 3.2 GTK3 development files (same library OpenCPN uses)
  if apt-cache show libwxgtk3.2-dev >/dev/null 2>&1; then need+=(libwxgtk3.2-dev)
  else need+=(libwxgtk3.0-gtk3-dev); fi
fi
dpkg -s liblzma-dev >/dev/null 2>&1 || need+=(liblzma-dev)
[ -f /usr/include/GL/gl.h ] || need+=(libgl-dev)
if [ ${#need[@]} -gt 0 ]; then
  echo "    installing: ${need[*]}"
  sudo apt-get update -q
  sudo apt-get install -y -q "${need[@]}"
fi
echo "    wxWidgets $(wx-config --version) ($(wx-config --selected-config))"

# ---- 3. Build -----------------------------------------------------------------
step "Building"
rm -rf "$WORK/build"
mkdir -p "$WORK/build" "$DIST"
cmake -S "$SRC" -B "$WORK/build" -DCMAKE_BUILD_TYPE=Release -DOCPN_API_DIR="$SRC/api"
cmake --build "$WORK/build" -j"$(nproc)"
SO="$WORK/build/libcm93depth_pi.so"
[ -f "$SO" ] || fail "build finished but libcm93depth_pi.so was not found"
strip --strip-unneeded "$SO" || true
echo "    $SO"

# ---- 4. Plugin tarball for OpenCPN's "Import plugin..." -----------------------
# Layout OpenCPN's plugin installer expects on Linux
# (model/src/plugin_handler.cpp, linux_entry_set_install_path):
#   <top>/metadata.xml
#   <top>/lib/opencpn/libcm93depth_pi.so  -> ~/.local/lib/opencpn/
# The metadata target is "all": OpenCPN's import check accepts it on any
# platform, and the build itself is matched to this PC (its distribution,
# architecture and wxWidgets), as the file name and description say.
step "Packaging the plugin for OpenCPN"
VER="$(sed -n 's/^#define PLUGIN_VERSION_\(MAJOR\|MINOR\|PATCH\) \([0-9]*\).*/\2/p' \
        "$SRC/src/cm93depth_pi.h" | paste -sd.)"
DISTRO="$(lsb_release -is | tr 'A-Z ' 'a-z_')"
REL="$(lsb_release -rs)"
case "$(uname -m)" in
  x86_64) ARCH=x86_64 ;;
  aarch64|arm64) ARCH=arm64 ;;
  armv7l|armv6l|armhf) ARCH=armhf ;;
  *) ARCH="$(uname -m)" ;;
esac
WXV="$(wx-config --version)"
TOP="cm93depth_pi-$VER-0_$DISTRO-$REL-$ARCH"
STAGE="$WORK/stage"
rm -rf "$STAGE"
mkdir -p "$STAGE/$TOP/lib/opencpn"
cp "$SO" "$STAGE/$TOP/lib/opencpn/"
cat > "$STAGE/$TOP/metadata.xml" <<EOF
<plugin version="1">
  <name>CM93 Least Depth</name>
  <version>$VER.0</version>
  <release>0</release>
  <summary>Least charted depth and look-ahead from CM93 charts, output as NMEA</summary>
  <api-version>1.18</api-version>
  <open-source>yes</open-source>
  <author>cm93depth_pi</author>
  <source>local build</source>
  <info-url></info-url>
  <description>Reads the CM93 vector charts at the vessel and ahead of it. Sends the least charted depth (\$PCMLD) and the first shoal ahead (\$PCMLA) as NMEA 0183. Built for $DISTRO $REL $ARCH, wxWidgets $WXV.</description>
  <target>all</target>
  <target-version>$REL</target-version>
  <target-arch>$ARCH</target-arch>
  <tarball-url></tarball-url>
  <tarball-checksum></tarball-checksum>
</plugin>
EOF
TARBALL="$DIST/$TOP.tar.gz"
rm -f "$TARBALL"
# files only, no directory entries
tar -czf "$TARBALL" -C "$STAGE" --no-recursion \
    "$TOP/metadata.xml" "$TOP/lib/opencpn/libcm93depth_pi.so"
echo "    $TARBALL"

if [ "$INSTALL" -eq 0 ]; then
  cat <<EOF

Built and packaged:
    $TARBALL
To install: start OpenCPN, Options > Plugins > Import plugin..., choose this
file, then enable 'CM93 Least Depth' in the plugin list.
The same file installs on any PC with $DISTRO $REL on $ARCH.
EOF
  exit 0
fi

# ---- 5. Direct install (only with --install) -----------------------------------
step "Installing for $USER"
if pgrep -x opencpn >/dev/null 2>&1; then
  echo "    OpenCPN is running. Close it, then press Enter."
  read -r _
fi
mkdir -p "$HOME/.local/lib/opencpn"
cp "$SO" "$HOME/.local/lib/opencpn/"
echo "    $HOME/.local/lib/opencpn/libcm93depth_pi.so"
CONF="$HOME/.opencpn/opencpn.conf"
if [ -f "$CONF" ]; then
  cp "$CONF" "$CONF.bak-cm93depth"
  if grep -q '^\[PlugIns/libcm93depth_pi.so\]' "$CONF"; then
    # set bEnabled=1 inside that section
    awk 'BEGIN{s=0} /^\[/{s=($0=="[PlugIns/libcm93depth_pi.so]")}
         s && /^bEnabled=/{$0="bEnabled=1"} {print}' "$CONF.bak-cm93depth" > "$CONF"
    grep -A3 '^\[PlugIns/libcm93depth_pi.so\]' "$CONF" | grep -q '^bEnabled=1' ||
      sed -i '/^\[PlugIns\/libcm93depth_pi.so\]/a bEnabled=1' "$CONF"
  else
    printf '\n[PlugIns/libcm93depth_pi.so]\nbEnabled=1\n' >> "$CONF"
  fi
  echo "    enabled in $CONF (backup: opencpn.conf.bak-cm93depth)"
fi
echo
echo "Done. Start OpenCPN; the CM93 Least Depth button is on the toolbar."
