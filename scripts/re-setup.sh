#!/usr/bin/env bash
# re-setup.sh — Full RE toolkit for Debian
# Run as a normal user with sudo access
# Usage: bash re-setup.sh [--skip] (skip already-installed sections)

set -e
SKIP_MODE=false
if [[ "$1" == "--skip" ]]; then
  SKIP_MODE=true
fi

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
info()    { echo -e "${GREEN}[+]${NC} $1"; }
warn()    { echo -e "${YELLOW}[!]${NC} $1"; }
section() { echo -e "\n${RED}━━━ $1 ━━━${NC}"; }

# ─── APT DEPS ────────────────────────────────────────────────────────────────
section "APT packages"
sudo apt-get update -qq
sudo apt-get install -y \
  build-essential git curl wget unzip python3 python3-pip python3-venv \
  gdb ltrace strace file binutils elfutils \
  nasm yasm \
  libssl-dev libffi-dev libpcap-dev \
  tcpdump tshark \
  bpftrace pahole dwarfdump \
  patchelf checksec \
  qemu-user qemu-system \
  jq xxd bsdmainutils \
  cmake ninja-build pkg-config \
  default-jdk \
  crash kexec-tools linux-perf \
  python3-bpfcc bpfcc-tools \
  wireshark nmap netcat-openbsd socat

# ─── RADARE2 ─────────────────────────────────────────────────────────────────
section "Radare2 (from git)"
if [ "$SKIP_MODE" = true ] && [ -d "$HOME/radare2" ]; then
  warn "skipped (--skip)"
elif [ ! -d "$HOME/radare2" ]; then
  git clone https://github.com/radare/radare2 "$HOME/radare2"
  cd "$HOME/radare2" && ./sys/install.sh && cd -
else
  warn "radare2 already exists, skipping"
fi

# ─── GDB PLUGINS ─────────────────────────────────────────────────────────────
section "GDB — pwndbg"
if [ "$SKIP_MODE" = true ] && [ -d "$HOME/pwndbg" ]; then
  warn "skipped (--skip)"
elif [ ! -d "$HOME/pwndbg" ]; then
  git clone https://github.com/pwndbg/pwndbg "$HOME/pwndbg"
  cd "$HOME/pwndbg" && ./setup.sh && cd -
else
  warn "pwndbg already exists, skipping"
fi

# ─── PYTHON RE TOOLS ─────────────────────────────────────────────────────────
section "Python tools (pwntools, ROPgadget, angr, frida)"
pip3 install --user --break-system-packages \
  pwntools \
  ROPgadget \
  ropper \
  angr \
  frida-tools \
  capstone \
  keystone-engine \
  unicorn \
  pyelftools \
  binwalk

# ─── FRIDA (native) ──────────────────────────────────────────────────────────
section "Frida CLI"
FRIDA_VERSION=$(pip3 show frida-tools 2>/dev/null | grep Version | awk '{print $2}')
info "frida-tools $FRIDA_VERSION installed via pip above"

# ─── GHIDRA ──────────────────────────────────────────────────────────────────
section "Ghidra"
GHIDRA_DIR="$HOME/tools/ghidra"
if [ "$SKIP_MODE" = true ] && [ -d "$GHIDRA_DIR" ]; then
  warn "skipped (--skip)"
elif [ ! -d "$GHIDRA_DIR" ]; then
  mkdir -p "$HOME/tools"
  GHIDRA_URL=$(curl -s https://api.github.com/repos/NationalSecurityAgency/ghidra/releases/latest \
    | jq -r '.assets[] | select(.name | test("ghidra.*_PUBLIC.*\\.zip")) | .browser_download_url' | head -1)
  info "Downloading Ghidra from $GHIDRA_URL"
  wget -q "$GHIDRA_URL" -O /tmp/ghidra.zip
  unzip -q /tmp/ghidra.zip -d "$HOME/tools/"
  mv "$HOME/tools"/ghidra_* "$GHIDRA_DIR"
  rm /tmp/ghidra.zip
  # launcher symlink
  ln -sf "$GHIDRA_DIR/ghidraRun" "$HOME/.local/bin/ghidra"
  chmod +x "$GHIDRA_DIR/ghidraRun"
  info "Ghidra installed → run: ghidra"
else
  warn "Ghidra already at $GHIDRA_DIR, skipping"
fi

# ─── GHIDRA CLI ─────────────────────────────────────────────────────────────
section "Ghidra CLI"
if [ "$SKIP_MODE" = true ] && command -v ghidra &> /dev/null && [[ $(ghidra --version 2>/dev/null | head -1) == ghidra\ * ]]; then
  warn "skipped (--skip)"
elif ! command -v ghidra &> /dev/null || [[ $(ghidra --version 2>/dev/null | head -1) != ghidra\ * ]]; then
  if [ ! -d "$HOME/ghidra-cli" ]; then
    git clone https://github.com/akiselev/ghidra-cli "$HOME/ghidra-cli"
  fi
  cd "$HOME/ghidra-cli" && cargo install --path . --quiet && cd -
  ln -sf "$HOME/.cargo/bin/ghidra" "$HOME/.local/bin/ghidra"
  ghidra config set ghidra_install_dir "$GHIDRA_DIR"
  info "ghidra-cli installed"
else
  warn "ghidra-cli already installed, skipping"
fi

# ─── CUTTER (Rizin GUI) ──────────────────────────────────────────────────────
section "Cutter (Rizin GUI)"
CUTTER_APPIMAGE="$HOME/tools/Cutter.AppImage"
if [ "$SKIP_MODE" = true ] && [ -f "$CUTTER_APPIMAGE" ]; then
  warn "skipped (--skip)"
elif [ ! -f "$CUTTER_APPIMAGE" ]; then
  CUTTER_URL=$(curl -s https://api.github.com/repos/rizinorg/cutter/releases/latest \
    | jq -r '.assets[] | select(.name | test("Cutter.*Linux.*AppImage")) | .browser_download_url' | head -1)
  wget -q "$CUTTER_URL" -O "$CUTTER_APPIMAGE"
  chmod +x "$CUTTER_APPIMAGE"
  ln -sf "$CUTTER_APPIMAGE" "$HOME/.local/bin/cutter"
  info "Cutter installed"
else
  warn "Cutter already exists, skipping"
fi

# ─── BINARY NINJA FREE ───────────────────────────────────────────────────────
section "Binary Ninja (free)"
warn "Binary Ninja free tier requires manual download from:"
warn "  https://binary.ninja/free/"
warn "  AppImage → chmod +x → move to ~/tools/"

# ─── IDA FREE ──────────────────────────────────────────────────────────────
section "IDA Free"
warn "IDA Free requires manual download (no CLI installer):"
warn "  https://hex-rays.com/ida-free/"

# ─── MISC BINARY UTILS ──────────────────────────────────────────────────────
section "Misc Python tools"
pip3 install --user --break-system-packages \
  lief \
  r2pipe \
  bcc

# imhex — hex editor with pattern language
IMHEX_APPIMAGE="$HOME/tools/ImHex.AppImage"
if [ "$SKIP_MODE" = true ] && [ -f "$IMHEX_APPIMAGE" ]; then
  warn "skipped (--skip)"
elif [ ! -f "$IMHEX_APPIMAGE" ]; then
  IMHEX_URL=$(curl -s https://api.github.com/repos/WerWolv/ImHex/releases/latest \
    | jq -r '.assets[] | select(.name | test(".*AppImage")) | .browser_download_url' | head -1)
  wget -q "$IMHEX_URL" -O "$IMHEX_APPIMAGE" && chmod +x "$IMHEX_APPIMAGE"
  ln -sf "$IMHEX_APPIMAGE" "$HOME/.local/bin/imhex"
  info "ImHex installed"
fi

# ─── PATH ────────────────────────────────────────────────────────────────────
section "PATH setup"
mkdir -p "$HOME/.local/bin"
if ! grep -q '.local/bin' "$HOME/.bashrc"; then
  echo 'export PATH="$HOME/.local/bin:$PATH"' >> "$HOME/.bashrc"
  info "Added ~/.local/bin to PATH in .bashrc"
fi

# ─── DONE ────────────────────────────────────────────────────────────────────
section "Done"
echo ""
echo "  Installed:"
echo "   gdb + pwndbg       — gdb"
echo "   pwntools           — python3 -c 'from pwn import *'"
echo "   ROPgadget / ropper — ropgadget / ropper"
echo "   angr               — python3 -c 'import angr'"
echo "   frida              — frida / frida-trace / frida-ps"
echo "   radare2            — r2"
echo "   Ghidra             — ghidra  (needs JDK 17)"
echo "   Cutter             — cutter"
echo "   ImHex              — imhex"
echo "   bpftrace           — bpftrace"
echo "   pahole / dwarfdump — pahole / dwarfdump"
echo "   ltrace / strace    — ltrace / strace"
echo "   lief / pyelftools  — python3 -c 'import lief'"
echo "   capstone/keystone  — python3 -c 'import capstone'"
echo "   qemu-user          — qemu-{arch}"
echo ""
echo "  Manual installs needed:"
echo "   Binary Ninja free  → https://binary.ninja/free/"
echo "   IDA Free           → https://hex-rays.com/ida-free/"
echo ""
warn "Restart your shell or run: source ~/.bashrc"

