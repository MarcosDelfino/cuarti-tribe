/*----------------------------------------------------------------------
 * Hacktribe / Freetribe - OMNICHORD Prototype App
 *
 * - Pads 1..12 (0..11): 12 Chord Roots (Chromatic C..B or Circle of 5ths)
 * - Pads 13..16 (12..15): 4 Chord Qualities:
 *     Pad 13 (12) = Maj      [0, 4, 7]
 *     Pad 14 (13) = Min      [0, 3, 7]
 *     Pad 15 (14) = Half-Dim [0, 3, 6, 10] (m7b5)
 *     Pad 16 (15) = Dim      [0, 3, 6, 9]  (dim7)
 *   Plus 2-pad combinations:
 *     Maj + Min      = Dom7  [0, 4, 7, 10]
 *     Maj + Half-Dim = Maj7  [0, 4, 7, 11]
 *     Min + Half-Dim = Min7  [0, 3, 7, 10]
 *     Maj + Dim      = Aug   [0, 4, 8]
 *     Min + Dim      = Sus4  [0, 5, 7]
 * - XY Pad:
 *     X-axis = 12-string Strumplate / Ribbon across 3-4 octaves of active chord
 *     Y-axis = Velocity (1..127)
 *     Remembers last chord even after releasing pads in Momentary mode.
 * - Mode Toggles:
 *     AMP_EG (0x20) / BPF (0x16) / MFX_HOLD: Switch Momentary <-> Toggle (Latch)
 *     HPF (0x14): Toggle Chord Pad Backing Sound ON/OFF (Strum-only vs Pad+Strum)
 *     LPF (0x12): Toggle Root Pad Layout (Chromatic C..B vs Circle of Fifths)
 *     PLAY (0x02) / STOP (0x03): Start/Stop 24 PPQN MIDI Clock (0xF8/0xFA/0xFC)
 *----------------------------------------------------------------------*/

#include "freetribe.h"
#include "ugui.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Electribe 2 Panel Button IDs */
#define BTN_PLAY         0x02
#define BTN_STOP         0x03
#define BTN_REC          0x04
#define BTN_TAP          0x05
#define BTN_BACK         0x07
#define BTN_FORWARD      0x08
#define BTN_MENU         0x09
#define BTN_SHIFT        0x0a
#define BTN_WRITE        0x0b
#define BTN_EXIT         0x0d
#define BTN_LPF          0x12
#define BTN_HPF          0x14
#define BTN_BPF          0x16
#define BTN_LEFT         0x1e
#define BTN_RIGHT        0x1f
#define BTN_AMP_EG       0x20
#define BTN_PART_MUTE    0x21
#define BTN_PART_ERASE   0x22
#define BTN_TRIGGER      0x23
#define BTN_SEQUENCER    0x24
#define BTN_KEYBOARD     0x25
#define BTN_CHORD        0x26
#define BTN_STEP_JUMP    0x27
#define BTN_PATTERN_SET  0x28

/* Electribe 2 Encoder IDs */
#define ENC_MAIN         0x00
#define ENC_OSC          0x01
#define ENC_CUTOFF       0x02
#define ENC_MOD          0x03
#define ENC_IFX          0x04

/* Electribe 2 Knob IDs */
#define KNOB_LEVEL       0x00
#define KNOB_EDIT        0x01
#define KNOB_PITCH       0x02
#define KNOB_DECAY       0x08

#define NUM_HARP_STRINGS  12
#define NUM_SCALE_STRINGS 8
#define MAX_HARP_VOICES   8
#define NUM_AUTO_SCALES   5

typedef enum {
    RIBBON_CLASSIC = 0,
    RIBBON_SCALE,
    RIBBON_CHORD,
    RIBBON_MODE_COUNT
} t_omni_ribbon_mode;

typedef enum {
    QUAL_MAJ = 0,
    QUAL_MIN,
    QUAL_HALF_DIM,
    QUAL_DIM,
    QUAL_DOM7,
    QUAL_MAJ7,
    QUAL_MIN7,
    QUAL_AUG,
    QUAL_SUS4,
    QUAL_COUNT
} t_omni_quality;

typedef enum {
    SCALE_MODE_FREE = 0,
    SCALE_MODE_MAJOR,
    SCALE_MODE_MINOR,
    SCALE_MODE_HARM_MINOR,
    SCALE_MODE_DORIAN,
    SCALE_MODE_MIXOLYDIAN,
    SCALE_MODE_COUNT
} t_omni_scale_mode;

typedef enum {
    VOICING_2 = 0,   /* Root + 3rd */
    VOICING_3,       /* Triad: Root + 3rd + 5th */
    VOICING_4,       /* 7th: Root + 3rd + 5th + 7th */
    VOICING_QUARTAL, /* 4-note Quartal stack (diatonic in AUTO, P4 in FREE) */
    VOICING_COUNT
} t_omni_voicing;

typedef struct {
    /* Settings */
    uint8_t midi_channel;       /* 0..15 (MIDI Ch 1..16) for chords */
    uint8_t ribbon_channel;     /* 0..15 (MIDI Ch 1..16) for XY ribbon */
    uint8_t base_octave;        /* 1..5 (default 4 -> MIDI 60 = C4/C3) */
    int8_t ribbon_octave_off;   /* -2..+2 octave offset for XY ribbon */
    uint8_t voicing_mode;       /* t_omni_voicing (default VOICING_3) */
    uint8_t ribbon_mode;        /* t_omni_ribbon_mode: CLASSIC (12) or SCALE (8) */
    bool latch_mode;            /* false = Momentary, true = Toggle/Latch */
    bool chord_backing_on;      /* true = Pads sound chord, false = Ribbon only */
    bool circle_of_fifths;      /* false = Chromatic C..B, true = Circle of 5ths */
    uint16_t strum_gate_ms;     /* Auto Note-Off gate for ribbon strings (default 140ms) */
    uint8_t ribbon_vel;         /* Master velocity for XY ribbon (1..127, controlled by Edit knob) */

    /* Scale / Auto-Harmonize State */
    uint8_t scale_mode;         /* t_omni_scale_mode: 0 = FREE, 1..5 = AUTO scales */
    uint8_t last_auto_scale;    /* Remembers last chosen AUTO scale (1..5) */
    uint8_t scale_key;          /* 0..11 (C..B) root key for AUTO mode */

    /* Pad State */
    uint16_t held_pads_mask;    /* Bitmask of physically held pads 0..15 */
    int8_t active_root_pad;     /* Currently sounding root pad 0..11 (-1 if none) */
    uint8_t last_root;          /* Last selected root note 0..11 (0=C .. 11=B), remembered for Ribbon */
    uint8_t last_deg;           /* Last scale degree 0..6 for Quartal AUTO voicing */
    uint8_t last_octave_off;    /* Extra octave offset (0 or 1) for pads 8..12 in AUTO mode */
    uint8_t sticky_quality;     /* Selected quality t_omni_quality (0..QUAL_COUNT-1) */
    uint8_t current_quality;    /* Effective quality (auto, sticky, or multi-pad combo) */
    bool quality_overridden;    /* True if user manually overrode AUTO scale quality */
    uint8_t chord_vel;          /* Last chord pad velocity 1..127 */

    /* Active Sounding Chord Notes (for clean Note-Off) */
    uint8_t sounding_chord_notes[4];
    uint8_t sounding_chord_count;
    uint8_t sounding_chord_chan;
    uint8_t chord_off_cooldown_ms; /* Time remaining in 25ms cooldown after last Note-Off */
    uint8_t chord_retrig_ms;       /* Delay gap (ms) before firing pending Note-On */
    uint8_t chord_retrig_vel;      /* Pending Note-On velocity */

    /* Harp / Strumplate Table (12 strings across 3-4 octaves) */
    uint8_t harp_notes[NUM_HARP_STRINGS];
    uint8_t chord_ribbon_count; /* Active note count (2..4) for RIBBON_CHORD */

    /* Active Strum Voices (auto Note-Off timer per voice) */
    uint8_t strum_voice_note[MAX_HARP_VOICES];
    uint8_t strum_voice_chan[MAX_HARP_VOICES];
    uint16_t strum_voice_timer[MAX_HARP_VOICES];

    /* XY Pad State & Auto-Calibration */
    uint32_t raw_x;
    uint32_t raw_y;
    uint32_t x_min;
    uint32_t x_max;
    uint32_t y_min;
    uint32_t y_max;
    uint8_t norm_x;             /* 0..127 */
    uint8_t norm_y;             /* 1..127 (Velocity) */
    int8_t current_zone;        /* 0..11 (-1 if released) */
    uint8_t last_strum_note;    /* Last MIDI note plucked on ribbon */
    uint8_t last_strum_vel;     /* Last MIDI vel plucked on ribbon */
    uint16_t xy_timeout_ms;     /* Countdown to detect finger lift off XY pad */

    /* MIDI Clock (24 PPQN for Volca Sample / external gear) */
    bool clock_playing;
    uint16_t bpm_tenths;        /* 1200 = 120.0 BPM */
    uint32_t clock_accum_us;
    uint32_t clock_tick_24ppqn;

    /* UI & Diagnostics */
    bool shift_held;
    uint8_t last_btn_id;
    char status_line[24];
    uint16_t status_timer_ms;
    uint16_t shutdown_timer_ms;
    bool do_shutdown;
    uint32_t last_systick_ms;
    uint16_t ui_accum_ms;
} t_omnichord;

static t_omnichord g_omni;
static UG_GUI g_ugui;
static UG_DEVICE g_ugui_device;

static const t_led_index k_pad_red_leds[16] = {
    LED_PAD_0_RED,  LED_PAD_1_RED,  LED_PAD_2_RED,  LED_PAD_3_RED,
    LED_PAD_4_RED,  LED_PAD_5_RED,  LED_PAD_6_RED,  LED_PAD_7_RED,
    LED_PAD_8_RED,  LED_PAD_9_RED,  LED_PAD_10_RED, LED_PAD_11_RED,
    LED_PAD_12_RED, LED_PAD_13_RED, LED_PAD_14_RED, LED_PAD_15_RED
};

static const t_led_index k_pad_blue_leds[16] = {
    LED_PAD_0_BLUE,  LED_PAD_1_BLUE,  LED_PAD_2_BLUE,  LED_PAD_3_BLUE,
    LED_PAD_4_BLUE,  LED_PAD_5_BLUE,  LED_PAD_6_BLUE,  LED_PAD_7_BLUE,
    LED_PAD_8_BLUE,  LED_PAD_9_BLUE,  LED_PAD_10_BLUE, LED_PAD_11_BLUE,
    LED_PAD_12_BLUE, LED_PAD_13_BLUE, LED_PAD_14_BLUE, LED_PAD_15_BLUE
};

static const t_led_index k_bar_red_leds[4] = {
    LED_BAR_0_RED, LED_BAR_1_RED, LED_BAR_2_RED, LED_BAR_3_RED
};

static const t_led_index k_bar_blue_leds[4] = {
    LED_BAR_0_BLUE, LED_BAR_1_BLUE, LED_BAR_2_BLUE, LED_BAR_3_BLUE
};

static const char *k_note_names[12] = {
    "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"
};

/* Circle of Fifths root order (classic Suzuki Omnichord layout: Eb..Ab) */
static const uint8_t k_fifths_roots[12] = {
    3, 10, 5, 0, 7, 2, 9, 4, 11, 6, 1, 8
};

static const char *k_qual_names[QUAL_COUNT] = {
    "MAJ", "MIN", "m7b5", "DIM7", "DOM7", "MAJ7", "MIN7", "AUG", "SUS4"
};

static const char *k_scale_names[SCALE_MODE_COUNT] = {
    "FREE", "MAJOR", "MINOR", "H.MIN", "DORIAN", "MIXO"
};

static const char *k_voicing_names[VOICING_COUNT] = {
    "2", "3", "4", "QRT"
};

/* 7 diatonic semitone intervals per scale (for scale_mode 1..5) */
static const uint8_t k_scale_intervals[NUM_AUTO_SCALES][7] = {
    {0, 2, 4, 5, 7, 9, 11}, /* MAJOR (Ionian) */
    {0, 2, 3, 5, 7, 8, 10}, /* MINOR (Aeolian) */
    {0, 2, 3, 5, 7, 8, 11}, /* H.MIN (Harmonic Minor) */
    {0, 2, 3, 5, 7, 9, 10}, /* DORIAN */
    {0, 2, 4, 5, 7, 9, 10}  /* MIXO (Mixolydian) */
};

/* Harmonized diatonic chord qualities for each degree 1..7 (dim = stacked m3) */
static const uint8_t k_scale_qualities[NUM_AUTO_SCALES][7] = {
    {QUAL_MAJ, QUAL_MIN, QUAL_MIN, QUAL_MAJ, QUAL_DOM7, QUAL_MIN, QUAL_DIM}, /* MAJOR */
    {QUAL_MIN, QUAL_DIM, QUAL_MAJ, QUAL_MIN, QUAL_MIN,  QUAL_MAJ, QUAL_MAJ}, /* MINOR */
    {QUAL_MIN, QUAL_DIM, QUAL_AUG, QUAL_MIN, QUAL_DOM7, QUAL_MAJ, QUAL_DIM}, /* H.MIN */
    {QUAL_MIN, QUAL_MIN, QUAL_MAJ, QUAL_MAJ, QUAL_MIN,  QUAL_DIM, QUAL_MAJ}, /* DORIAN */
    {QUAL_MAJ, QUAL_MIN, QUAL_DIM, QUAL_MAJ, QUAL_MIN,  QUAL_MIN, QUAL_MAJ}  /* MIXO */
};

/* 4-note base intervals for each quality (voicing 2/3/4 slices first 2/3/4 tones) */
static const uint8_t k_chord_intervals[QUAL_COUNT][4] = {
    {0, 4, 7, 11}, /* QUAL_MAJ  (Maj7 when 4 notes) */
    {0, 3, 7, 10}, /* QUAL_MIN  (Min7 when 4 notes) */
    {0, 3, 6, 9},  /* QUAL_HALF_DIM slot -> replaced by stacked m3 */
    {0, 3, 6, 9},  /* QUAL_DIM  (stacked minor 3rds: 0, 3, 6, 9) */
    {0, 4, 7, 10}, /* QUAL_DOM7 (Dom7: triad is 0,4,7; 7th is +10) */
    {0, 4, 7, 11}, /* QUAL_MAJ7 */
    {0, 3, 7, 10}, /* QUAL_MIN7 */
    {0, 4, 8, 10}, /* QUAL_AUG */
    {0, 5, 7, 10}  /* QUAL_SUS4 */
};

static void _ugui_pixel(UG_S16 x, UG_S16 y, UG_COLOR c) {
    if (x >= 0 && x < 128 && y >= 0 && y < 64) {
        ft_put_pixel((uint16_t)x, (uint16_t)y, c == C_BLACK);
    }
}

static void _set_status(const char *msg) {
    strncpy(g_omni.status_line, msg, sizeof(g_omni.status_line) - 1);
    g_omni.status_line[sizeof(g_omni.status_line) - 1] = '\0';
    g_omni.status_timer_ms = 1400;
}

static uint8_t _pad_to_root(uint8_t pad, uint8_t *oct_off_out) {
    if (oct_off_out) {
        *oct_off_out = 0;
    }
    if (pad >= 12) {
        return 0;
    }
    if (g_omni.scale_mode != SCALE_MODE_FREE) {
        uint8_t s_idx = (uint8_t)(g_omni.scale_mode - 1);
        /* Continuous diatonic degrees across Pads 0..11: 0..6 = I..VII, 7 = I(+1), 8 = II(+1)... */
        uint8_t deg = (uint8_t)(pad % 7);
        if (oct_off_out) {
            *oct_off_out = (uint8_t)(pad / 7);
        }
        return (uint8_t)((g_omni.scale_key + k_scale_intervals[s_idx][deg]) % 12);
    }
    if (g_omni.circle_of_fifths) {
        return k_fifths_roots[pad];
    }
    return pad;
}

static uint8_t _pad_auto_quality(uint8_t pad) {
    if (g_omni.scale_mode == SCALE_MODE_FREE || pad >= 12) {
        return g_omni.sticky_quality;
    }
    uint8_t s_idx = (uint8_t)(g_omni.scale_mode - 1);
    uint8_t deg = (uint8_t)(pad % 7);
    return k_scale_qualities[s_idx][deg];
}

/* Build 2, 3, or 4 chord semitone offsets according to voicing_mode */
static uint8_t _build_chord_tones(uint8_t *tones_out) {
    uint8_t q = g_omni.current_quality;
    if (q >= QUAL_COUNT) {
        q = QUAL_MAJ;
    }

    if (g_omni.voicing_mode == VOICING_QUARTAL) {
        if (g_omni.scale_mode != SCALE_MODE_FREE && !g_omni.quality_overridden) {
            /* Diatonic 4-note quartal stack: deg, deg+3, deg+6, deg+9 */
            uint8_t s_idx = (uint8_t)(g_omni.scale_mode - 1);
            uint8_t d0 = g_omni.last_deg % 7;
            uint8_t prev = 0;
            for (uint8_t i = 0; i < 4; i++) {
                uint8_t di = (uint8_t)((d0 + i * 3) % 7);
                int16_t raw = (int16_t)k_scale_intervals[s_idx][di] - (int16_t)k_scale_intervals[s_idx][d0];
                if (raw < 0) raw += 12;
                while (i > 0 && raw <= prev) {
                    raw += 12;
                }
                tones_out[i] = (uint8_t)raw;
                prev = (uint8_t)raw;
            }
            return 4;
        } else {
            /* Quartal voicing in FREE mode */
            tones_out[0] = 0;
            tones_out[1] = 5;
            tones_out[2] = 10;
            tones_out[3] = (q == QUAL_MIN || q == QUAL_MIN7) ? 14 : 15;
            return 4;
        }
    }

    uint8_t n = 3;
    if (g_omni.voicing_mode == VOICING_2) n = 2;
    else if (g_omni.voicing_mode == VOICING_4) n = 4;

    /* In AUTO mode without manual override, stack diatonic 3rds strictly from the active scale */
    if (g_omni.scale_mode != SCALE_MODE_FREE && !g_omni.quality_overridden) {
        uint8_t s_idx = (uint8_t)(g_omni.scale_mode - 1);
        uint8_t d0 = g_omni.last_deg % 7;
        uint8_t prev = 0;
        for (uint8_t i = 0; i < n; i++) {
            uint8_t di = (uint8_t)((d0 + i * 2) % 7);
            int16_t raw = (int16_t)k_scale_intervals[s_idx][di] - (int16_t)k_scale_intervals[s_idx][d0];
            if (raw < 0) raw += 12;
            while (i > 0 && raw <= prev) {
                raw += 12;
            }
            tones_out[i] = (uint8_t)raw;
            prev = (uint8_t)raw;
        }
        return n;
    }

    for (uint8_t i = 0; i < n; i++) {
        tones_out[i] = k_chord_intervals[q][i];
    }
    return n;
}

/* Rebuild the XY strumplate table (12 chord strings in CLASSIC, 8 scale notes +1 oct in SCALE, or chord tones in CHORD) */
static void _rebuild_harp_table(void) {
    if (g_omni.ribbon_mode == RIBBON_SCALE) {
        uint8_t s_idx = (g_omni.scale_mode != SCALE_MODE_FREE) ? (uint8_t)(g_omni.scale_mode - 1) : 0;
        uint8_t key = (g_omni.scale_mode != SCALE_MODE_FREE) ? g_omni.scale_key : g_omni.last_root;
        /* One octave above the first pad (+12 semitones) + ribbon_octave_off */
        int base_midi = ((int)g_omni.base_octave + 2 + (int)g_omni.ribbon_octave_off) * 12 + (int)key;
        for (uint8_t s = 0; s < 7; s++) {
            int note = base_midi + (int)k_scale_intervals[s_idx][s];
            if (note < 0) note = 0;
            if (note > 127) note = 127;
            g_omni.harp_notes[s] = (uint8_t)note;
        }
        int oct_note = base_midi + 12;
        if (oct_note < 0) oct_note = 0;
        if (oct_note > 127) oct_note = 127;
        g_omni.harp_notes[7] = (uint8_t)oct_note;
        return;
    }

    uint8_t tones[4];
    uint8_t num_tones = _build_chord_tones(tones);
    g_omni.chord_ribbon_count = num_tones;

    int base_midi = ((int)g_omni.base_octave + 1 + (int)g_omni.last_octave_off + (int)g_omni.ribbon_octave_off) * 12 + (int)g_omni.last_root;
    if (g_omni.ribbon_mode == RIBBON_CHORD) {
        for (uint8_t s = 0; s < num_tones; s++) {
            int note = base_midi + (int)tones[s];
            if (note < 0) note = 0;
            if (note > 127) note = 127;
            g_omni.harp_notes[s] = (uint8_t)note;
        }
        return;
    }

    for (uint8_t s = 0; s < NUM_HARP_STRINGS; s++) {
        uint8_t oct = s / num_tones;
        uint8_t deg = s % num_tones;
        int note = base_midi + (int)(oct * 12 + tones[deg]);
        if (note < 0) note = 0;
        if (note > 127) note = 127;
        g_omni.harp_notes[s] = (uint8_t)note;
    }
}

/* Stop currently sounding chord backing notes */
static void _stop_chord_backing(void) {
    g_omni.chord_retrig_ms = 0;
    if (g_omni.sounding_chord_count > 0) {
        for (uint8_t i = 0; i < g_omni.sounding_chord_count; i++) {
            ft_send_note_off((char)g_omni.sounding_chord_chan,
                             (char)g_omni.sounding_chord_notes[i], 0);
        }
        g_omni.sounding_chord_count = 0;
        g_omni.chord_off_cooldown_ms = 25;
    }
}

static void _fire_chord_notes(uint8_t vel) {
    if (!g_omni.chord_backing_on) {
        return;
    }

    uint8_t tones[4];
    uint8_t num_tones = _build_chord_tones(tones);
    uint8_t base_midi = (uint8_t)((g_omni.base_octave + 1 + g_omni.last_octave_off) * 12 + g_omni.last_root);
    uint8_t count = 0;

    for (uint8_t i = 0; i < num_tones; i++) {
        uint8_t note = (uint8_t)(base_midi + tones[i]);
        if (note <= 127) {
            g_omni.sounding_chord_notes[count++] = note;
            ft_send_note_on((char)g_omni.midi_channel, (char)note, (char)vel);
        }
    }
    g_omni.sounding_chord_count = count;
    g_omni.sounding_chord_chan = g_omni.midi_channel;
}

/* Trigger chord backing notes for (last_root, last_octave_off, current_quality, voicing) */
static void _start_chord_backing(uint8_t vel) {
    _stop_chord_backing();

    if (!g_omni.chord_backing_on) {
        return;
    }

    if (g_omni.chord_off_cooldown_ms > 0) {
        /* Guarantee >= 25ms Note-Off -> Note-On gap even on fast non-legato changes */
        g_omni.chord_retrig_vel = vel;
        g_omni.chord_retrig_ms = g_omni.chord_off_cooldown_ms;
    } else {
        _fire_chord_notes(vel);
    }
}

/* Evaluate held quality pads (12..15) or fallback to sticky_quality */
static uint8_t _resolve_quality_from_pads(void) {
    uint8_t q_mask = (uint8_t)((g_omni.held_pads_mask >> 12) & 0x0F);

    if (q_mask == 0) {
        return g_omni.sticky_quality;
    }

    /* Single quality pad held */
    if (q_mask == 0x01) return QUAL_MAJ;
    if (q_mask == 0x02) return QUAL_MIN;
    if (q_mask == 0x04) return QUAL_HALF_DIM;
    if (q_mask == 0x08) return QUAL_DIM;

    /* 2-pad Omnichord combinations */
    if (q_mask == (0x01 | 0x02)) return QUAL_DOM7;  /* Maj + Min */
    if (q_mask == (0x01 | 0x04)) return QUAL_MAJ7;  /* Maj + Half-Dim */
    if (q_mask == (0x02 | 0x04)) return QUAL_MIN7;  /* Min + Half-Dim */
    if (q_mask == (0x01 | 0x08)) return QUAL_AUG;   /* Maj + Dim */
    if (q_mask == (0x02 | 0x08)) return QUAL_SUS4;  /* Min + Dim */

    return g_omni.sticky_quality;
}

/* Trigger a single harp string note with polyphonic auto-gate management */
static void _pluck_harp_string(uint8_t string_idx, uint8_t velocity) {
    if (string_idx >= NUM_HARP_STRINGS || velocity == 0) {
        return;
    }

    uint8_t note = g_omni.harp_notes[string_idx];
    uint8_t chan = g_omni.ribbon_channel;
    if (velocity > 127) {
        velocity = 127;
    }

    /* If same note is already ringing in a voice slot, retrigger it cleanly */
    int8_t slot = -1;
    uint16_t min_timer = 0xFFFF;
    int8_t oldest_slot = 0;

    for (uint8_t i = 0; i < MAX_HARP_VOICES; i++) {
        if (g_omni.strum_voice_timer[i] > 0 &&
            g_omni.strum_voice_note[i] == note &&
            g_omni.strum_voice_chan[i] == chan) {
            ft_send_note_off((char)chan, (char)note, 0);
            g_omni.strum_voice_timer[i] = 0;
            slot = (int8_t)i;
            break;
        }
        if (g_omni.strum_voice_timer[i] == 0 && slot < 0) {
            slot = (int8_t)i;
        }
        if (g_omni.strum_voice_timer[i] < min_timer) {
            min_timer = g_omni.strum_voice_timer[i];
            oldest_slot = (int8_t)i;
        }
    }

    if (slot < 0) {
        slot = oldest_slot;
        if (g_omni.strum_voice_timer[(uint8_t)slot] > 0) {
            ft_send_note_off((char)g_omni.strum_voice_chan[(uint8_t)slot],
                             (char)g_omni.strum_voice_note[(uint8_t)slot], 0);
        }
    }

    ft_send_note_on((char)chan, (char)note, (char)velocity);
    g_omni.strum_voice_note[(uint8_t)slot] = note;
    g_omni.strum_voice_chan[(uint8_t)slot] = chan;
    g_omni.strum_voice_timer[(uint8_t)slot] = g_omni.strum_gate_ms;

    g_omni.last_strum_note = note;
    g_omni.last_strum_vel = velocity;
}

/*--- Panel Callbacks ---*/

static void _trigger_callback(uint8_t raw_pad, uint8_t raw_vel, bool state) {
    uint8_t pad = (raw_pad >= 0x11) ? (raw_pad - 0x11) : raw_pad;
    if (pad >= 16) {
        return;
    }

    bool was_held = (g_omni.held_pads_mask & (1U << pad)) != 0;
    if (state == was_held) {
        /* Ignore continuous pressure / aftertouch repeats while pad stays held */
        return;
    }

    (void)raw_vel;

    if (state) {
        g_omni.held_pads_mask |= (uint16_t)(1U << pad);
    } else {
        g_omni.held_pads_mask &= (uint16_t)~(1U << pad);
    }

    /* Pads 13..16 (index 12..15): Chord Quality Selectors (also override AUTO scale!) */
    if (pad >= 12) {
        if (state) {
            uint8_t q_idx = (uint8_t)(pad - 12);
            g_omni.sticky_quality = q_idx;
            g_omni.quality_overridden = true;
            g_omni.current_quality = _resolve_quality_from_pads();
            _rebuild_harp_table();

            /* If a chord is currently sounding (held or latched), re-voice it live! */
            if (g_omni.active_root_pad >= 0) {
                _start_chord_backing(g_omni.chord_vel);
            }
        } else {
            uint8_t new_q = _resolve_quality_from_pads();
            if (new_q != g_omni.current_quality) {
                g_omni.current_quality = new_q;
                _rebuild_harp_table();
                if (g_omni.active_root_pad >= 0) {
                    _start_chord_backing(g_omni.chord_vel);
                }
            }
        }
        return;
    }

    /* Pads 1..12 (index 0..11): Chord Root / Scale Degree Selectors */
    if (state) {
        bool q_held = ((g_omni.held_pads_mask >> 12) & 0x0F) != 0;
        if (g_omni.scale_mode != SCALE_MODE_FREE && !q_held) {
            g_omni.current_quality = _pad_auto_quality(pad);
            g_omni.quality_overridden = false;
        } else {
            g_omni.current_quality = _resolve_quality_from_pads();
            g_omni.quality_overridden = (g_omni.scale_mode != SCALE_MODE_FREE);
        }

        if (g_omni.latch_mode && g_omni.active_root_pad == (int8_t)pad) {
            /* Tapping the already-latched root turns the chord backing off */
            _stop_chord_backing();
            g_omni.active_root_pad = -1;
        } else {
            g_omni.active_root_pad = (int8_t)pad;
            g_omni.last_deg = (uint8_t)(pad % 7);
            g_omni.last_root = _pad_to_root(pad, &g_omni.last_octave_off);
            _rebuild_harp_table();
            _start_chord_backing(g_omni.chord_vel);
        }
    } else {
        /* Pad release in Momentary mode */
        if (!g_omni.latch_mode && g_omni.active_root_pad == (int8_t)pad) {
            /* Check if another root pad (0..11) is still held */
            uint16_t root_mask = g_omni.held_pads_mask & 0x0FFF;
            if (root_mask != 0) {
                for (uint8_t p = 0; p < 12; p++) {
                    if (root_mask & (1U << p)) {
                        g_omni.active_root_pad = (int8_t)p;
                        g_omni.last_deg = (uint8_t)(p % 7);
                        g_omni.last_root = _pad_to_root(p, &g_omni.last_octave_off);
                        bool q_held = ((g_omni.held_pads_mask >> 12) & 0x0F) != 0;
                        if (g_omni.scale_mode != SCALE_MODE_FREE && !q_held) {
                            g_omni.current_quality = _pad_auto_quality(p);
                            g_omni.quality_overridden = false;
                        }
                        _rebuild_harp_table();
                        _start_chord_backing(g_omni.chord_vel);
                        break;
                    }
                }
            } else {
                /* Stop chord pad sound, but KEEP g_omni.last_root & harp_notes for Ribbon! */
                _stop_chord_backing();
                g_omni.active_root_pad = -1;
            }
        }
    }
}

static void _release_xy_ribbon_voices(void) {
    g_omni.current_zone = -1;
    for (uint8_t i = 0; i < MAX_HARP_VOICES; i++) {
        if (g_omni.strum_voice_timer[i] > 0) {
            ft_send_note_off((char)g_omni.strum_voice_chan[i],
                             (char)g_omni.strum_voice_note[i], 0);
            g_omni.strum_voice_timer[i] = 0;
        }
    }
}

static void _xy_pad_callback(uint32_t x_val, uint32_t y_val) {
    g_omni.raw_x = x_val;
    g_omni.raw_y = y_val;

    /* Ignore explicit zero release packet if sent by MCU */
    if (x_val == 0 && y_val == 0) {
        g_omni.xy_timeout_ms = 0;
        _release_xy_ribbon_voices();
        return;
    }

    /* Dynamic auto-ranging so full width/height of the XY pad maps to 0..127 */
    if (x_val < g_omni.x_min) g_omni.x_min = x_val;
    if (x_val > g_omni.x_max) g_omni.x_max = x_val;
    if (y_val < g_omni.y_min) g_omni.y_min = y_val;
    if (y_val > g_omni.y_max) g_omni.y_max = y_val;

    uint32_t x_span = (g_omni.x_max > g_omni.x_min) ? (g_omni.x_max - g_omni.x_min) : 1;
    uint32_t y_span = (g_omni.y_max > g_omni.y_min) ? (g_omni.y_max - g_omni.y_min) : 1;

    uint32_t nx = ((x_val - g_omni.x_min) * 127U) / x_span;
    uint32_t ny = ((y_val - g_omni.y_min) * 127U) / y_span;
    if (nx > 127) nx = 127;
    if (ny > 127) ny = 127;

    g_omni.norm_x = (uint8_t)nx;
    /* Map Y to velocity 15..127 so even low touches remain audible */
    g_omni.norm_y = (uint8_t)(15 + ((ny * 112U) / 127U));

    /* Map X (0..127) to 12 strings (CLASSIC), 8 scale notes (SCALE), or chord tones (CHORD) */
    uint8_t num_zones = NUM_HARP_STRINGS;
    if (g_omni.ribbon_mode == RIBBON_SCALE) {
        num_zones = NUM_SCALE_STRINGS;
    } else if (g_omni.ribbon_mode == RIBBON_CHORD) {
        num_zones = (g_omni.chord_ribbon_count > 0) ? g_omni.chord_ribbon_count : 3;
    }
    int8_t zone = (int8_t)((nx * num_zones) / 128U);
    if (zone < 0) zone = 0;
    if (zone >= (int8_t)num_zones) zone = (int8_t)(num_zones - 1);

    bool was_released = (g_omni.xy_timeout_ms == 0);
    g_omni.xy_timeout_ms = 45; /* Real 45ms finger-lift watchdog */

    if (was_released || zone != g_omni.current_zone) {
        g_omni.current_zone = zone;
        _pluck_harp_string((uint8_t)zone, g_omni.ribbon_vel);
    }
}

static void _set_scale_mode(uint8_t new_mode) {
    if (new_mode >= SCALE_MODE_COUNT) {
        new_mode = SCALE_MODE_FREE;
    }
    g_omni.scale_mode = new_mode;
    if (new_mode != SCALE_MODE_FREE) {
        g_omni.last_auto_scale = new_mode;
        g_omni.last_root = g_omni.scale_key;
        g_omni.last_octave_off = 0;
        g_omni.current_quality = k_scale_qualities[new_mode - 1][0];
        g_omni.quality_overridden = false;
    } else {
        g_omni.last_octave_off = 0;
        g_omni.quality_overridden = false;
    }
    _stop_chord_backing();
    g_omni.active_root_pad = -1;
    _rebuild_harp_table();
}

static void _button_callback(uint8_t index, bool state) {
    if (index == BTN_SHIFT) {
        g_omni.shift_held = state;
        return;
    }

    if (!state) {
        return;
    }

    g_omni.last_btn_id = index;

    switch (index) {
    case BTN_AMP_EG:
    case BTN_BPF:
    case BTN_CHORD:
    case 0x1d: /* MFX Hold candidate ID */
    case 0x1b: /* Touch Scale candidate ID */
        g_omni.latch_mode = !g_omni.latch_mode;
        if (!g_omni.latch_mode && g_omni.active_root_pad >= 0 &&
            (g_omni.held_pads_mask & 0x0FFF) == 0) {
            _stop_chord_backing();
            g_omni.active_root_pad = -1;
        }
        _set_status(g_omni.latch_mode ? "MODE: TOGGLE/HOLD" : "MODE: MOMENTARY");
        break;

    case BTN_HPF:
    case 0x1c: /* Master FX candidate ID */
        g_omni.chord_backing_on = !g_omni.chord_backing_on;
        if (!g_omni.chord_backing_on) {
            _stop_chord_backing();
        } else if (g_omni.active_root_pad >= 0) {
            _start_chord_backing(g_omni.chord_vel);
        }
        _set_status(g_omni.chord_backing_on ? "CHORD PAD: ON" : "RIBBON ONLY");
        break;

    case BTN_LPF:
        if (g_omni.scale_mode == SCALE_MODE_FREE) {
            g_omni.circle_of_fifths = !g_omni.circle_of_fifths;
            _set_status(g_omni.circle_of_fifths ? "LAYOUT: 5THS" : "LAYOUT: CHROMATIC");
        } else {
            uint8_t next_s = (uint8_t)(g_omni.scale_mode + 1);
            if (next_s >= SCALE_MODE_COUNT) {
                next_s = SCALE_MODE_MAJOR;
            }
            _set_scale_mode(next_s);
            _set_status(k_scale_names[g_omni.scale_mode]);
        }
        break;

    case BTN_KEYBOARD:
    case BTN_SEQUENCER:
        if (g_omni.scale_mode == SCALE_MODE_FREE) {
            _set_scale_mode(g_omni.last_auto_scale);
            _set_status("AUTO SCALE: ON");
        } else {
            _set_scale_mode(SCALE_MODE_FREE);
            _set_status("MODE: FREE");
        }
        break;

    case BTN_TRIGGER:
    case BTN_MENU:
    case 0x1a: { /* Gate Arp candidate ID */
        g_omni.ribbon_mode = (uint8_t)((g_omni.ribbon_mode + 1) % RIBBON_MODE_COUNT);
        _release_xy_ribbon_voices();
        _rebuild_harp_table();
        static const char *const k_rib_names[RIBBON_MODE_COUNT] = {
            "RIBBON: CLASSIC", "RIBBON: SCALE", "RIBBON: CHORD"
        };
        _set_status(k_rib_names[g_omni.ribbon_mode]);
        break;
    }

    case BTN_PLAY:
        g_omni.clock_playing = !g_omni.clock_playing;
        if (g_omni.clock_playing) {
            g_omni.clock_accum_us = 0;
            g_omni.clock_tick_24ppqn = 0;
            svc_midi_send_byte(0xFA); /* MIDI Start */
            _set_status("MIDI SYNC: START");
        } else {
            svc_midi_send_byte(0xFC); /* MIDI Stop */
            _set_status("MIDI SYNC: STOP");
        }
        break;

    case BTN_STOP:
        if (g_omni.clock_playing) {
            g_omni.clock_playing = false;
            svc_midi_send_byte(0xFC);
        }
        _stop_chord_backing();
        g_omni.active_root_pad = -1;
        _set_status("ALL NOTES OFF");
        break;

    case BTN_LEFT:
    case BTN_BACK:
        if (g_omni.shift_held) {
            if (g_omni.ribbon_octave_off > -2) {
                g_omni.ribbon_octave_off--;
                _release_xy_ribbon_voices();
                _rebuild_harp_table();
            }
            char msg[16];
            snprintf(msg, sizeof(msg), "RIB OCT: %d", (int)g_omni.ribbon_octave_off);
            _set_status(msg);
        } else if (g_omni.base_octave > 1) {
            g_omni.base_octave--;
            _rebuild_harp_table();
            if (g_omni.active_root_pad >= 0) {
                _start_chord_backing(g_omni.chord_vel);
            }
            char msg[16];
            snprintf(msg, sizeof(msg), "OCTAVE: %d", g_omni.base_octave);
            _set_status(msg);
        }
        break;

    case BTN_RIGHT:
    case BTN_FORWARD:
        if (g_omni.shift_held) {
            if (g_omni.ribbon_octave_off < 2) {
                g_omni.ribbon_octave_off++;
                _release_xy_ribbon_voices();
                _rebuild_harp_table();
            }
            char msg[16];
            snprintf(msg, sizeof(msg), "RIB OCT: %d", (int)g_omni.ribbon_octave_off);
            _set_status(msg);
        } else if (g_omni.base_octave < 5) {
            g_omni.base_octave++;
            _rebuild_harp_table();
            if (g_omni.active_root_pad >= 0) {
                _start_chord_backing(g_omni.chord_vel);
            }
            char msg[16];
            snprintf(msg, sizeof(msg), "OCTAVE: %d", g_omni.base_octave);
            _set_status(msg);
        }
        break;

    case BTN_EXIT:
        if (g_omni.shift_held && g_omni.shutdown_timer_ms == 0) {
            _stop_chord_backing();
            g_omni.shutdown_timer_ms = 9000; /* 6s copla + 3s Cuarti Corp */
        }
        break;

    default:
        break;
    }
}

static void _encoder_callback(uint8_t index, int8_t raw_val) {
    int delta = (raw_val == 1 || raw_val > 0) ? 1 : -1;

    switch (index) {
    case ENC_MAIN: {
        if (g_omni.shift_held) {
            int pv = (int)g_omni.chord_vel + delta * 4;
            if (pv < 1) pv = 1;
            if (pv > 127) pv = 127;
            g_omni.chord_vel = (uint8_t)pv;
            char msg[16];
            snprintf(msg, sizeof(msg), "PAD VEL: %d", g_omni.chord_vel);
            _set_status(msg);
        } else {
            int bpm = (int)g_omni.bpm_tenths + delta * 10;
            if (bpm < 300) bpm = 300;
            if (bpm > 3000) bpm = 3000;
            g_omni.bpm_tenths = (uint16_t)bpm;
        }
        break;
    }
    case ENC_OSC: {
        if (g_omni.shift_held) {
            _release_xy_ribbon_voices();
            int ch = (int)g_omni.ribbon_channel + delta;
            if (ch < 0) ch = 15;
            if (ch > 15) ch = 0;
            g_omni.ribbon_channel = (uint8_t)ch;
            char msg[16];
            snprintf(msg, sizeof(msg), "RIBBON CH: %d", g_omni.ribbon_channel + 1);
            _set_status(msg);
        } else {
            _stop_chord_backing();
            int ch = (int)g_omni.midi_channel + delta;
            if (ch < 0) ch = 15;
            if (ch > 15) ch = 0;
            g_omni.midi_channel = (uint8_t)ch;
            char msg[16];
            snprintf(msg, sizeof(msg), "CHORD CH: %d", g_omni.midi_channel + 1);
            _set_status(msg);
        }
        break;
    }
    case ENC_CUTOFF: {
        if (g_omni.shift_held) {
            int oct = (int)g_omni.base_octave + delta;
            if (oct < 1) oct = 1;
            if (oct > 5) oct = 5;
            g_omni.base_octave = (uint8_t)oct;
            _rebuild_harp_table();
            if (g_omni.active_root_pad >= 0) {
                _start_chord_backing(g_omni.chord_vel);
            }
            char msg[16];
            snprintf(msg, sizeof(msg), "OCTAVE: %d", g_omni.base_octave);
            _set_status(msg);
        } else {
            int v = (int)g_omni.voicing_mode + delta;
            if (v < 0) v = 0;
            if (v >= VOICING_COUNT) v = VOICING_COUNT - 1;
            g_omni.voicing_mode = (uint8_t)v;
            _rebuild_harp_table();
            if (g_omni.active_root_pad >= 0) {
                _start_chord_backing(g_omni.chord_vel);
            }
            _set_status(k_voicing_names[g_omni.voicing_mode]);
        }
        break;
    }
    case ENC_MOD: {
        int sm = (int)g_omni.scale_mode + delta;
        if (sm < 0) sm = SCALE_MODE_COUNT - 1;
        if (sm >= SCALE_MODE_COUNT) sm = 0;
        _set_scale_mode((uint8_t)sm);
        _set_status(k_scale_names[g_omni.scale_mode]);
        break;
    }
    case ENC_IFX: {
        int k = (int)g_omni.scale_key + delta;
        if (k < 0) k = 11;
        if (k > 11) k = 0;
        g_omni.scale_key = (uint8_t)k;
        if (g_omni.scale_mode != SCALE_MODE_FREE) {
            _set_scale_mode(g_omni.scale_mode);
        }
        _set_status("SCALE KEY");
        break;
    }
    default:
        break;
    }
}

static void _knob_callback(uint8_t index, uint8_t value) {
    uint8_t val7 = value >> 1;
    if (index == KNOB_DECAY) {
        /* Adjust ribbon string ring duration (30ms .. 1200ms) */
        g_omni.strum_gate_ms = (uint16_t)(30 + (uint32_t)val7 * 9U);
    } else if (index == KNOB_LEVEL) {
        /* Level knob -> Pad Chord Velocity (1..127, default 127) */
        g_omni.chord_vel = (val7 < 1) ? 1 : val7;
        char msg[16];
        snprintf(msg, sizeof(msg), "PAD VEL: %d", g_omni.chord_vel);
        _set_status(msg);
    } else if (index == KNOB_EDIT || index == KNOB_PITCH) {
        /* Oscillator Edit / Pitch knob -> Ribbon Master Velocity (0..127) */
        g_omni.ribbon_vel = val7;
        g_omni.last_strum_vel = g_omni.ribbon_vel;
        char msg[16];
        snprintf(msg, sizeof(msg), "RIB VEL: %d", g_omni.ribbon_vel);
        _set_status(msg);
    }
}

/* Process real elapsed hardware milliseconds from systick_get() */
static void _process_elapsed_ms(uint32_t dt) {
    if (dt == 0) {
        return;
    }

    if (g_omni.shutdown_timer_ms > 0) {
        if (dt >= g_omni.shutdown_timer_ms) {
            g_omni.shutdown_timer_ms = 0;
            g_omni.do_shutdown = true;
        } else {
            g_omni.shutdown_timer_ms -= (uint16_t)dt;
        }
    }

    if (g_omni.status_timer_ms > 0) {
        if (dt >= g_omni.status_timer_ms) {
            g_omni.status_timer_ms = 0;
        } else {
            g_omni.status_timer_ms -= (uint16_t)dt;
        }
    }

    if (g_omni.chord_off_cooldown_ms > 0) {
        if (dt >= g_omni.chord_off_cooldown_ms) {
            g_omni.chord_off_cooldown_ms = 0;
        } else {
            g_omni.chord_off_cooldown_ms -= (uint8_t)dt;
        }
    }

    /* Delayed Note-On after Note-Off on fast/legato chord switch (forces envelope reset) */
    if (g_omni.chord_retrig_ms > 0) {
        if (dt >= g_omni.chord_retrig_ms) {
            g_omni.chord_retrig_ms = 0;
            if (g_omni.chord_backing_on && g_omni.active_root_pad >= 0) {
                _fire_chord_notes(g_omni.chord_retrig_vel);
            }
        } else {
            g_omni.chord_retrig_ms -= (uint8_t)dt;
        }
    }

    /* XY Pad touch release timeout */
    if (g_omni.xy_timeout_ms > 0) {
        if (dt >= g_omni.xy_timeout_ms) {
            g_omni.xy_timeout_ms = 0;
            _release_xy_ribbon_voices();
        } else {
            g_omni.xy_timeout_ms -= (uint16_t)dt;
        }
    }

    /* Decrement active harp string Note-Off timers */
    for (uint8_t i = 0; i < MAX_HARP_VOICES; i++) {
        if (g_omni.strum_voice_timer[i] > 0) {
            if (dt >= g_omni.strum_voice_timer[i]) {
                g_omni.strum_voice_timer[i] = 0;
                ft_send_note_off((char)g_omni.strum_voice_chan[i],
                                 (char)g_omni.strum_voice_note[i], 0);
            } else {
                g_omni.strum_voice_timer[i] -= (uint16_t)dt;
            }
        }
    }

    /* 24 PPQN MIDI Clock generator (1000us per real ms) */
    if (g_omni.clock_playing) {
        uint32_t us_per_clk = 25000000U / (uint32_t)g_omni.bpm_tenths;
        g_omni.clock_accum_us += dt * 1000U;
        while (g_omni.clock_accum_us >= us_per_clk) {
            g_omni.clock_accum_us -= us_per_clk;
            svc_midi_send_byte(0xF8);
            g_omni.clock_tick_24ppqn++;
        }
    }
}

static void _update_leds(void) {
    /* Mode & Status LEDs */
    ft_set_led(LED_TOUCH_SCALE, 255);
    ft_set_led(LED_CHORD, 255);
    ft_set_led(LED_MFX_HOLD, g_omni.latch_mode ? 255 : 0);
    ft_set_led(LED_AMP_EG, g_omni.latch_mode ? 255 : 0);
    ft_set_led(LED_BPF, g_omni.latch_mode ? 255 : 0);
    ft_set_led(LED_HPF, g_omni.chord_backing_on ? 255 : 0);
    ft_set_led(LED_MASTER_FX, g_omni.chord_backing_on ? 255 : 0);
    ft_set_led(LED_LPF, (g_omni.scale_mode == SCALE_MODE_FREE && g_omni.circle_of_fifths) ? 255 : 0);
    ft_set_led(LED_KEYBOARD, (g_omni.scale_mode != SCALE_MODE_FREE) ? 255 : 0);
    ft_set_led(LED_TRIGGER, (g_omni.ribbon_mode == RIBBON_SCALE) ? 255 : 0);
    ft_set_led(LED_GATE_ARP, (g_omni.ribbon_mode == RIBBON_SCALE) ? 255 : 0);
    ft_set_led(LED_PLAY, g_omni.clock_playing ? 255 : 0);
    ft_set_led(LED_SHIFT, g_omni.shift_held ? 255 : 0);

    /* 4 Bar LEDs flash quarter-note beat when MIDI Clock is running */
    uint8_t beat = (uint8_t)((g_omni.clock_tick_24ppqn / 24U) % 4U);
    for (uint8_t b = 0; b < 4; b++) {
        ft_set_led(k_bar_red_leds[b], (g_omni.clock_playing && b == beat) ? 255 : 0);
        ft_set_led(k_bar_blue_leds[b], (!g_omni.clock_playing && b == 0) ? 100 : 0);
    }

    /* Pads 1..12 (0..11): Chord Roots / Scale Degrees */
    for (uint8_t p = 0; p < 12; p++) {
        uint8_t root = _pad_to_root(p, NULL);
        uint8_t r = 0;
        uint8_t bl = 90;

        if (g_omni.scale_mode != SCALE_MODE_FREE && p >= 7) {
            bl = 35; /* Dimmer blue for upper octave degrees 8..12 */
        }

        if (g_omni.active_root_pad == (int8_t)p) {
            /* Currently sounding chord pad */
            r = 255;
            bl = 255;
        } else if (root == g_omni.last_root &&
                   (g_omni.scale_mode == SCALE_MODE_FREE || (p / 7) == g_omni.last_octave_off)) {
            /* Remembered root loaded into the XY Ribbon */
            r = 180;
            bl = 40;
        }
        ft_set_led(k_pad_red_leds[p], r);
        ft_set_led(k_pad_blue_leds[p], bl);
    }

    /* Pads 13..16 (12..15): 4 Qualities (Maj, Min, Half-Dim, Dim) */
    uint8_t q_held = (uint8_t)((g_omni.held_pads_mask >> 12) & 0x0F);
    for (uint8_t q = 0; q < 4; q++) {
        uint8_t pad = (uint8_t)(12 + q);
        uint8_t r = 110;
        uint8_t bl = 0;

        bool active_auto_q = (g_omni.scale_mode != SCALE_MODE_FREE &&
                              !g_omni.quality_overridden &&
                              g_omni.current_quality == q);
        bool active_free_q = ((g_omni.scale_mode == SCALE_MODE_FREE || g_omni.quality_overridden) &&
                              g_omni.sticky_quality == q);

        if ((q_held & (1U << q)) || (q_held == 0 && (active_auto_q || active_free_q))) {
            r = 255;
            bl = 220;
        }
        ft_set_led(k_pad_red_leds[pad], r);
        ft_set_led(k_pad_blue_leds[pad], bl);
    }
}

static void _render_oled(void) {
    static char line[32];

    UG_FillFrame(0, 0, 127, 63, C_WHITE);
    UG_FontSelect(FONT_5X8);
    UG_SetBackcolor(C_WHITE);
    UG_SetForecolor(C_BLACK);

    /* Easter Egg Shutdown Sequence (6s copla + 3s Cuarti Corp) */
    if (g_omni.shutdown_timer_ms > 3000) {
        UG_PutString(0, 6,  "Si a la ma\xA4" "ana dormia");
        UG_PutString(0, 18, "Y a la tarde bebia");
        /* Draw exact 180-deg rotated '?' at (0, 30) */
        UG_DrawPixel(2, 30, C_BLACK);
        UG_DrawPixel(2, 32, C_BLACK);
        UG_DrawPixel(1, 33, C_BLACK);
        UG_DrawPixel(0, 34, C_BLACK);
        UG_DrawPixel(1, 35, C_BLACK);
        UG_DrawPixel(2, 35, C_BLACK);
        UG_DrawPixel(3, 35, C_BLACK);
        UG_PutString(6, 30, "Sera muy buena");
        UG_PutString(0, 40, "la noche");
        UG_PutString(0, 52, "Buena como este dia?");
        return;
    } else if (g_omni.shutdown_timer_ms > 0) {
        UG_PutString(30, 14, "CUARTICHORD");
        UG_PutString(18, 26, "IS A PRODUCT OF");
        UG_PutString(10, 38, "CUARTI CORPORATION");
        return;
    }

    /* Top Bar: Title, MIDI Channel, BPM & Sync state */
    snprintf(line, sizeof(line), "CUARTICHORD C%02d %3d%s",
             g_omni.midi_channel + 1,
             g_omni.bpm_tenths / 10,
             g_omni.clock_playing ? "*" : " ");
    UG_PutString(0, 0, line);
    UG_DrawLine(0, 9, 127, 9, C_BLACK);

    /* Chord Display, Voicing & Mode (MOMENTARY vs HOLD) */
    snprintf(line, sizeof(line), "%s %s%s V:%s %s",
             k_note_names[g_omni.last_root % 12],
             k_qual_names[g_omni.current_quality % QUAL_COUNT],
             g_omni.quality_overridden ? "*" : "",
             k_voicing_names[g_omni.voicing_mode % VOICING_COUNT],
             g_omni.latch_mode ? "HLD" : "MOM");
    UG_PutString(0, 12, line);

    /* Scale Mode / Key & Octave */
    if (g_omni.scale_mode == SCALE_MODE_FREE) {
        snprintf(line, sizeof(line), "FREE:%s O:%d %s",
                 g_omni.circle_of_fifths ? "5THS" : "CHRM",
                 g_omni.base_octave,
                 g_omni.chord_backing_on ? (g_omni.active_root_pad >= 0 ? "PLY" : "RDY") : "MUT");
    } else {
        snprintf(line, sizeof(line), "AUTO:%s %s O:%d",
                 k_note_names[g_omni.scale_key % 12],
                 k_scale_names[g_omni.scale_mode],
                 g_omni.base_octave);
    }
    UG_PutString(0, 22, line);

    /* Visual Ribbon Strumplate: 12 boxes (CLASSIC), 8 boxes (SCALE), or 2..4 boxes (CHORD) */
    if (g_omni.ribbon_mode == RIBBON_CHORD) {
        uint8_t n = (g_omni.chord_ribbon_count > 0) ? g_omni.chord_ribbon_count : 3;
        uint16_t step = (uint16_t)(120 / n);
        uint16_t w = (uint16_t)(step - 2);
        for (uint8_t s = 0; s < n; s++) {
            uint16_t x0 = (uint16_t)(4 + s * step);
            uint16_t x1 = (uint16_t)(x0 + w);
            if (g_omni.current_zone == (int8_t)s) {
                UG_FillFrame(x0, 32, x1, 43, C_BLACK);
            } else {
                UG_DrawFrame(x0, 32, x1, 43, C_BLACK);
            }
        }
    } else if (g_omni.ribbon_mode == RIBBON_SCALE) {
        for (uint8_t s = 0; s < NUM_SCALE_STRINGS; s++) {
            uint16_t x0 = (uint16_t)(4 + s * 15);
            uint16_t x1 = x0 + 13;
            if (g_omni.current_zone == (int8_t)s) {
                UG_FillFrame(x0, 32, x1, 43, C_BLACK);
            } else {
                UG_DrawFrame(x0, 32, x1, 43, C_BLACK);
            }
        }
    } else {
        for (uint8_t s = 0; s < NUM_HARP_STRINGS; s++) {
            uint16_t x0 = (uint16_t)(4 + s * 10);
            uint16_t x1 = x0 + 8;
            if (g_omni.current_zone == (int8_t)s) {
                UG_FillFrame(x0, 32, x1, 43, C_BLACK);
            } else {
                UG_DrawFrame(x0, 32, x1, 43, C_BLACK);
            }
        }
    }

    /* XY Ribbon Telemetry Line (Mode, Note, Velocity, Ribbon Ch, Zone) */
    uint8_t s_note = g_omni.last_strum_note;
    static const char *const k_rib_tags[RIBBON_MODE_COUNT] = { "STR", "SCL", "CHD" };
    snprintf(line, sizeof(line), "%s:%s%d V%03d R%02d Z%02d",
             k_rib_tags[g_omni.ribbon_mode % RIBBON_MODE_COUNT],
             k_note_names[s_note % 12],
             (int)(s_note / 12) - 1,
             g_omni.ribbon_vel,
             g_omni.ribbon_channel + 1,
             g_omni.current_zone >= 0 ? g_omni.current_zone + 1 : 0);
    UG_PutString(0, 45, line);

    /* Bottom Status Bar (clean when idle) */
    UG_DrawLine(0, 54, 127, 54, C_BLACK);
    if (g_omni.status_timer_ms > 0) {
        UG_PutString(0, 56, g_omni.status_line);
    }
}

t_status app_init(void) {
    memset(&g_omni, 0, sizeof(g_omni));
    g_omni.midi_channel = 0;        /* Chords MIDI Ch 1 */
    g_omni.ribbon_channel = 0;      /* Ribbon MIDI Ch 1 */
    g_omni.base_octave = 4;         /* MIDI 60 base */
    g_omni.ribbon_octave_off = 0;
    g_omni.voicing_mode = VOICING_3;/* Triad (1+3+5) by default */
    g_omni.latch_mode = false;      /* Momentary by default */
    g_omni.chord_backing_on = true; /* Sound chord on pads + ribbon */
    g_omni.circle_of_fifths = false;/* Chromatic C..B by default */
    g_omni.strum_gate_ms = 140;     /* 140ms crisp string decay gate */
    g_omni.ribbon_vel = 85;         /* Default Ribbon Master Velocity = 85 */
    g_omni.last_strum_vel = 85;
    g_omni.ribbon_mode = RIBBON_CLASSIC;
    g_omni.chord_ribbon_count = 3;
    g_omni.scale_mode = SCALE_MODE_MAJOR;
    g_omni.last_auto_scale = SCALE_MODE_MAJOR;
    g_omni.scale_key = 0;           /* C */
    g_omni.active_root_pad = -1;
    g_omni.last_root = 0;           /* C */
    g_omni.sticky_quality = QUAL_MAJ;
    g_omni.current_quality = QUAL_MAJ;
    g_omni.chord_vel = 127;
    g_omni.current_zone = -1;
    g_omni.last_strum_note = 60;
    g_omni.bpm_tenths = 1200;       /* 120.0 BPM */

    /* Initial XY auto-calibration bounds */
    g_omni.x_min = 0xFFFF;
    g_omni.x_max = 1;
    g_omni.y_min = 0xFFFF;
    g_omni.y_max = 1;

    _rebuild_harp_table();
    _set_status("CUARTICHORD READY");

    g_ugui_device.x_dim = 128;
    g_ugui_device.y_dim = 64;
    g_ugui_device.pset = _ugui_pixel;
    UG_Init(&g_ugui, &g_ugui_device);
    UG_FontSelect(FONT_5X8);
    UG_FillScreen(C_WHITE);

    ft_set_trigger_mode(0);

    ft_register_panel_callback(TRIGGER_EVENT, _trigger_callback);
    ft_register_panel_callback(XY_PAD_EVENT, _xy_pad_callback);
    ft_register_panel_callback(BUTTON_EVENT, _button_callback);
    ft_register_panel_callback(ENCODER_EVENT, _encoder_callback);
    ft_register_panel_callback(KNOB_EVENT, _knob_callback);

    g_omni.last_systick_ms = systick_get();

    _update_leds();
    _render_oled();

    return SUCCESS;
}

void app_run(void) {
    uint32_t now = systick_get();
    uint32_t dt = now - g_omni.last_systick_ms;
    if (dt > 0) {
        g_omni.last_systick_ms = now;
        _process_elapsed_ms(dt);
        g_omni.ui_accum_ms += (uint16_t)dt;
    }

    if (g_omni.do_shutdown) {
        ft_shutdown();
        return;
    }

    if (g_omni.ui_accum_ms >= 33) {
        g_omni.ui_accum_ms = 0;
        _update_leds();
        _render_oled();
    }
}
