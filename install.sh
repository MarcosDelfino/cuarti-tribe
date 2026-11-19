#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VENV_DIR="$REPO_ROOT/.venv"
CPU_DIR="$REPO_ROOT/freetribe/cpu"

echo "========================================"
echo "  CUARTI CORP - CuartiChord Installer"
echo "========================================"
echo ""

# 1. Check ARM GCC toolchain
if ! command -v arm-none-eabi-gcc >/dev/null 2>&1; then
    echo "[!] arm-none-eabi-gcc not found."
    if [[ "$(uname -s)" == "Darwin" ]] && command -v brew >/dev/null 2>&1; then
        echo "[*] Installing arm-none-eabi-gcc via Homebrew..."
        brew install arm-none-eabi-gcc
    else
        echo "[ERROR] Please install arm-none-eabi-gcc first (e.g. 'sudo apt install gcc-arm-none-eabi' or Homebrew) and re-run ./install.sh."
        exit 1
    fi
fi

# 2. Check Python 3 & create local venv
if ! command -v python3 >/dev/null 2>&1; then
    echo "[ERROR] python3 is required. Please install Python 3 and re-run ./install.sh."
    exit 1
fi

if [[ ! -d "$VENV_DIR" ]]; then
    echo "[*] Creating Python virtual environment in .venv..."
    python3 -m venv "$VENV_DIR"
fi

echo "[*] Installing Python dependencies (mido, python-rtmidi, bsdiff4)..."
"$VENV_DIR/bin/pip" install --upgrade pip >/dev/null
"$VENV_DIR/bin/pip" install mido python-rtmidi bsdiff4

# 3. Compile CuartiChord (Freetribe omnichord app)
echo "[*] Compiling CuartiChord firmware..."
make -C "$CPU_DIR" APP=omnichord
cp "$CPU_DIR/build/cpu.bin" "$CPU_DIR/build/omnichord.bin"

# 4. Install global 'cuartichord' launcher command
INSTALL_DIR=""
if [[ -d "/opt/homebrew/bin" && -w "/opt/homebrew/bin" ]]; then
    INSTALL_DIR="/opt/homebrew/bin"
elif [[ -d "/usr/local/bin" && -w "/usr/local/bin" ]]; then
    INSTALL_DIR="/usr/local/bin"
else
    INSTALL_DIR="$HOME/.local/bin"
    mkdir -p "$INSTALL_DIR"
fi

LAUNCHER_BIN="$INSTALL_DIR/cuartichord"
cat > "$LAUNCHER_BIN" <<EOF
#!/usr/bin/env bash
exec "$VENV_DIR/bin/python" "$REPO_ROOT/scripts/cuartichord_launcher.py" "\$@"
EOF
chmod +x "$LAUNCHER_BIN"

echo ""
echo "[OK] CuartiChord built and installed!"
echo "     Command : $LAUNCHER_BIN"
echo "     Binary  : $CPU_DIR/build/omnichord.bin"
echo ""
if [[ ":$PATH:" != *":$INSTALL_DIR:"* ]]; then
    echo "[NOTE] Add $INSTALL_DIR to your PATH if 'cuartichord' is not found:"
    echo "       export PATH=\"$INSTALL_DIR:\$PATH\""
    echo ""
fi
echo "Next step: Connect your Electribe 2 (running Hacktribe 2) via MIDI IN/OUT"
echo "and run 'cuartichord' to verify MIDI and bootload it into RAM."
