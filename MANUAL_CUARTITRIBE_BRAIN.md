# Manual de Usuario — CuartiTribe Brain (Electribe 2 Custom MIDI Sequencer)

**CuartiTribe Brain** es un sistema operativo/secuenciador MIDI polimétrico y generativo escrito sobre **Freetribe** para el procesador ARM (`TI AM1802`) de la **Korg Electribe 2**, con un emulador nativo para macOS (`SDL2` + `CoreMIDI`) que ejecuta exactamente el mismo código C que el hardware físico.

---

## 1. Filosofía y Capacidades Principales

Combina lo mejor de 4 mundos en la interfaz física de la Electribe 2:
1. **Elektron (Octatrack / Digitakt):**
   - **Parameter Locks (P-Locks) por paso:** Mantené apretado cualquier paso y girá cualquier perilla o encoder para bloquear Nota, Velocidad, Gate, Microtiming, Probabilidad, Condición, Ratchets, Acorde o MIDI CC en ese único paso.
   - **Trig Conditions (`1:2`, `2:2`, `1:3..4:4`, `FILL`, `!FIL`, `1ST`, `PRE`):** Tus secuencias evolucionan durante decenas de compases sin repetirse igual.
2. **Aphex Twin / IDM Engine:**
   - **Sub-step Ratchets (`1x`, `2x`, `3x`, `4x`, `6x`, `8x`):** Ráfagas de repeticiones dentro de un mismo paso con rampa de velocidad (constante, decay o crescendo).
   - **Escalas de Reloj por pista (`/16` a `4x`):** Desde `4x` (semifusas `1/64` para drill & bass) hasta `/16` (1 paso = 1 compás entero).
   - **Generador `APHEX GEN` de 1 botón:** Crea patrones sincopados con ratchets, microtiming y saltos de octava.
3. **Ambient / Generative Engine (Mutable Instruments Marbles / Turing Machine):**
   - **Hasta 256 pasos (16 compases) independientes por pista.**
   - **Turing Machine (`TURING %`):** En `0%` la secuencia queda fija; entre `1%` y `50%` muta suavemente las notas dentro de la escala activa; entre `51%` y `100%` muta también el ritmo y los ratchets.
   - **Voicings de Acordes por paso:** `1NT` (nota simple), `5TH` (quinta), `MIN`, `MAJ`, `SUS4`, `m7`, `M7`, `OCT`.
   - **Generador `AMBIENT GEN` de 1 botón:** Crea secuencias largas (128+ pasos) con acordes espaciados, gates largos superpuestos y deriva melódica.
4. **Teenage Engineering OP-1 / OP-Z & BeatStep Pro:**
   - **Step Components (`OCT+`, `OCT+-`, `+5TH`, `RMP+`, `RMP-`, `VFAD`, `GLCH`):** Comportamientos lúdicos por paso que transponen o mutan el paso en cada vuelta.
   - **5 Direcciones por pista:** `FWD->` (adelante), `<-REV` (reversa), `PEND<>` (péndulo), `DRNK?~` (caminata ebria: 50% avanza, 25% repite, 25% retrocede) y `RND??` (aleatorio).
   - **Generador Euclídeo en tiempo real:** Distribución matemática de pulsos (`K`) sobre pasos (`N`).

---

## 2. Lectura de la Pantalla OLED (`128x64`)

### Vista Normal de Secuencia
```text
T10 C10 128 STP 01/08   <-- Pista (T01..16), Canal MIDI (C01..16), BPM, Estado (STP/PLY/FIL), Compás actual / Total
[ ][ ][ ][ ].........   <-- Cinta de los 16 compases (256 pasos): relleno = compás actual, punto = fuera del largo
  _  █  ▓  _  □  _ ...  <-- Gráfico de los 16 pasos del compás actual:
                            • Altura de la barra = Nota MIDI
                            • Sólida (█) = Paso normal
                            • Rayada (▓) = Paso con Ratchet (2x..8x)
                            • Hueca  (□) = Paso con Condición (1:2, etc.) o Probabilidad < 100%
L:128 1x FWD-> TRG:00%  <-- Largo en pasos (1..256), Escala de Clock (/16..4x), Dirección, Mutación Turing %
C HIRAJ E:0/16 R:0%     <-- Nota Raíz + Escala musical, Euclídeo (Pulsos/Pasos), Probabilidad de Ratchet %
```

### Vista Inspector de Parameter-Lock (al mantener presionado un Pad)
```text
PLOCK S017 [C#4]        <-- Número de paso global (1..256) y Nota bloqueada
V:110 G:24 uT:+00       <-- Velocidad (1..127), Gate (1..96 ticks), Microtiming (-23..+23)
1:2 P:075% R:4x         <-- Trig Condition (1:2), Probabilidad (75%), Ratchet (4x)
OCT+ m7 CC:090          <-- Step Component (OCT+), Acorde (m7), Valor bloqueado de MIDI CC
```

---

## 3. Cómo leer los LEDs de Compás (`BAR 1..4`) y los 16 Pads

Como la Electribe física tiene **4 LEDs de compás (`BAR 1..4`)** bicolores (Rojo + Azul), **CuartiTribe Brain** los organiza en **4 Bancos de 4 Compases** para cubrir los **16 Compases (256 pasos)**:
* **Azul:** Compases **1 a 4** (pasos 1–64)
* **Rojo:** Compases **5 a 8** (pasos 65–128)
* **Violeta (Rojo + Azul):** Compases **9 a 12** (pasos 129–192)
* **Parpadeo Rojo/Azul:** Compases **13 a 16** (pasos 193–256)

### Colores de los 16 Pads en modo `SEQUENCER`:
* **Apagado:** Paso vacío.
* **Azul:** Paso activo normal.
* **Violeta:** Paso activo que contiene **Parameter Locks** (Ratchet, Condición, Step Component, Acorde o MIDI CC).
* **Rojo brillante:** Cabezal de reproducción (Playhead) pasando por ese paso.
* **Blanco/Violeta máximo:** Paso seleccionado/retenido para editar P-Locks.

---

## 4. Los 8 Modos de los Pads (Fila de botones sobre los pads)

| Botón de Modo | Función de los 16 Pads |
| :--- | :--- |
| **`SEQUENCER`** | Los 16 pads muestran los 16 pasos del compás actual. **Tocar = activar/desactivar paso.** **Mantener apretado = abrir Inspector de P-Lock** (en el emulador: **clic derecho** fija el paso para editar cómodo con el mouse). |
| **`TRIGGER`** | Los 16 pads disparan en vivo las **16 pistas MIDI** y seleccionan la pista activa (`T01..T16`). |
| **`KEYBOARD`** | Los 16 pads se convierten en un teclado cromático/cuantizado a la escala activa para tocar o grabar en vivo (`REC`) en la pista actual. |
| **`GEN / CHORD`** | **Matriz Generativa Lúdica:**<br>• **Pads 1–4:** Ritmos Euclídeos instantáneos (`3/8`, `5/16`, `7/16`, `11/16`)<br>• **Pad 5 (`APHEX!`):** Genera secuencia IDM/Aphex en la pista actual<br>• **Pad 6 (`AMBIENT!`):** Genera secuencia Ambient de 128 pasos con acordes<br>• **Pad 7 (`Tur 20%`):** Activa/desactiva deriva melódica suave (Turing 20%)<br>• **Pad 8 (`Tur 70%`):** Activa/desactiva caos generativo (Turing 70%)<br>• **Pads 9–13:** Dirección (`FWD->`, `<-REV`, `PEND<>`, `DRNK?~`, `RND??`)<br>• **Pads 14–15:** Bajar / Subir velocidad de reloj (`CLK -` / `CLK +`)<br>• **Pad 16:** Limpiar pista (`CLEAR`) |
| **`STEP JUMP`** | Saltar instantáneamente al paso `1..16` (o con `SHIFT` mantenido, saltar al compás `1..16`). |
| **`PART MUTE`** | Mutea / Desmutea cualquiera de las 16 pistas (` Azul = Activa `, ` Rojo = Muteada `). |
| **`ERASE`** | Tocar un pad `1..16` borra y resetea esa pista completa. |
| **`BARS 1-16` (`Pattern Set`)** | Los 16 pads eligen directamente qué **Compás (`1..16`)** estás viendo/editando. **Con `SHIFT` mantenido:** tocar el pad `N` define el largo de la pista en `N × 16` pasos (`16` a `256` pasos). |

---

## 5. Mapa de Encoders y Knobs (Modo Normal vs. Modo P-Lock)

Cada encoder y perilla tiene doble función: **sin pisar ningún pad** ajusta la pista/secuenciador en vivo; **manteniendo apretado un pad** (o con clic derecho en el emulador) clava un **Parameter Lock (P-Lock)** en ese paso.

### Los 5 Encoders Superiores
*(En el emulador: arrastrar arriba/abajo con el mouse o usar la **rueda del mouse** encima del encoder)*

| Encoder | Función Normal (Pista / Global) | Con `SHIFT` activo | **Modo P-Lock (Manteniendo un Pad)** |
| :--- | :--- | :--- | :--- |
| **`MAIN ENC`** | **BPM** (`30.0` a `300.0`) | Largo de pista de a 4 pasos | **Nota MIDI del paso** semitono por semitono |
| **`OSC ENC`** | **Pulsos Euclídeos (`K`)** en vivo | **Canal MIDI** de la pista (`1..16`) | **Ratchet del paso** (`1x, 2x, 3x, 4x, 6x, 8x`) |
| **`CLK ENC`** | **Escala de Reloj** (`/16, /8, /4, /2, 1x, 2x, 3x, 4x`) | **Escala Musical** (`Hirajoshi`, `Minor`, `Dorian`, etc.) | **Condición Elektron** (`ALW, 1:2..4:4, FILL, !FIL, 1ST, PRE`) |
| **`DIR ENC`** | **Dirección de pista** (`FWD, REV, PEND, DRNK, RND`) | — | **Step Component OP-Z** (`OCT+, OCT+-, +5TH, RMP+, RMP-, VFAD, GLCH`) |
| **`LEN ENC`** | **Largo exacto de pista** (`1..256` pasos, para polimetrías como `13`, `21`, `64`, `128`, `256`) | — | **Tipo de Acorde del paso** (`1NT, 5TH, MIN, MAJ, SUS4, m7, M7, OCT`) |

### Las 8 Perillas (Knobs)
*(En el emulador: arrastrar arriba/abajo o usar la **rueda del mouse**)*

| Perilla (Knob) | Función Normal (Toda la Pista) | **Modo P-Lock (Manteniendo un Pad)** |
| :--- | :--- | :--- |
| **`PITCH`** | Nota base de la pista | **Nota del paso** (cuantizada a la escala) |
| **`LEVEL`** | Velocidad general de la pista | **Velocidad del paso** (`1..127`) |
| **`DECAY`** | Duración de Gate de toda la pista | **Gate del paso** (`1..96` ticks; valores altos = pads largos) |
| **`ATTACK`** | — | **Microtiming del paso** (`-23..+23` ticks) |
| **`TURING` (`Mod Depth`)** | **Mutación Turing Machine (`0..100%`)** | **Probabilidad del paso (`1..100%`)** |
| **`RATCHET` (`Mod Speed`)** | **Probabilidad de Ratchet espontáneo (`0..100%`)** | **Cantidad de Ratchet del paso (`1x..8x`)** |
| **`CC 74` (`Resonance`)** | Envía `MIDI CC 74` (Cutoff) en vivo | **Bloquea `MIDI CC 74`** en ese paso |
| **`CC 71` (`EG Int`)** | Envía `MIDI CC 71` en vivo | **Bloquea `MIDI CC`** en ese paso |

---

## 6. Atajos Rápidos de Teclado en el Emulador de macOS

* **`Espacio`:** Play / Stop
* **`1` a `8` y `Q` a `I`:** Tocar / mantener los **Pads 1 a 16**
* **`Clic Derecho` sobre un Pad:** **Fija (Latch) el Pad en modo P-Lock** para que puedas mover los knobs y encoders tranquilamente con el mouse; volvé a hacer clic en cualquier pad para soltarlo.
* **`Flechas Izquierda / Derecha`:** Cambiar de Pista (`T01..T16`)
* **`[` y `]`:** Cambiar de Compás (`BAR 1..16`, hasta 256 pasos)
* **`G`:** Generar secuencia **Aphex Twin IDM** en la pista actual
* **`H`:** Generar secuencia **Ambient** en la pista actual
* **`F`:** Activar modo **FILL** (dispara todos los pasos que tengan condición `FILL`)
* **`R`:** Activar/desactivar grabación en vivo (`REC`)
* **`S`:** Guardar el proyecto completo en Flash (`WRITE`); **`Shift + S`** carga el proyecto guardado.
* **Botón superior izquierdo (`Virtual: CuartiTribe Brain Out`):** Hace clic ahí para alternar entre el puerto virtual CoreMIDI y cualquier interfaz MIDI física conectada a tu Mac.

---

## 7. Cómo compilar y cargar en la Electribe 2 física

1. **Correr el emulador en Mac:**
   ```bash
   cd simulator && make run
   ```
2. **Compilar el firmware ARM (`cpu.bin`) para la Electribe 2:**
   ```bash
   export PATH="/opt/homebrew/bin:$PATH"
   make -C freetribe/cpu APP=brain
   ```
   Esto genera `freetribe/cpu/build/cpu.bin`.
3. **Subirlo en RAM a la Electribe 2 (con Hacktribe instalado, 100% reversible al apagar):**
   ```bash
   python3 scripts/execute_freetribe.py freetribe/cpu/build/cpu.bin
   ```
