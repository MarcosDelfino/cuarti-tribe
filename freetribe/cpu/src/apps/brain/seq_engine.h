/*----------------------------------------------------------------------
 * CuartiTribe Brain - Advanced Generative & Polymetric MIDI Sequencer
 * Built on Freetribe for Korg Electribe 2 (and macOS Hardware Simulator)
 *----------------------------------------------------------------------*/

#ifndef SEQ_ENGINE_H
#define SEQ_ENGINE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SEQ_NUM_TRACKS 16
#define SEQ_MAX_STEPS 256
#define SEQ_STEPS_PER_BAR 16
#define SEQ_MAX_BARS (SEQ_MAX_STEPS / SEQ_STEPS_PER_BAR) /* 16 bars */
#define SEQ_TICKS_PER_STEP 24 /* 96 PPQN -> 24 ticks per 1/16th step */

/* Track Playback Directions (BeatStep Pro / OXI style) */
typedef enum {
    DIR_FORWARD = 0,
    DIR_REVERSE,
    DIR_PENDULUM,
    DIR_DRUNK,  /* Generative walk: 50% +1, 25% 0, 25% -1 */
    DIR_RANDOM, /* Pure random step inside track length */
    DIR_COUNT
} t_seq_direction;

/* Track Clock Division / Multiplication (Ambient to Aphex IDM) */
typedef enum {
    CLK_DIV_16 = 0, /* 1 step = 1 bar (ultra-slow ambient) */
    CLK_DIV_8,      /* 1 step = 1/2 note */
    CLK_DIV_4,      /* 1 step = 1/4 note */
    CLK_DIV_2,      /* 1 step = 1/8 note */
    CLK_1X,         /* 1 step = 1/16 note (standard) */
    CLK_2X,         /* 1 step = 1/32 note */
    CLK_3X,         /* 1 step = 1/24 triplet */
    CLK_4X,         /* 1 step = 1/64 note (Aphex drill) */
    CLK_SCALE_COUNT
} t_seq_clock_scale;

/* Elektron-style Trig Conditions */
typedef enum {
    COND_ALWAYS = 0,
    COND_1_2,
    COND_2_2,
    COND_1_3,
    COND_2_3,
    COND_3_3,
    COND_1_4,
    COND_2_4,
    COND_3_4,
    COND_4_4,
    COND_FILL,     /* Only when FILL (Tap) is held/latched */
    COND_NOT_FILL, /* Only when FILL is NOT active */
    COND_FIRST,    /* Only on first loop iteration */
    COND_PRE,      /* Only if previous trig fired */
    COND_COUNT
} t_seq_condition;

/* OP-1 / OP-Z Playful Step Components */
typedef enum {
    COMP_NONE = 0,
    COMP_OCT_UP,     /* 50% chance +1 octave */
    COMP_OCT_BIPOL,  /* Random -1, 0, +1 octave */
    COMP_FIFTH,      /* 50% chance +7 semitones */
    COMP_RAMP_UP,    /* Accumulates +2 semitones each loop */
    COMP_RAMP_DOWN,  /* Accumulates -2 semitones each loop */
    COMP_VEL_FADE,   /* Ramps velocity across repeats */
    COMP_GLITCH_RND, /* Randomize ratchet + microtiming on trigger */
    COMP_COUNT
} t_seq_component;

/* Chord / Voicing per step (for Ambient pads/chords on poly tracks) */
typedef enum {
    CHORD_NONE = 0, /* Single note */
    CHORD_FIFTH,    /* Root + 7 */
    CHORD_MIN,      /* Root + 3 + 7 */
    CHORD_MAJ,      /* Root + 4 + 7 */
    CHORD_SUS4,     /* Root + 5 + 7 */
    CHORD_MIN7,     /* Root + 3 + 7 + 10 */
    CHORD_MAJ7,     /* Root + 4 + 7 + 11 */
    CHORD_OCT,      /* Root + 12 */
    CHORD_TYPE_COUNT
} t_seq_chord;

/* Musical Scales for Generative / Quantizer Engine */
typedef enum {
    SCALE_CHROMATIC = 0,
    SCALE_MINOR,
    SCALE_MAJOR,
    SCALE_DORIAN,
    SCALE_PHRYGIAN,
    SCALE_LYDIAN,
    SCALE_PENTA_MIN,
    SCALE_HIRAJOSHI, /* Ambient Japanese pentatonic */
    SCALE_BHAIRAV,
    SCALE_WHOLETONE,
    SCALE_COUNT
} t_seq_scale_id;

/* UI / Pad Operating Modes (mapped to Electribe Trigger/Seq/Kbd/Chord/StepJump/Mute/Erase) */
typedef enum {
    MODE_SEQUENCER = 0, /* 16 pads = 16 steps of current bar (Hold pad = P-Lock!) */
    MODE_TRIGGER,       /* 16 pads = 16 tracks live trigger / select */
    MODE_KEYBOARD,      /* 16 pads = Chromatic/Scale keyboard for current track */
    MODE_GENERATIVE,    /* Chord button: Euclidean / Turing / Aphex / Ambient generator */
    MODE_STEP_JUMP,     /* Live step jump / loop roll on the 16 pads */
    MODE_PART_MUTE,     /* 16 pads mute/unmute the 16 tracks */
    MODE_PART_ERASE,    /* 16 pads clear/reset track or step */
    MODE_PATTERN_SET    /* 16 pads select/chain patterns or bar banks (1..16) */
} t_ui_mode;

/* Per-Step Data Structure (8 bytes packed -> 16 * 256 * 8 = 32 KB total) */
typedef struct {
    uint8_t flags;        /* bit0: active, bit1: tie, bits2..4: chord_type, bits5..6: ratchet_vel_mode */
    uint8_t note;         /* MIDI note 0..127 (default 60 = C4) */
    uint8_t velocity;     /* 1..127 (default 100) */
    uint8_t gate;         /* 1..96 ticks (24 = full 1/16 step, 96 = 4 steps ambient pad) */
    int8_t microtiming;   /* -23..+23 ticks offset */
    uint8_t probability;  /* 1..100 % */
    uint8_t cond_ratchet; /* high nibble: t_seq_condition, low nibble: ratchet count index (0=1x,1=2x,2=3x,3=4x,4=6x,5=8x) */
    uint8_t comp_cclock;  /* bits 0..2: t_seq_component, bit 3: cc_locked, bits 4..7: cc_lock_high (or cc_lock_val >> 3) */
    uint8_t cc_val;       /* 0..127 parameter-locked MIDI CC value (0xFF = unlocked) */
} t_seq_step;

#define STEP_FLAG_ACTIVE 0x01
#define STEP_FLAG_TIE    0x02

/* Per-Track Configuration & Runtime State */
typedef struct {
    uint8_t midi_channel;     /* 0..15 (MIDI Ch 1..16) */
    uint16_t length;          /* 1..256 steps (default 64 or 16) */
    uint8_t clock_scale;      /* t_seq_clock_scale */
    uint8_t direction;        /* t_seq_direction */
    uint8_t base_note;        /* Default note for drum/trigger tracks (e.g. 36 Kick, 60 C4) */
    uint8_t cc_number;        /* Assigned MIDI CC number for per-step P-locks & knob (default 74 Cutoff) */
    uint8_t cc_current;       /* Last sent CC value 0..127 */

    /* Generative Parameters per Track */
    uint8_t euclid_pulses;    /* Euclidean K (0..64) */
    uint8_t euclid_steps;     /* Euclidean N (1..64) */
    uint8_t euclid_offset;    /* Euclidean rotation (0..63) */
    uint8_t turing_prob;      /* 0..100%: 0=locked loop, 1..50=melodic drift, 51..100=Aphex chaos */
    uint8_t octave_spread;    /* 0..3 octaves range for generative notes */
    uint8_t ratchet_prob;     /* 0..100% probability of spontaneous Aphex ratchets */
    bool muted;

    /* Runtime Playback State (not persisted needed, reset on stop) */
    uint16_t play_step;       /* Current step 0..255 */
    uint16_t tick_counter;    /* Sub-step tick counter */
    uint16_t loop_count;      /* Number of completed loops (for 1:2, 1:4 conditions & ramps) */
    int8_t pendulum_dir;      /* +1 or -1 */
    bool last_trig_fired;     /* For COND_PRE */
    uint16_t shift_reg;       /* 16-bit Turing Machine shift register */

    /* Active Note-Off Queue (supports polyphony / chords per track) */
    uint8_t active_notes[4];
    uint16_t note_off_ticks[4];

    /* Active Ratchet Burst State */
    uint8_t ratchet_remaining;
    uint8_t ratchet_SubTicks;
    uint8_t ratchet_counter;
    uint8_t ratchet_note;
    uint8_t ratchet_vel;
    uint8_t ratchet_gate;
    uint8_t ratchet_mode;

    t_seq_step steps[SEQ_MAX_STEPS];
} t_seq_track;

/* Complete Sequencer Brain State (Also serves as telemetry struct for USB/e-Ink display) */
typedef struct {
    bool playing;
    bool recording;
    bool fill_active;         /* Momentary or latched FILL (via Tap button) */
    bool shift_held;
    int8_t held_step_pad;     /* -1 if none, 0..15 when holding a pad for Elektron P-Lock */
    bool plock_modified;      /* True if a knob was turned while holding a step pad */

    uint16_t bpm_tenths;      /* e.g. 1200 = 120.0 BPM (range 30.0 .. 300.0) */
    uint8_t swing;            /* 50..75 % */
    uint8_t root_note;        /* 0..11 (C..B) */
    uint8_t scale_id;         /* t_seq_scale_id */

    uint8_t current_track;    /* 0..15 */
    uint8_t current_bar;      /* 0..15 (16 bars of 16 steps = 256 steps) */
    uint8_t bar_bank;         /* 0..3 (Bank 0=Bars 1-4, Bank 1=Bars 5-8, Bank 2=Bars 9-12, Bank 3=Bars 13-16) */
    bool follow_bars;         /* Auto-follow playhead across bars */
    t_ui_mode ui_mode;
    int8_t kbd_octave;        /* Keyboard octave offset (-2..+3) */

    /* X/Y Kaoss Pad Performance State */
    bool xy_active;
    uint8_t xy_x;             /* 0..127 */
    uint8_t xy_y;             /* 0..127 */
    uint8_t xy_mode;          /* 0=Aphex Ratchet/Pitch Chaos, 1=Ambient Cloud/CC, 2=Turing Mutator */

    /* Last Edited Parameter Banner for OLED / e-Ink */
    char status_msg[24];
    uint16_t status_timer;

    uint32_t global_tick;
    uint32_t rng_state;

    t_seq_track tracks[SEQ_NUM_TRACKS];
} t_seq_brain;

extern t_seq_brain g_brain;

/* Engine API */
void seq_engine_init(void);
void seq_engine_tick_1ms(void);
void seq_engine_start(void);
void seq_engine_stop(void);
void seq_engine_toggle_play(void);

/* Editing & Generative Functions */
uint16_t seq_get_focused_step_index(void);
void seq_toggle_step_pad(uint8_t pad);
void seq_trigger_live_pad(uint8_t pad, uint8_t velocity, bool pressed);
void seq_apply_euclidean(uint8_t track_idx, uint8_t pulses, uint8_t steps, uint8_t offset);
void seq_generate_aphex_track(uint8_t track_idx);
void seq_generate_ambient_track(uint8_t track_idx);
void seq_clear_track(uint8_t track_idx);
uint8_t seq_quantize_note(uint8_t note, uint8_t root, uint8_t scale_id);
void seq_set_status(const char *label, int32_t val);
void seq_set_status_str(const char *label, const char *val);

/* Flash Persistence (via SPI1 Flash in Freetribe) */
void seq_save_to_flash(uint8_t slot);
bool seq_load_from_flash(uint8_t slot);

/* Helper Names for Display */
const char *seq_scale_name(uint8_t scale_id);
const char *seq_dir_name(uint8_t dir);
const char *seq_clk_name(uint8_t clk);
const char *seq_cond_name(uint8_t cond);
const char *seq_comp_name(uint8_t comp);
const char *seq_chord_name(uint8_t chord);
void seq_note_name(uint8_t note, char *buf);

#ifdef __cplusplus
}
#endif

#endif /* SEQ_ENGINE_H */
