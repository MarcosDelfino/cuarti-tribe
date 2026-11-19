#!/usr/bin/env python3
"""Interactive MIDI diagnostic and CuartiChord bootloader launcher."""

import sys
import time
from pathlib import Path
import mido

REPO_ROOT = Path(__file__).resolve().parent.parent
BIN_PATH = REPO_ROOT / "freetribe" / "cpu" / "build" / "omnichord.bin"

sys.path.insert(0, str(REPO_ROOT / "scripts"))
from e2_syx_codec import syx_enc  # noqa: E402


def select_ports():
    inputs = mido.get_input_names()
    outputs = mido.get_output_names()

    if not inputs or not outputs:
        print("[ERROR] No se encontraron puertos MIDI IN/OUT en macOS.")
        print("  IN :", inputs)
        print("  OUT:", outputs)
        return None, None

    if "Scarlett 18i20 USB" in inputs and "Scarlett 18i20 USB" in outputs:
        return "Scarlett 18i20 USB", "Scarlett 18i20 USB"

    if len(inputs) == 1 and len(outputs) == 1:
        return inputs[0], outputs[0]

    print("\nPuertos MIDI disponibles:")
    for i, name in enumerate(inputs, 1):
        print(f"  IN  [{i}] {name}")
    in_idx = int(input("Elegi MIDI IN (numero): ").strip()) - 1

    for i, name in enumerate(outputs, 1):
        print(f"  OUT [{i}] {name}")
    out_idx = int(input("Elegi MIDI OUT (numero): ").strip()) - 1

    return inputs[in_idx], outputs[out_idx]


def check_midi(in_port_name, out_port_name):
    print(f"\n=== Chequeo MIDI ({in_port_name}) ===")
    try:
        inp = mido.open_input(in_port_name)
        out = mido.open_output(out_port_name)
    except Exception as exc:
        print(f"[ERROR] No pude abrir el puerto MIDI: {exc}")
        return False, None

    for _ in inp.iter_pending():
        pass

    # 1. Listen briefly for incoming clock/notes
    print("1) Escuchando trafico MIDI IN (1.5s)...", end=" ", flush=True)
    t0 = time.time()
    rx_msgs = []
    while time.time() - t0 < 1.5:
        rx_msgs.extend(inp.iter_pending())
        time.sleep(0.01)

    clocks = sum(1 for m in rx_msgs if m.type == "clock")
    others = [m.type for m in rx_msgs if m.type != "clock"]
    if rx_msgs:
        print(f"OK ({len(rx_msgs)} msgs: {clocks} clocks, otros={others[:5]})")
    else:
        print("Sin trafico espontaneo (normal si Clock/Play esta apagado)")

    # 2. Send Korg Search Device SysEx
    print("2) Enviando Search Device (F0 42 50 00 00 F7)...", end=" ", flush=True)
    for _ in inp.iter_pending():
        pass
    out.send(mido.Message("sysex", data=[0x42, 0x50, 0x00, 0x00]))

    t0 = time.time()
    reply = None
    while time.time() - t0 < 2.5:
        for m in inp.iter_pending():
            if m.type == "sysex":
                reply = m.bytes()
                break
        if reply:
            break
        time.sleep(0.01)

    inp.close()
    out.close()

    if reply and len(reply) >= 12 and reply[:4] == [0xF0, 0x42, 0x50, 0x01]:
        g_ch = reply[4]
        dev_id = reply[6]
        ver = f"{reply[10]}.{reply[11]:02d}"
        print(f"OK! Electribe detectada (ID: 0x{dev_id:02X}, Ch: {g_ch}, FW: {ver})")
        return True, (g_ch, dev_id)

    print("SIN RESPUESTA.")
    print("   -> Revisa que la Electribe este en Hacktribe 2 (no ya en CuartiChord)")
    print("   -> Revisa cables TRS Tipo A: E2 OUT->Scarlett IN, E2 IN->Scarlett OUT")
    return False, None


def monitor_midi(in_port_name, seconds=5.0):
    print(f"\nEscuchando MIDI IN por {seconds:.0f}s (toca pads o dale Play)...")
    inp = mido.open_input(in_port_name)
    t0 = time.time()
    count = 0
    while time.time() - t0 < seconds:
        for m in inp.iter_pending():
            if m.type != "clock":
                print("  RX:", m)
            count += 1
        time.sleep(0.01)
    inp.close()
    print(f"Total mensajes recibidos: {count}")


def wait_sysex(inp, timeout=3.0):
    t0 = time.time()
    while time.time() - t0 < timeout:
        for m in inp.iter_pending():
            if m.type == "sysex":
                return m.bytes()
        time.sleep(0.002)
    return None


def boot_cuartichord(in_port_name, out_port_name):
    if not BIN_PATH.is_file():
        print(f"[ERROR] No encuentro el binario en: {BIN_PATH}")
        return False

    code = BIN_PATH.read_bytes()
    chunks = [code[i : i + 0x100].ljust(256, b"\x00")[:256] for i in range(0, len(code), 0x100)]

    inp = mido.open_input(in_port_name)
    out = mido.open_output(out_port_name)
    for _ in inp.iter_pending():
        pass

    sysex_head = [0x42, 0x30, 0x00, 0x01, 0x24]

    print("\n1) Pivotando a Freetribe loader (0x58)...")
    out.send(mido.Message("sysex", data=sysex_head + [0x58] + syx_enc(bytes(8))))
    time.sleep(1.0)

    print("2) Verificando magic word (0x64)...", end=" ", flush=True)
    for _ in inp.iter_pending():
        pass
    out.send(mido.Message("sysex", data=sysex_head + list(b"\x64\x01\x23\x45\x67")))
    r = wait_sysex(inp, timeout=3.0)
    if not r or r[5:9] != [0x76, 0x54, 0x32, 0x10]:
        print(f"FALLO (resp={r})")
        inp.close()
        out.close()
        return False
    print("OK!")

    total = len(chunks)
    print(f"3) Subiendo {BIN_PATH.name} ({len(code)} bytes, {total} bloques):")
    for i, chunk in enumerate(chunks, 1):
        out.send(mido.Message("sysex", data=sysex_head + [0x54] + syx_enc(chunk)))
        r = wait_sysex(inp, timeout=3.0)
        if not r or r[5] != 0x21:
            print(f"\n[ERROR] Fallo en bloque {i}/{total}: {r}")
            inp.close()
            out.close()
            return False
        bar_len = 30
        filled = int(bar_len * i / total)
        bar = "#" * filled + "-" * (bar_len - filled)
        print(f"\r   [{bar}] {i}/{total}", end="", flush=True)
    print()

    print("4) Ejecutando en 0x80000000...")
    addr = (0x80000000).to_bytes(4, "little") + bytes(4)
    out.send(mido.Message("sysex", data=sysex_head + [0x57] + syx_enc(addr)))
    r = wait_sysex(inp, timeout=3.0)

    inp.close()
    out.close()

    if r:
        msg_txt = bytes(r[1:-1]).decode("ascii", errors="replace").strip()
        print(f"\n{msg_txt}")
    print("\n[LISTO] CuartiChord corriendo! Ya podes desconectar los cables MIDI.")
    return True


def main():
    print(r"""
  ____ _   _   _    ____ _____ ___    ____ ___  ____  ____
 / ___| | | | / \  |  _ \_   _|_ _|  / ___/ _ \|  _ \|  _ \
| |   | | | |/ _ \ | |_) || |  | |  | |  | | | | |_) | |_) |
| |___| |_| / ___ \|  _ < | |  | |  | |__| |_| |  _ <|  __/
 \____|\___/_/   \_\_| \_\|_| |___|  \____\___/|_| \_\_|
==============================================================
        CUARTICHORD - Launcher & MIDI Check  [v1.0]
==============================================================
""")

    in_port, out_port = select_ports()
    if not in_port:
        sys.exit(1)

    ok, _ = check_midi(in_port, out_port)

    while True:
        print("\nOpciones:")
        if ok:
            print("  [Enter] o [1] Bootear CuartiChord")
        else:
            print("  [1] Intentar bootear CuartiChord igual")
        print("  [2] Re-chequear conexion MIDI")
        print("  [3] Monitor MIDI en vivo (5s)")
        print("  [q] Salir")

        choice = input("\n> ").strip().lower()
        if choice == "" and ok:
            choice = "1"

        if choice == "1":
            if boot_cuartichord(in_port, out_port):
                break
        elif choice == "2":
            ok, _ = check_midi(in_port, out_port)
        elif choice == "3":
            monitor_midi(in_port, 5.0)
        elif choice == "q":
            break


if __name__ == "__main__":
    main()
