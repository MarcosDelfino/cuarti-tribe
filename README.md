# CuartiChord

**CuartiChord** turns a Korg Electribe 2 (Synth or Sampler) into a standalone MIDI **Omnichord-style chord & strumplate controller**.

CuartiChord is a fork of [bangcorrupt's Hacktribe](https://github.com/bangcorrupt/hacktribe) and runs on top of [Freetribe](https://github.com/bangcorrupt/freetribe), booting custom bare-metal ARM9 firmware directly into the Electribe 2's RAM over MIDI SysEx.

> **Credit & Origins:** All low-level reverse engineering, Hacktribe firmware patches, and the Freetribe ARM9/Blackfin runtime were created by [bangcorrupt](https://github.com/bangcorrupt). CuartiChord builds on that foundation with a custom Freetribe app (`freetribe/cpu/src/apps/omnichord/omnichord.c`) and a macOS/Python MIDI launcher (`scripts/cuartichord_launcher.py`).

---

## How to Install

### Prerequisites
- **Hardware:** Korg Electribe 2 (Blue Synth or Red/Black Sampler) + a MIDI interface (DIN MIDI IN/OUT connected to your computer).
- **Toolchain (to build from source):** `arm-none-eabi-gcc` and `make`.
- **Python 3** with `mido`, `python-rtmidi`, and `bsdiff4`:
  ```bash
  pip install mido python-rtmidi bsdiff4
  ```

### 1. Install Hacktribe 2 on the Electribe 2
CuartiChord uses the Freetribe RAM bootloader (`0x58` / `0x57` SysEx), which is included in **Hacktribe 2**:
1. Place your stock Electribe 2 `SYSTEM.VSB` (v2.02) in the repo root.
2. Apply the Hacktribe 2 patch (`patch/hacktribe-2.patch`) using `scripts/e2-firmware-patch.py` (see `scripts/hacktribe-tutorial.sh` for the exact workflow) to generate the patched `SYSTEM.VSB`.
3. Copy the patched `SYSTEM.VSB` to your Electribe's SD card under `KORG/electribe2/System/` and run **System Update** on the Electribe.

### 2. Build CuartiChord
```bash
cd freetribe/cpu
make APP=omnichord
cp build/cpu.bin build/omnichord.bin
```

### 3. Boot CuartiChord via MIDI
With the Electribe powered on (running Hacktribe 2) and MIDI IN + OUT connected:
```bash
python scripts/cuartichord_launcher.py
```
*(Or using the raw Freetribe loader directly:)*
```bash
python scripts/execute_freetribe.py freetribe/cpu/build/omnichord.bin
```

> **Note:** CuartiChord runs entirely in RAM (`OC_RAM`). Power-cycling the Electribe (or pressing `Shift + Exit`) cleanly returns it to Hacktribe 2.

---

## User Manual

### Startup Defaults
When CuartiChord boots, it starts ready to play with:
- **Scale Mode:** `AUTO:C MAJOR`
- **Base Octave:** `O:4` (MIDI `60` base)
- **Voicing:** `V:3` (3-voice triad)
- **Chord MIDI Channel:** `C01` (Channel 1)
- **Ribbon MIDI Channel:** `R01` (Channel 1)
- **Pad Velocity:** `127` (fixed)
- **Ribbon Velocity:** `85`
- **Ribbon Mode:** `STR` (`CLASSIC`)

---

### Trigger Pads (1–16)

#### Pads 1–12: Root / Scale Degree Selectors
- **In `AUTO` mode:** Pads 1–12 follow the active scale diatonically across octaves, automatically selecting the diatonic chord quality for each degree (e.g., in C Major: `C maj`, `D min`, `E min`, `F maj`, `G maj`, `A min`, `B m7b5`, `C maj`, `D min`, ...).
- **In `FREE` mode:** Pads 1–12 select chromatic roots (`C`..`B`) or Circle of 5ths (`5THS`) depending on layout.

#### Pads 13–16: Chord Quality Selectors
Pressing pads 13–16 selects the chord quality (and temporarily overrides `AUTO` quality while held or until the next root press):
- **Pad 13:** `MAJ`
- **Pad 14:** `MIN`
- **Pad 15:** `m7b5` (Half-diminished)
- **Pad 16:** `DIM7` (Stacked minor 3rds)
- **2-Pad Combos:**
  - `13 + 14` → `DOM7`
  - `13 + 15` → `MAJ7`
  - `14 + 15` → `MIN7`
  - `13 + 16` → `AUG`
  - `14 + 16` → `SUS4`

---

### XY Touch Pad (Ribbon)

Slide or tap across the X axis to trigger notes. Cycle between the **3 Ribbon Modes** using **`Trigger`**, **`Gate Arp`**, or **`Menu`**:

| Mode | OLED Tag | Behavior |
| :--- | :--- | :--- |
| **Classic** | `STR` | 12-string Omnichord strumplate spanning multiple octaves of the active chord. |
| **Scale** | `SCL` | 8 zones playing the 7 notes of the active scale (starting 1 octave above Pad 1) + the octave root (`do re mi fa sol la si do2`). |
| **Chord** | `CHD` | 2–4 wide zones mapped directly to the exact notes of the active chord and voicing (`V:2`, `V:3`, `V:4`, `V:QRT`). |

---

### Encoders, Knobs & Buttons

#### Encoders
| Encoder | Normal Turn | `Shift` + Turn |
| :--- | :--- | :--- |
| **Main** | BPM (`30–300`) | Pad Velocity (`1–127`, default `127`) |
| **Oscillator** | Chord MIDI Channel (`1–16`) | Ribbon MIDI Channel (`1–16`) |
| **Cutoff** | Voicing (`2`, `3`, `4`, `QRT` quartal) | Base Octave (`1–5`, default `4`) |
| **Mod** | Scale Mode (`FREE`, `MAJOR`, `MINOR`, `H.MIN`, `DORIAN`, `MIXO`) | — |
| **IFX** | Scale Key (`C`–`B`) | — |

#### Knobs
- **`Edit` / `Pitch`:** Ribbon Master Velocity (`0–127`, default `85`).
- **`Level`:** Pad Chord Velocity (`1–127`, default `127`).
- **`Decay`:** Ribbon string ring / auto Note-Off gate (`30 ms` – `1200 ms`, default `140 ms`).

#### Buttons
- **`MFX Hold` / `Amp EG` / `BPF` / `Chord` / `Touch Scale`:** Toggle **Momentary (`MOM`)** vs **Latch/Hold (`HLD`)**.
- **`HPF` / `Master FX`:** Toggle **Chord Backing ON** vs **Ribbon Only (`MUT`)**.
- **`Keyboard` / `Sequencer`:** Toggle **`AUTO` Scale** mode vs **`FREE`** mode.
- **`LPF`:** In `FREE` mode, toggle **Chromatic** vs **Circle of 5ths**. In `AUTO` mode, cycle scale type.
- **`Trigger` / `Gate Arp` / `Menu`:** Cycle Ribbon Mode (`CLASSIC` → `SCALE` → `CHORD`).
- **`Left` / `Right` (`Back` / `Forward`):** Base Octave (`1–5`).
- **`Shift + Left` / `Right`:** Ribbon Octave Offset (`-2` to `+2` octaves).
- **`Play`:** Start/Stop 24 PPQN MIDI Clock (`0xFA` / `0xFC` / `0xF8`).
- **`Stop`:** Stop MIDI Clock and send All Notes Off.
- **`Shift + Exit`:** Clean shutdown back to Hacktribe 2.

---

## License
Contents of the `patch/` directory are not open source and are not covered by the GPL.  
All other files are licensed under **AGPL-3.0** unless stated otherwise.
