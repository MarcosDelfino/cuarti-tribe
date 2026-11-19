/*----------------------------------------------------------------------
 * CuartiTribe Brain - Sequencer Engine Implementation
 *----------------------------------------------------------------------*/

#include "seq_engine.h"
#include "freetribe.h"
#include "dev_flash.h"
#include <stdio.h>
#include <string.h>

#ifdef __arm__
t_seq_brain g_brain __attribute__((section(".ddr_bss")));
#else
t_seq_brain g_brain;
#endif

/* 96 PPQN fractional accumulator for 1ms systick */
static uint32_t g_tick_accum_us = 0;

/* Flash storage base address in upper user area (safe above 1.5MB) */
#define BRAIN_FLASH_BASE 0x00180000
#define BRAIN_FLASH_MAGIC 0x43544231 /* "CTB1" */

static const uint16_t k_scale_masks[SCALE_COUNT] = {
    0x0FFF, /* CHROMATIC: 1 1 1 1 1 1 1 1 1 1 1 1 */
    0x05AD, /* MINOR:     C D D# F G G# A# */
    0x0AB5, /* MAJOR:     C D E F G A B */
    0x06AD, /* DORIAN:    C D D# F G A A# */
    0x05AB, /* PHRYGIAN:  C C# D# F G G# A# */
    0x0AD5, /* LYDIAN:    C D E F# G A B */
    0x04A9, /* PENTA MIN: C D# F G A# */
    0x0195, /* HIRAJOSHI: C D D# G G# (Japanese Ambient) */
    0x099B, /* BHAIRAV:   C C# E F G G# B */
    0x0555  /* WHOLETONE: C D E F# G# A# */
};

static const uint8_t k_ratchet_counts[6] = {1, 2, 3, 4, 6, 8};

/* Ticks per step for each clock scale (base = 24 ticks per 1/16th step) */
static const uint16_t k_clock_step_ticks[CLK_SCALE_COUNT] = {
    384, /* CLK_DIV_16: 1 bar per step */
    192, /* CLK_DIV_8:  1/2 note */
    96,  /* CLK_DIV_4:  1/4 note */
    48,  /* CLK_DIV_2:  1/8 note */
    24,  /* CLK_1X:     1/16 note */
    12,  /* CLK_2X:     1/32 note */
    8,   /* CLK_3X:     1/24 triplet */
    6    /* CLK_4X:     1/64 note (Aphex drill) */
};

static const int8_t k_chord_intervals[CHORD_TYPE_COUNT][4] = {
    {0, -1, -1, -1}, /* CHORD_NONE */
    {0, 7, -1, -1},  /* CHORD_FIFTH */
    {0, 3, 7, -1},   /* CHORD_MIN */
    {0, 4, 7, -1},   /* CHORD_MAJ */
    {0, 5, 7, -1},   /* CHORD_SUS4 */
    {0, 3, 7, 10},   /* CHORD_MIN7 */
    {0, 4, 7, 11},   /* CHORD_MAJ7 */
    {0, 12, -1, -1}  /* CHORD_OCT */
};

/* Default base notes for 16 tracks (Drums on 1..8, Melodic/Synths on 9..16) */
static const uint8_t k_default_base_notes[SEQ_NUM_TRACKS] = {
    36, 38, 42, 46, 39, 45, 48, 50,
    48, 60, 60, 64, 67, 72, 60, 48
};

static uint32_t _rand32(void) {
    uint32_t x = g_brain.rng_state;
    if (x == 0) {
        x = 0xA5A5F00DU;
    }
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g_brain.rng_state = x;
    return x;
}

static uint8_t _rand_range(uint8_t min_v, uint8_t max_v) {
    if (max_v <= min_v) {
        return min_v;
    }
    return (uint8_t)(min_v + (_rand32() % (uint32_t)(max_v - min_v + 1)));
}

const char *seq_scale_name(uint8_t scale_id) {
    static const char *names[SCALE_COUNT] = {
        "CHROM", "MINOR", "MAJOR", "DORIAN", "PHRYG",
        "LYDIAN", "PENTA", "HIRAJ", "BHAIR", "WHOLE"
    };
    return names[scale_id % SCALE_COUNT];
}

const char *seq_dir_name(uint8_t dir) {
    static const char *names[DIR_COUNT] = {
        "FWD->", "<-REV", "PEND<>", "DRNK?~", "RND??"
    };
    return names[dir % DIR_COUNT];
}

const char *seq_clk_name(uint8_t clk) {
    static const char *names[CLK_SCALE_COUNT] = {
        "/16", "/8", "/4", "/2", "1x", "2x", "3x", "4x"
    };
    return names[clk % CLK_SCALE_COUNT];
}

const char *seq_cond_name(uint8_t cond) {
    static const char *names[COND_COUNT] = {
        "ALW", "1:2", "2:2", "1:3", "2:3", "3:3",
        "1:4", "2:4", "3:4", "4:4", "FILL", "!FIL", "1ST", "PRE"
    };
    return names[cond % COND_COUNT];
}

const char *seq_comp_name(uint8_t comp) {
    static const char *names[COMP_COUNT] = {
        "---", "OCT+", "OCT+-", "+5TH", "RMP+", "RMP-", "VFAD", "GLCH"
    };
    return names[comp % COMP_COUNT];
}

const char *seq_chord_name(uint8_t chord) {
    static const char *names[CHORD_TYPE_COUNT] = {
        "1NT", "5TH", "MIN", "MAJ", "SUS4", "m7", "M7", "OCT"
    };
    return names[chord % CHORD_TYPE_COUNT];
}

void seq_note_name(uint8_t note, char *buf) {
    static const char *n_str[12] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };
    int oct = (int)(note / 12) - 1;
    snprintf(buf, 6, "%s%d", n_str[note % 12], oct);
}

void seq_set_status(const char *label, int32_t val) {
    snprintf(g_brain.status_msg, sizeof(g_brain.status_msg), "%s: %ld", label,
             (long)val);
    g_brain.status_timer = 1400; /* ~1.4s display overlay */
}

void seq_set_status_str(const char *label, const char *val) {
    snprintf(g_brain.status_msg, sizeof(g_brain.status_msg), "%s: %s", label,
             val);
    g_brain.status_timer = 1400;
}

uint8_t seq_quantize_note(uint8_t note, uint8_t root, uint8_t scale_id) {
    if (note > 127) {
        note = 127;
    }
    uint16_t mask = k_scale_masks[scale_id % SCALE_COUNT];
    int rel = ((int)note - (int)(root % 12) + 120) % 12;
    if (mask & (1 << rel)) {
        return note;
    }
    /* Search nearest scale degree */
    for (int d = 1; d <= 6; d++) {
        int down = (rel - d + 12) % 12;
        if (mask & (1 << down)) {
            return (note >= d) ? (uint8_t)(note - d) : note;
        }
        int up = (rel + d) % 12;
        if (mask & (1 << up)) {
            return (note + d <= 127) ? (uint8_t)(note + d) : note;
        }
    }
    return note;
}

static void _stop_track_notes(uint8_t t_idx) {
    t_seq_track *trk = &g_brain.tracks[t_idx];
    for (int i = 0; i < 4; i++) {
        if (trk->note_off_ticks[i] > 0) {
            ft_send_note_off((char)trk->midi_channel,
                             (char)trk->active_notes[i], 0);
            trk->note_off_ticks[i] = 0;
        }
    }
    trk->ratchet_remaining = 0;
}

static void _trigger_track_note(uint8_t t_idx, uint8_t root_note, uint8_t vel,
                                uint16_t gate_ticks, uint8_t chord_type) {
    t_seq_track *trk = &g_brain.tracks[t_idx];

    /* Stop any lingering notes on this track before re-triggering */
    for (int i = 0; i < 4; i++) {
        if (trk->note_off_ticks[i] > 0) {
            ft_send_note_off((char)trk->midi_channel,
                             (char)trk->active_notes[i], 0);
            trk->note_off_ticks[i] = 0;
        }
    }

    if (vel < 1) {
        vel = 1;
    }
    if (vel > 127) {
        vel = 127;
    }
    if (gate_ticks < 1) {
        gate_ticks = 1;
    }

    chord_type %= CHORD_TYPE_COUNT;
    for (int i = 0; i < 4; i++) {
        int8_t interval = k_chord_intervals[chord_type][i];
        if (interval < 0) {
            break;
        }
        uint16_t n = (uint16_t)root_note + (uint16_t)interval;
        if (n > 127) {
            n = 127;
        }
        if (i > 0 && g_brain.scale_id != SCALE_CHROMATIC) {
            n = seq_quantize_note((uint8_t)n, g_brain.root_note,
                                  g_brain.scale_id);
        }
        trk->active_notes[i] = (uint8_t)n;
        trk->note_off_ticks[i] = gate_ticks;
        ft_send_note_on((char)trk->midi_channel, (char)n, (char)vel);
    }
}

void seq_clear_track(uint8_t track_idx) {
    if (track_idx >= SEQ_NUM_TRACKS) {
        return;
    }
    t_seq_track *trk = &g_brain.tracks[track_idx];
    _stop_track_notes(track_idx);
    uint8_t def_note = k_default_base_notes[track_idx];
    for (uint16_t s = 0; s < SEQ_MAX_STEPS; s++) {
        trk->steps[s].flags = 0;
        trk->steps[s].note = def_note;
        trk->steps[s].velocity = 100;
        trk->steps[s].gate = 12;
        trk->steps[s].microtiming = 0;
        trk->steps[s].probability = 100;
        trk->steps[s].cond_ratchet = 0; /* COND_ALWAYS, 1x ratchet */
        trk->steps[s].comp_cclock = 0;  /* COMP_NONE */
        trk->steps[s].cc_val = 0xFF;    /* No CC lock */
    }
}

/* Euclidean Rhythm Generator (Bjorklund distribution across track length) */
void seq_apply_euclidean(uint8_t track_idx, uint8_t pulses, uint8_t steps,
                         uint8_t offset) {
    if (track_idx >= SEQ_NUM_TRACKS || steps == 0) {
        return;
    }
    t_seq_track *trk = &g_brain.tracks[track_idx];
    if (pulses > steps) {
        pulses = steps;
    }
    trk->euclid_pulses = pulses;
    trk->euclid_steps = steps;
    trk->euclid_offset = offset;

    for (uint16_t s = 0; s < trk->length; s++) {
        uint16_t pos = (s + steps - (offset % steps)) % steps;
        bool hit = (((uint32_t)pos * (uint32_t)pulses) % (uint32_t)steps) <
                   (uint32_t)pulses;
        if (pulses == 0) {
            hit = false;
        }
        if (hit) {
            trk->steps[s].flags |= STEP_FLAG_ACTIVE;
        } else {
            trk->steps[s].flags &= ~STEP_FLAG_ACTIVE;
        }
    }
}

/* One-button Aphex Twin IDM/Drill Generator */
void seq_generate_aphex_track(uint8_t track_idx) {
    if (track_idx >= SEQ_NUM_TRACKS) {
        return;
    }
    t_seq_track *trk = &g_brain.tracks[track_idx];
    bool is_melodic = (track_idx >= 8);

    for (uint16_t s = 0; s < trk->length; s++) {
        t_seq_step *st = &trk->steps[s];
        bool downbeat = (s % 4) == 0;
        uint8_t chance = downbeat ? 65 : 42;
        if (_rand_range(0, 99) < chance) {
            st->flags = STEP_FLAG_ACTIVE;
            st->velocity = _rand_range(65, 127);
            st->gate = _rand_range(4, 20);
            st->microtiming = (_rand_range(0, 99) < 30)
                                  ? (int8_t)_rand_range(0, 10) - 5
                                  : 0;
            st->probability = (_rand_range(0, 99) < 35) ? _rand_range(45, 90) : 100;

            /* Inject Aphex ratchets (2x, 3x, 4x, 6x, 8x) on ~28% of active steps */
            uint8_t r_idx = 0;
            if (_rand_range(0, 99) < 28) {
                r_idx = _rand_range(1, 5);
                /* Set velocity ramp mode in bits 5..6 of flags */
                st->flags |= (uint8_t)((_rand_range(1, 2) & 0x03) << 5);
            }
            /* Inject Elektron Trig Conditions on ~25% of steps */
            uint8_t cond = COND_ALWAYS;
            if (_rand_range(0, 99) < 25) {
                cond = _rand_range(COND_1_2, COND_4_4);
            }
            st->cond_ratchet = (uint8_t)((cond << 4) | (r_idx & 0x0F));

            if (is_melodic) {
                int8_t oct = (int8_t)_rand_range(0, 2) - 1;
                uint8_t raw = (uint8_t)((int)trk->base_note + oct * 12 +
                                        (int)_rand_range(0, 11));
                st->note = seq_quantize_note(raw, g_brain.root_note,
                                             g_brain.scale_id);
                st->comp_cclock = (_rand_range(0, 99) < 30)
                                      ? _rand_range(COMP_OCT_UP, COMP_GLITCH_RND)
                                      : COMP_NONE;
            } else {
                st->note = trk->base_note;
                st->comp_cclock = (_rand_range(0, 99) < 20) ? COMP_GLITCH_RND
                                                            : COMP_NONE;
            }
            /* Random CC P-Lock on 30% of steps */
            if (_rand_range(0, 99) < 30) {
                st->cc_val = _rand_range(20, 125);
            } else {
                st->cc_val = 0xFF;
            }
        } else {
            st->flags = 0;
        }
    }
    seq_set_status_str("GEN", "APHEX IDM");
}

/* One-button Ambient Evolving Generator */
void seq_generate_ambient_track(uint8_t track_idx) {
    if (track_idx >= SEQ_NUM_TRACKS) {
        return;
    }
    t_seq_track *trk = &g_brain.tracks[track_idx];

    /* Ambient defaults: longer steps, slow clock or pendulum/drunk walk */
    if (trk->length < 64) {
        trk->length = 128;
    }
    if (trk->clock_scale == CLK_1X) {
        trk->clock_scale = CLK_DIV_2; /* Half-speed 1/8th or slower */
    }
    trk->turing_prob = 18; /* Gentle Marbles-style melodic drift */

    for (uint16_t s = 0; s < trk->length; s++) {
        t_seq_step *st = &trk->steps[s];
        /* Sparse, breathing note placement */
        bool phrase_point = (s % 8 == 0) || (s % 16 == 6) || (s % 16 == 11);
        uint8_t chance = phrase_point ? 60 : 14;
        if (_rand_range(0, 99) < chance) {
            uint8_t chord = _rand_range(CHORD_FIFTH, CHORD_MAJ7);
            st->flags = STEP_FLAG_ACTIVE | (uint8_t)((chord & 0x07) << 2);
            uint8_t raw = (uint8_t)((int)trk->base_note - 12 +
                                    (int)_rand_range(0, 24));
            st->note = seq_quantize_note(raw, g_brain.root_note,
                                         g_brain.scale_id);
            st->velocity = _rand_range(45, 92);
            st->gate = _rand_range(48, 96); /* Long overlapping ambient gates */
            st->microtiming = 0;
            st->probability = _rand_range(65, 100);
            uint8_t cond = (_rand_range(0, 99) < 40)
                               ? _rand_range(COND_1_2, COND_3_4)
                               : COND_ALWAYS;
            st->cond_ratchet = (uint8_t)(cond << 4);
            st->comp_cclock = (_rand_range(0, 99) < 25) ? COMP_OCT_UP : COMP_NONE;
            st->cc_val = _rand_range(35, 105);
        } else {
            st->flags = 0;
        }
    }
    seq_set_status_str("GEN", "AMBIENT");
}

void seq_engine_init(void) {
    memset(&g_brain, 0, sizeof(g_brain));
    g_brain.bpm_tenths = 1280; /* 128.0 BPM */
    g_brain.swing = 50;
    g_brain.root_note = 0; /* C */
    g_brain.scale_id = SCALE_HIRAJOSHI;
    g_brain.current_track = 9; /* Start on Melodic Track 10 */
    g_brain.current_bar = 0;
    g_brain.bar_bank = 0;
    g_brain.follow_bars = false;
    g_brain.ui_mode = MODE_SEQUENCER;
    g_brain.held_step_pad = -1;
    g_brain.kbd_octave = 0;
    g_brain.rng_state = 0x1337BEEFU;

    for (uint8_t t = 0; t < SEQ_NUM_TRACKS; t++) {
        t_seq_track *trk = &g_brain.tracks[t];
        trk->midi_channel = t;
        trk->length = (t >= 8) ? 128 : 64; /* 128 steps on synth tracks, 64 on drums */
        trk->clock_scale = CLK_1X;
        trk->direction = DIR_FORWARD;
        trk->base_note = k_default_base_notes[t];
        trk->cc_number = 74; /* Filter Cutoff CC74 */
        trk->cc_current = 64;
        trk->euclid_steps = 16;
        trk->euclid_pulses = 0;
        trk->euclid_offset = 0;
        trk->turing_prob = 0;
        trk->octave_spread = 1;
        trk->ratchet_prob = 0;
        trk->muted = false;
        trk->pendulum_dir = 1;
        trk->shift_reg = (uint16_t)(0xACE1U + t * 0x103U);
        seq_clear_track(t);
    }

    /* Seed Track 1 (Kick), Track 3 (HiHat), Track 9 (Ambient Pad), Track 10 (Aphex Lead) */
    seq_apply_euclidean(0, 5, 16, 0);  /* Polymetric 5/16 Kick */
    seq_apply_euclidean(2, 11, 16, 2); /* 11/16 Hi-Hat with ratchets */
    g_brain.tracks[2].steps[3].cond_ratchet = (COND_ALWAYS << 4) | 3; /* 4x ratchet */
    g_brain.tracks[2].steps[11].cond_ratchet = (COND_1_2 << 4) | 5;   /* 8x ratchet every 2nd loop */

    g_brain.tracks[8].length = 128;
    seq_generate_ambient_track(8);

    g_brain.tracks[9].length = 128;
    seq_generate_aphex_track(9);

    seq_set_status_str("BRAIN", "256 STEPS");
}

void seq_engine_start(void) {
    g_brain.playing = true;
    g_brain.global_tick = 0;
    g_tick_accum_us = 0;
    for (uint8_t t = 0; t < SEQ_NUM_TRACKS; t++) {
        g_brain.tracks[t].play_step = 0;
        g_brain.tracks[t].tick_counter = 0;
        g_brain.tracks[t].loop_count = 0;
        g_brain.tracks[t].pendulum_dir = 1;
    }
    svc_midi_send_byte(0xFA); /* MIDI Realtime START */
    seq_set_status_str("SEQ", "PLAY");
}

void seq_engine_stop(void) {
    g_brain.playing = false;
    svc_midi_send_byte(0xFC); /* MIDI Realtime STOP */
    for (uint8_t t = 0; t < SEQ_NUM_TRACKS; t++) {
        _stop_track_notes(t);
    }
    seq_set_status_str("SEQ", "STOP");
}

void seq_engine_toggle_play(void) {
    if (g_brain.playing) {
        seq_engine_stop();
    } else {
        seq_engine_start();
    }
}

uint16_t seq_get_focused_step_index(void) {
    if (g_brain.held_step_pad >= 0 && g_brain.held_step_pad < 16) {
        return (uint16_t)(g_brain.current_bar * 16 + g_brain.held_step_pad);
    }
    return (uint16_t)(g_brain.current_bar * 16);
}

void seq_toggle_step_pad(uint8_t pad) {
    if (pad >= 16) {
        return;
    }
    t_seq_track *trk = &g_brain.tracks[g_brain.current_track];
    uint16_t step_idx = (uint16_t)(g_brain.current_bar * 16 + pad);
    if (step_idx >= SEQ_MAX_STEPS) {
        return;
    }
    trk->steps[step_idx].flags ^= STEP_FLAG_ACTIVE;
}

void seq_trigger_live_pad(uint8_t pad, uint8_t velocity, bool pressed) {
    if (pad >= 16) {
        return;
    }
    if (g_brain.ui_mode == MODE_TRIGGER) {
        g_brain.current_track = pad;
        t_seq_track *trk = &g_brain.tracks[pad];
        if (pressed) {
            ft_send_note_on((char)trk->midi_channel, (char)trk->base_note,
                            (char)(velocity > 0 ? velocity : 100));
            seq_set_status("TRACK", pad + 1);
        } else {
            ft_send_note_off((char)trk->midi_channel, (char)trk->base_note, 0);
        }
    } else if (g_brain.ui_mode == MODE_KEYBOARD) {
        t_seq_track *trk = &g_brain.tracks[g_brain.current_track];
        int raw_note = (int)trk->base_note + (int)g_brain.kbd_octave * 12 + (int)pad;
        if (raw_note < 0) {
            raw_note = 0;
        }
        if (raw_note > 127) {
            raw_note = 127;
        }
        uint8_t q_note = seq_quantize_note((uint8_t)raw_note, g_brain.root_note,
                                           g_brain.scale_id);
        if (pressed) {
            ft_send_note_on((char)trk->midi_channel, (char)q_note,
                            (char)(velocity > 0 ? velocity : 100));
            /* If holding or recording, write note into current step */
            if (g_brain.recording && g_brain.playing) {
                uint16_t s = trk->play_step;
                trk->steps[s].flags |= STEP_FLAG_ACTIVE;
                trk->steps[s].note = q_note;
                trk->steps[s].velocity = velocity > 0 ? velocity : 100;
            }
            char nbuf[8];
            seq_note_name(q_note, nbuf);
            seq_set_status_str("NOTE", nbuf);
        } else {
            ft_send_note_off((char)trk->midi_channel, (char)q_note, 0);
        }
    }
}

static bool _check_trig_condition(t_seq_track *trk, uint8_t cond, uint8_t prob) {
    /* Probability check first */
    if (prob < 100 && _rand_range(1, 100) > prob) {
        return false;
    }
    uint16_t loop = trk->loop_count;
    switch (cond % COND_COUNT) {
    case COND_ALWAYS:
        return true;
    case COND_1_2:
        return (loop % 2) == 0;
    case COND_2_2:
        return (loop % 2) == 1;
    case COND_1_3:
        return (loop % 3) == 0;
    case COND_2_3:
        return (loop % 3) == 1;
    case COND_3_3:
        return (loop % 3) == 2;
    case COND_1_4:
        return (loop % 4) == 0;
    case COND_2_4:
        return (loop % 4) == 1;
    case COND_3_4:
        return (loop % 4) == 2;
    case COND_4_4:
        return (loop % 4) == 3;
    case COND_FILL:
        return g_brain.fill_active;
    case COND_NOT_FILL:
        return !g_brain.fill_active;
    case COND_FIRST:
        return loop == 0;
    case COND_PRE:
        return trk->last_trig_fired;
    default:
        return true;
    }
}

static void _advance_track_step(t_seq_track *trk) {
    uint16_t len = trk->length;
    if (len < 1) {
        len = 1;
    }
    if (len > SEQ_MAX_STEPS) {
        len = SEQ_MAX_STEPS;
    }

    switch (trk->direction % DIR_COUNT) {
    case DIR_FORWARD:
        trk->play_step++;
        if (trk->play_step >= len) {
            trk->play_step = 0;
            trk->loop_count++;
        }
        break;

    case DIR_REVERSE:
        if (trk->play_step == 0) {
            trk->play_step = len - 1;
            trk->loop_count++;
        } else {
            trk->play_step--;
        }
        break;

    case DIR_PENDULUM:
        if (len <= 1) {
            trk->play_step = 0;
        } else if (trk->pendulum_dir > 0) {
            if (trk->play_step + 1 >= len) {
                trk->pendulum_dir = -1;
                trk->play_step--;
                trk->loop_count++;
            } else {
                trk->play_step++;
            }
        } else {
            if (trk->play_step == 0) {
                trk->pendulum_dir = 1;
                trk->play_step++;
                trk->loop_count++;
            } else {
                trk->play_step--;
            }
        }
        break;

    case DIR_DRUNK: {
        uint8_t r = _rand_range(0, 3);
        if (r <= 1) {
            /* 50% step forward */
            trk->play_step = (trk->play_step + 1) % len;
            if (trk->play_step == 0) {
                trk->loop_count++;
            }
        } else if (r == 2) {
            /* 25% step backward */
            trk->play_step = (trk->play_step == 0) ? (len - 1)
                                                   : (trk->play_step - 1);
        }
        /* 25% stay on same step */
        break;
    }

    case DIR_RANDOM:
        trk->play_step = (uint16_t)(_rand32() % len);
        break;
    }
}

static void _process_step_trigger(uint8_t t_idx) {
    t_seq_track *trk = &g_brain.tracks[t_idx];
    uint16_t s_idx = trk->play_step;
    t_seq_step *st = &trk->steps[s_idx];

    /* Turing Machine / Marbles live mutation */
    if (trk->turing_prob > 0) {
        uint16_t bit = (trk->shift_reg & 1U);
        if (_rand_range(1, 100) <= trk->turing_prob) {
            bit ^= 1U;
            if (st->flags & STEP_FLAG_ACTIVE) {
                int delta = ((int)(_rand32() % 7) - 3) * 2;
                int new_n = (int)st->note + delta;
                if (new_n < 24) {
                    new_n = 24;
                }
                if (new_n > 108) {
                    new_n = 108;
                }
                st->note = seq_quantize_note((uint8_t)new_n, g_brain.root_note,
                                             g_brain.scale_id);
            }
        }
        trk->shift_reg = (trk->shift_reg >> 1) | (bit << 15);
        if (trk->turing_prob > 55 && _rand_range(1, 100) <= (trk->turing_prob - 50)) {
            st->flags ^= STEP_FLAG_ACTIVE;
        }
    }

    if (trk->muted || !(st->flags & STEP_FLAG_ACTIVE)) {
        trk->last_trig_fired = false;
        return;
    }

    uint8_t cond = (st->cond_ratchet >> 4) & 0x0F;
    if (!_check_trig_condition(trk, cond, st->probability)) {
        trk->last_trig_fired = false;
        return;
    }
    trk->last_trig_fired = true;

    /* Send Parameter-Locked MIDI CC if present */
    if (st->cc_val <= 127) {
        ft_send_cc((char)trk->midi_channel, (char)trk->cc_number,
                   (char)st->cc_val);
        trk->cc_current = st->cc_val;
    }

    /* Compute Note & Velocity with OP-1/OP-Z Step Components */
    int note = (int)st->note;
    int vel = (int)st->velocity;
    uint8_t comp = st->comp_cclock & 0x07;
    uint8_t r_idx = st->cond_ratchet & 0x0F;

    switch (comp) {
    case COMP_OCT_UP:
        if (_rand_range(0, 1) == 1) {
            note += 12;
        }
        break;
    case COMP_OCT_BIPOL:
        note += ((int)_rand_range(0, 2) - 1) * 12;
        break;
    case COMP_FIFTH:
        if (_rand_range(0, 1) == 1) {
            note += 7;
        }
        break;
    case COMP_RAMP_UP:
        note += (int)((trk->loop_count % 4) * 2);
        break;
    case COMP_RAMP_DOWN:
        note -= (int)((trk->loop_count % 4) * 2);
        break;
    case COMP_VEL_FADE:
        vel = vel - (int)((trk->loop_count % 4) * 20);
        break;
    case COMP_GLITCH_RND:
        r_idx = _rand_range(0, 5);
        break;
    default:
        break;
    }

    /* Spontaneous track ratchet probability or live XY Pad Chaos */
    if (trk->ratchet_prob > 0 && _rand_range(1, 100) <= trk->ratchet_prob) {
        r_idx = _rand_range(1, 5);
    }
    if (g_brain.xy_active && t_idx == g_brain.current_track &&
        g_brain.xy_mode == 0) {
        r_idx = (uint8_t)((g_brain.xy_x * 5) / 127);
        note += ((int)(g_brain.xy_y / 16) - 4) * 2;
    }

    if (note < 0) {
        note = 0;
    }
    if (note > 127) {
        note = 127;
    }
    if (g_brain.scale_id != SCALE_CHROMATIC && t_idx >= 8) {
        note = seq_quantize_note((uint8_t)note, g_brain.root_note,
                                 g_brain.scale_id);
    }
    if (vel < 10) {
        vel = 10;
    }
    if (vel > 127) {
        vel = 127;
    }

    uint8_t r_count = k_ratchet_counts[r_idx % 6];
    uint8_t chord_type = (st->flags >> 2) & 0x07;
    uint16_t step_ticks = k_clock_step_ticks[trk->clock_scale % CLK_SCALE_COUNT];

    if (r_count <= 1) {
        uint16_t gate_ticks = st->gate;
        if (st->flags & STEP_FLAG_TIE) {
            gate_ticks = step_ticks + 2;
        }
        _trigger_track_note(t_idx, (uint8_t)note, (uint8_t)vel, gate_ticks,
                            chord_type);
    } else {
        /* Start Aphex sub-step ratchet burst */
        uint8_t sub_ticks = (uint8_t)(step_ticks / r_count);
        if (sub_ticks < 2) {
            sub_ticks = 2;
        }
        uint8_t sub_gate = (uint8_t)(sub_ticks > 2 ? (sub_ticks - 1) : 1);
        _trigger_track_note(t_idx, (uint8_t)note, (uint8_t)vel, sub_gate,
                            chord_type);
        trk->ratchet_remaining = (uint8_t)(r_count - 1);
        trk->ratchet_SubTicks = sub_ticks;
        trk->ratchet_counter = sub_ticks;
        trk->ratchet_note = (uint8_t)note;
        trk->ratchet_vel = (uint8_t)vel;
        trk->ratchet_gate = sub_gate;
        trk->ratchet_mode = (st->flags >> 5) & 0x03;
    }
}

static void _seq_tick_96ppqn(void) {
    /* Send standard 24 PPQN MIDI Clock every 4 internal ticks */
    if ((g_brain.global_tick % 4) == 0) {
        svc_midi_send_byte(0xF8);
    }

    for (uint8_t t = 0; t < SEQ_NUM_TRACKS; t++) {
        t_seq_track *trk = &g_brain.tracks[t];

        /* 1. Decrement active Note-Off timers */
        for (int i = 0; i < 4; i++) {
            if (trk->note_off_ticks[i] > 0) {
                trk->note_off_ticks[i]--;
                if (trk->note_off_ticks[i] == 0) {
                    ft_send_note_off((char)trk->midi_channel,
                                     (char)trk->active_notes[i], 0);
                }
            }
        }

        /* 2. Process active sub-step Ratchet bursts */
        if (trk->ratchet_remaining > 0) {
            if (trk->ratchet_counter > 0) {
                trk->ratchet_counter--;
            }
            if (trk->ratchet_counter == 0) {
                /* Adjust ratchet velocity according to ramp mode */
                if (trk->ratchet_mode == 1 && trk->ratchet_vel > 22) {
                    trk->ratchet_vel -= 15; /* Decay burst */
                } else if (trk->ratchet_mode == 2 && trk->ratchet_vel < 112) {
                    trk->ratchet_vel += 15; /* Crescendo burst */
                }
                _trigger_track_note(t, trk->ratchet_note, trk->ratchet_vel,
                                    trk->ratchet_gate, CHORD_NONE);
                trk->ratchet_remaining--;
                trk->ratchet_counter = trk->ratchet_SubTicks;
            }
        }

        /* 3. Advance step clock per track (supports independent clock scales) */
        uint16_t step_ticks =
            k_clock_step_ticks[trk->clock_scale % CLK_SCALE_COUNT];
        if (trk->tick_counter == 0) {
            _process_step_trigger(t);
        }

        trk->tick_counter++;
        if (trk->tick_counter >= step_ticks) {
            trk->tick_counter = 0;
            _advance_track_step(trk);
        }
    }

    /* Follow playhead across bars if enabled */
    if (g_brain.follow_bars && g_brain.held_step_pad < 0) {
        uint8_t play_bar =
            (uint8_t)(g_brain.tracks[g_brain.current_track].play_step / 16);
        g_brain.current_bar = play_bar;
        g_brain.bar_bank = play_bar / 4;
    }

    g_brain.global_tick++;
}

void seq_engine_tick_1ms(void) {
    if (g_brain.status_timer > 0) {
        g_brain.status_timer--;
    }

    if (!g_brain.playing) {
        return;
    }

    /*
     * 96 PPQN:
     * ticks_per_sec = (BPM * 96) / 60 = (bpm_tenths * 96) / 600
     * us_per_tick = 600,000,000 / (bpm_tenths * 96) = 6,250,000 / bpm_tenths
     */
    uint32_t us_per_tick = 6250000U / (uint32_t)g_brain.bpm_tenths;
    if (us_per_tick < 200) {
        us_per_tick = 200;
    }
    g_tick_accum_us += 1000U;
    while (g_tick_accum_us >= us_per_tick) {
        g_tick_accum_us -= us_per_tick;
        _seq_tick_96ppqn();
    }
}

void seq_save_to_flash(uint8_t slot) {
    (void)slot;
    /* Disabled in RAM-only mode for 100% hardware safety */
    seq_set_status_str("RAM MODE", "SAFE NO-FLASH");
}

bool seq_load_from_flash(uint8_t slot) {
    (void)slot;
    seq_set_status_str("RAM MODE", "SAFE NO-FLASH");
    return false;
}
