/*----------------------------------------------------------------------
 * CuartiTribe Brain - Panel Controls, LEDs, and 128x64 OLED UI
 *----------------------------------------------------------------------*/

#include "freetribe.h"
#include "seq_engine.h"
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
#define BTN_AMP_EG       0x20
#define BTN_LEFT         0x1e
#define BTN_RIGHT        0x1f
#define BTN_PART_MUTE    0x21
#define BTN_PART_ERASE   0x22
#define BTN_TRIGGER      0x23
#define BTN_SEQUENCER    0x24
#define BTN_KEYBOARD     0x25
#define BTN_CHORD        0x26
#define BTN_STEP_JUMP    0x27
#define BTN_PATTERN_SET  0x28

/* Electribe 2 Knob IDs */
#define KNOB_LEVEL       0x00
#define KNOB_PAN         0x01
#define KNOB_PITCH       0x02
#define KNOB_RES         0x03
#define KNOB_EG          0x04
#define KNOB_MOD_DEPTH   0x05
#define KNOB_ATTACK      0x06
#define KNOB_DECAY       0x08
#define KNOB_MOD_SPEED   0x0a

/* Electribe 2 Encoder IDs */
#define ENC_MAIN         0x00
#define ENC_OSC          0x01
#define ENC_CUTOFF       0x02
#define ENC_MOD          0x03
#define ENC_IFX          0x04

static UG_GUI g_ugui;
static UG_DEVICE g_ugui_device;
static uint8_t g_ui_tick_div = 0;

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

static void _ugui_pixel(UG_S16 x, UG_S16 y, UG_COLOR c) {
    if (x >= 0 && x < 128 && y >= 0 && y < 64) {
        ft_put_pixel((uint16_t)x, (uint16_t)y, c == C_BLACK);
    }
}

static void _tick_1ms_isr(void) {
    seq_engine_tick_1ms();
}

static void _set_pad_rgb(uint8_t pad, uint8_t red, uint8_t blue) {
    if (pad < 16) {
        ft_set_led(k_pad_red_leds[pad], red);
        ft_set_led(k_pad_blue_leds[pad], blue);
    }
}

static void _update_panel_leds(void) {
    t_seq_track *trk = &g_brain.tracks[g_brain.current_track];

    /* Transport & Mode LEDs */
    ft_set_led(LED_PLAY, g_brain.playing ? 255 : 0);
    ft_set_led(LED_REC, g_brain.recording ? 255 : 0);
    ft_set_led(LED_TAP, g_brain.fill_active ? 255 : 0);
    ft_set_led(LED_SHIFT, g_brain.shift_held ? 255 : 0);

    ft_set_led(LED_SEQUENCER, g_brain.ui_mode == MODE_SEQUENCER ? 255 : 0);
    ft_set_led(LED_TRIGGER, g_brain.ui_mode == MODE_TRIGGER ? 255 : 0);
    ft_set_led(LED_KEYBOARD, g_brain.ui_mode == MODE_KEYBOARD ? 255 : 0);
    ft_set_led(LED_CHORD, g_brain.ui_mode == MODE_GENERATIVE ? 255 : 0);
    ft_set_led(LED_STEP_JUMP, g_brain.ui_mode == MODE_STEP_JUMP ? 255 : 0);
    ft_set_led(LED_PART_MUTE, g_brain.ui_mode == MODE_PART_MUTE ? 255 : 0);
    ft_set_led(LED_PART_ERASE, g_brain.ui_mode == MODE_PART_ERASE ? 255 : 0);
    ft_set_led(LED_PATTERN_SET, g_brain.ui_mode == MODE_PATTERN_SET ? 255 : 0);

    /*
     * 4 Bar LEDs represent 16 Bars (256 steps) via 4 Banks of 4 Bars:
     * Bank 0 (Bars 1..4):   Blue
     * Bank 1 (Bars 5..8):   Red
     * Bank 2 (Bars 9..12):  Purple (Red + Blue)
     * Bank 3 (Bars 13..16): Blinking Red/Blue
     */
    uint8_t bank = g_brain.current_bar / 4;
    uint8_t bar_in_bank = g_brain.current_bar % 4;
    uint8_t play_bar = (uint8_t)(trk->play_step / 16);

    for (uint8_t b = 0; b < 4; b++) {
        uint8_t r = 0, bl = 0;
        if (b == bar_in_bank) {
            if (bank == 0) {
                bl = 255;
            } else if (bank == 1) {
                r = 255;
            } else if (bank == 2) {
                r = 200;
                bl = 200;
            } else {
                r = (g_brain.global_tick & 8) ? 255 : 40;
                bl = (g_brain.global_tick & 8) ? 40 : 255;
            }
        } else if (g_brain.playing && (bank * 4 + b) == play_bar) {
            r = 90;
        }
        ft_set_led(k_bar_red_leds[b], r);
        ft_set_led(k_bar_blue_leds[b], bl);
    }

    /* 16 Trigger Pads LEDs */
    uint16_t base_step = (uint16_t)(g_brain.current_bar * 16);
    for (uint8_t p = 0; p < 16; p++) {
        uint8_t r = 0, bl = 0;

        if (g_brain.ui_mode == MODE_SEQUENCER ||
            g_brain.ui_mode == MODE_STEP_JUMP) {
            uint16_t s_idx = base_step + p;
            t_seq_step *st = &trk->steps[s_idx];
            bool is_playhead = g_brain.playing && (trk->play_step == s_idx);
            bool has_plock = (st->cond_ratchet != 0) ||
                             ((st->comp_cclock & 0x07) != 0) ||
                             (st->cc_val <= 127) ||
                             (((st->flags >> 2) & 0x07) != 0);

            if (is_playhead) {
                r = 255;
                bl = (st->flags & STEP_FLAG_ACTIVE) ? 120 : 0;
            } else if (g_brain.held_step_pad == (int8_t)p) {
                r = 255;
                bl = 255;
            } else if (st->flags & STEP_FLAG_ACTIVE) {
                bl = 220;
                if (has_plock) {
                    r = 160; /* Purple = Active step with Elektron P-Lock / Ratchet */
                }
            }
        } else if (g_brain.ui_mode == MODE_TRIGGER) {
            if (p == g_brain.current_track) {
                r = 255;
                bl = 255;
            } else if (!g_brain.tracks[p].muted) {
                bl = 140;
                if (g_brain.playing &&
                    (g_brain.tracks[p].steps[g_brain.tracks[p].play_step].flags &
                     STEP_FLAG_ACTIVE)) {
                    r = 255;
                }
            }
        } else if (g_brain.ui_mode == MODE_PART_MUTE) {
            if (!g_brain.tracks[p].muted) {
                bl = 220;
            } else {
                r = 90;
            }
        } else if (g_brain.ui_mode == MODE_PATTERN_SET) {
            /* 16 pads = 16 bars (256 steps) */
            if (p == g_brain.current_bar) {
                bl = 255;
                r = 255;
            } else if (p < (trk->length + 15) / 16) {
                bl = 140;
            }
            if (g_brain.playing && p == play_bar) {
                r = 255;
            }
        } else if (g_brain.ui_mode == MODE_GENERATIVE) {
            /* Highlight generative action pads */
            if (p < 4) {
                bl = 180; /* Euclidean presets */
            } else if (p == 4 || p == 5) {
                r = 240;
                bl = 180; /* Aphex / Ambient generators */
            } else if (p == 6 || p == 7) {
                r = 200; /* Turing drift */
            } else {
                bl = 90;
            }
        } else if (g_brain.ui_mode == MODE_KEYBOARD) {
            bl = (p == 0 || p == 12) ? 255 : 110;
        }

        _set_pad_rgb(p, r, bl);
    }
}

static void _render_oled_ui(void) {
    t_seq_track *trk = &g_brain.tracks[g_brain.current_track];
    char line[32];

    UG_FillFrame(0, 0, 127, 63, C_WHITE);
    UG_FontSelect(FONT_5X8);
    UG_SetBackcolor(C_WHITE);
    UG_SetForecolor(C_BLACK);

    /* --- TOP BAR: Track, Channel, BPM, Bar (1..16) --- */
    uint8_t total_bars = (uint8_t)((trk->length + 15) / 16);
    snprintf(line, sizeof(line), "T%02d C%02d %3d %s %02d/%02d",
             g_brain.current_track + 1, trk->midi_channel + 1,
             g_brain.bpm_tenths / 10,
             g_brain.playing ? (g_brain.fill_active ? "FIL" : "PLY") : "STP",
             g_brain.current_bar + 1, total_bars);
    UG_PutString(0, 0, line);
    UG_DrawLine(0, 9, 127, 9, C_BLACK);

    /* --- MIDDLE SECTION --- */
    if (g_brain.held_step_pad >= 0 && g_brain.held_step_pad < 16) {
        /* ELEKTRON PARAMETER-LOCK INSPECTOR OVERLAY */
        uint16_t s_idx = (uint16_t)(g_brain.current_bar * 16 + g_brain.held_step_pad);
        t_seq_step *st = &trk->steps[s_idx];
        char n_str[8];
        seq_note_name(st->note, n_str);
        uint8_t cond = (st->cond_ratchet >> 4) & 0x0F;
        uint8_t r_idx = st->cond_ratchet & 0x0F;
        static const uint8_t r_map[6] = {1, 2, 3, 4, 6, 8};
        uint8_t comp = st->comp_cclock & 0x07;
        uint8_t chrd = (st->flags >> 2) & 0x07;

        snprintf(line, sizeof(line), "PLOCK S%03d [%s]", s_idx + 1, n_str);
        UG_PutString(0, 12, line);

        snprintf(line, sizeof(line), "V:%03d G:%02d uT:%+03d",
                 st->velocity, st->gate, st->microtiming);
        UG_PutString(0, 22, line);

        snprintf(line, sizeof(line), "%s P:%03d%% R:%dx",
                 seq_cond_name(cond), st->probability, r_map[r_idx % 6]);
        UG_PutString(0, 32, line);

        if (st->cc_val <= 127) {
            snprintf(line, sizeof(line), "%s %s CC:%03d",
                     seq_comp_name(comp), seq_chord_name(chrd), st->cc_val);
        } else {
            snprintf(line, sizeof(line), "%s %s CC:---",
                     seq_comp_name(comp), seq_chord_name(chrd));
        }
        UG_PutString(0, 42, line);
    } else {
        /* 256-Step Bar Overview Ribbon (16 bars x 7px wide) */
        uint8_t play_bar = (uint8_t)(trk->play_step / 16);
        for (uint8_t b = 0; b < 16; b++) {
            uint16_t bx = (uint16_t)(2 + b * 8);
            bool in_len = (b * 16) < trk->length;
            if (b == g_brain.current_bar) {
                UG_FillFrame(bx, 11, bx + 6, 15, C_BLACK);
            } else if (in_len) {
                UG_DrawFrame(bx, 11, bx + 6, 15, C_BLACK);
                if (g_brain.playing && b == play_bar) {
                    UG_FillFrame(bx + 2, 12, bx + 4, 14, C_BLACK);
                }
            } else {
                UG_DrawPixel(bx + 3, 13, C_BLACK);
            }
        }

        /* 16-Step Graphical Visualizer for Current Bar */
        uint16_t base_s = (uint16_t)(g_brain.current_bar * 16);
        for (uint8_t i = 0; i < 16; i++) {
            uint16_t s_idx = base_s + i;
            t_seq_step *st = &trk->steps[s_idx];
            uint16_t x0 = (uint16_t)(i * 8 + 1);
            uint16_t x1 = x0 + 5;

            /* Baseline tick */
            UG_DrawLine(x0, 40, x1, 40, C_BLACK);

            if (st->flags & STEP_FLAG_ACTIVE) {
                /* Bar height proportional to note pitch (24..96) */
                int h = ((int)st->note - 24) / 4;
                if (h < 3) {
                    h = 3;
                }
                if (h > 20) {
                    h = 20;
                }
                uint16_t y_top = (uint16_t)(40 - h);
                if ((st->cond_ratchet & 0x0F) > 0) {
                    /* Striped bar = Aphex Ratchet step */
                    for (uint16_t yy = y_top; yy <= 40; yy += 2) {
                        UG_DrawLine(x0, yy, x1, yy, C_BLACK);
                    }
                } else if (st->probability < 100 || (st->cond_ratchet >> 4) > 0) {
                    /* Hollow bar = Conditional / Probabilistic step */
                    UG_DrawFrame(x0, y_top, x1, 40, C_BLACK);
                } else {
                    /* Solid bar = Normal active step */
                    UG_FillFrame(x0, y_top, x1, 40, C_BLACK);
                }
            }

            /* Playhead triangle indicator */
            if (g_brain.playing && trk->play_step == s_idx) {
                UG_FillFrame(x0 + 1, 42, x1 - 1, 44, C_BLACK);
            }
        }

        /* Track Timing & Generative Info Line */
        snprintf(line, sizeof(line), "L:%03d %s %s TRG:%02d%%",
                 trk->length, seq_clk_name(trk->clock_scale),
                 seq_dir_name(trk->direction), trk->turing_prob);
        UG_PutString(0, 46, line);
    }

    /* --- BOTTOM STATUS / SCALE / POPUP BAR --- */
    UG_DrawLine(0, 54, 127, 54, C_BLACK);
    if (g_brain.status_timer > 0) {
        UG_PutString(0, 56, g_brain.status_msg);
    } else {
        static const char *root_names[12] = {
            "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
        };
        snprintf(line, sizeof(line), "%s %s E:%d/%d R:%d%%",
                 root_names[g_brain.root_note % 12],
                 seq_scale_name(g_brain.scale_id), trk->euclid_pulses,
                 trk->euclid_steps, trk->ratchet_prob);
        UG_PutString(0, 56, line);
    }
}

/* --- PANEL CALLBACKS --- */

static void _trigger_callback(uint8_t raw_pad, uint8_t vel, bool state) {
    uint8_t pad = (raw_pad >= 0x11) ? (raw_pad - 0x11) : raw_pad;
    if (pad >= 16) {
        return;
    }

    t_seq_track *trk = &g_brain.tracks[g_brain.current_track];

    if (g_brain.ui_mode == MODE_SEQUENCER) {
        if (state) {
            g_brain.held_step_pad = (int8_t)pad;
            g_brain.plock_modified = false;
        } else {
            if (g_brain.held_step_pad == (int8_t)pad) {
                if (!g_brain.plock_modified) {
                    seq_toggle_step_pad(pad);
                }
                g_brain.held_step_pad = -1;
            }
        }
    } else if (g_brain.ui_mode == MODE_TRIGGER ||
               g_brain.ui_mode == MODE_KEYBOARD) {
        seq_trigger_live_pad(pad, vel, state);
    } else if (g_brain.ui_mode == MODE_PART_MUTE && state) {
        g_brain.tracks[pad].muted = !g_brain.tracks[pad].muted;
        if (g_brain.tracks[pad].muted) {
            seq_set_status("MUTE TRK", pad + 1);
        } else {
            seq_set_status("UNMUTE TRK", pad + 1);
        }
    } else if (g_brain.ui_mode == MODE_PART_ERASE && state) {
        seq_clear_track(pad);
        seq_set_status("CLEARED TRK", pad + 1);
    } else if (g_brain.ui_mode == MODE_STEP_JUMP && state) {
        if (g_brain.shift_held) {
            g_brain.current_bar = pad;
            trk->play_step = (uint16_t)(pad * 16);
            seq_set_status("JUMP BAR", pad + 1);
        } else {
            trk->play_step = (uint16_t)(g_brain.current_bar * 16 + pad);
            seq_set_status("JUMP STEP", trk->play_step + 1);
        }
    } else if (g_brain.ui_mode == MODE_PATTERN_SET && state) {
        if (g_brain.shift_held) {
            trk->length = (uint16_t)((pad + 1) * 16);
            seq_set_status("TRK LEN", trk->length);
        } else {
            g_brain.current_bar = pad;
            g_brain.bar_bank = pad / 4;
            seq_set_status("BAR PAGE", pad + 1);
        }
    } else if (g_brain.ui_mode == MODE_GENERATIVE && state) {
        /* Playful Generative Pad Matrix */
        switch (pad) {
        case 0:
            seq_apply_euclidean(g_brain.current_track, 3, 8, 0);
            seq_set_status_str("EUCLID", "3/8 TRESILLO");
            break;
        case 1:
            seq_apply_euclidean(g_brain.current_track, 5, 16, 0);
            seq_set_status_str("EUCLID", "5/16 BOSSA");
            break;
        case 2:
            seq_apply_euclidean(g_brain.current_track, 7, 16, 0);
            seq_set_status_str("EUCLID", "7/16 IDM");
            break;
        case 3:
            seq_apply_euclidean(g_brain.current_track, 11, 16, 2);
            seq_set_status_str("EUCLID", "11/16 DENSE");
            break;
        case 4:
            seq_generate_aphex_track(g_brain.current_track);
            break;
        case 5:
            seq_generate_ambient_track(g_brain.current_track);
            break;
        case 6:
            trk->turing_prob = (trk->turing_prob == 20) ? 0 : 20;
            seq_set_status("TURING DRIFT%", trk->turing_prob);
            break;
        case 7:
            trk->turing_prob = (trk->turing_prob == 70) ? 0 : 70;
            seq_set_status("APHEX CHAOS%", trk->turing_prob);
            break;
        case 8:
        case 9:
        case 10:
        case 11:
        case 12:
            trk->direction = (uint8_t)(pad - 8);
            seq_set_status_str("DIR", seq_dir_name(trk->direction));
            break;
        case 13:
            if (trk->clock_scale > 0) {
                trk->clock_scale--;
            }
            seq_set_status_str("CLOCK", seq_clk_name(trk->clock_scale));
            break;
        case 14:
            if (trk->clock_scale + 1 < CLK_SCALE_COUNT) {
                trk->clock_scale++;
            }
            seq_set_status_str("CLOCK", seq_clk_name(trk->clock_scale));
            break;
        case 15:
            seq_clear_track(g_brain.current_track);
            seq_set_status_str("TRACK", "CLEARED");
            break;
        }
    }
}

static void _button_callback(uint8_t index, bool state) {
    t_seq_track *trk = &g_brain.tracks[g_brain.current_track];

    if (index == BTN_SHIFT) {
        g_brain.shift_held = state;
        return;
    }
    if (index == BTN_TAP) {
        /* Tap acts as Elektron FILL button! */
        g_brain.fill_active = state;
        if (state) {
            seq_set_status_str("TRIG", "FILL ACTIVE");
        }
        return;
    }

    if (!state) {
        return;
    }

    switch (index) {
    case BTN_PLAY:
        seq_engine_toggle_play();
        break;

    case BTN_STOP:
        seq_engine_stop();
        break;

    case BTN_REC:
        g_brain.recording = !g_brain.recording;
        seq_set_status_str("REC", g_brain.recording ? "ON" : "OFF");
        break;

    case BTN_LEFT:
        if (g_brain.shift_held) {
            /* Previous Bar (1..16) */
            g_brain.current_bar = (g_brain.current_bar == 0)
                                      ? 15
                                      : (g_brain.current_bar - 1);
            g_brain.bar_bank = g_brain.current_bar / 4;
            seq_set_status("BAR PAGE", g_brain.current_bar + 1);
        } else {
            g_brain.current_track = (g_brain.current_track == 0)
                                        ? 15
                                        : (g_brain.current_track - 1);
            seq_set_status("TRACK", g_brain.current_track + 1);
        }
        break;

    case BTN_RIGHT:
        if (g_brain.shift_held) {
            /* Next Bar (1..16) */
            g_brain.current_bar = (g_brain.current_bar + 1) % 16;
            g_brain.bar_bank = g_brain.current_bar / 4;
            seq_set_status("BAR PAGE", g_brain.current_bar + 1);
        } else {
            g_brain.current_track = (g_brain.current_track + 1) % 16;
            seq_set_status("TRACK", g_brain.current_track + 1);
        }
        break;

    case BTN_BACK:
        g_brain.current_bar = (g_brain.current_bar == 0)
                                  ? 15
                                  : (g_brain.current_bar - 1);
        g_brain.bar_bank = g_brain.current_bar / 4;
        seq_set_status("BAR PAGE", g_brain.current_bar + 1);
        break;

    case BTN_FORWARD:
        g_brain.current_bar = (g_brain.current_bar + 1) % 16;
        g_brain.bar_bank = g_brain.current_bar / 4;
        seq_set_status("BAR PAGE", g_brain.current_bar + 1);
        break;

    case BTN_SEQUENCER:
        g_brain.ui_mode = MODE_SEQUENCER;
        seq_set_status_str("MODE", "SEQUENCER");
        break;

    case BTN_TRIGGER:
        g_brain.ui_mode = MODE_TRIGGER;
        seq_set_status_str("MODE", "TRIGGER 16");
        break;

    case BTN_KEYBOARD:
        g_brain.ui_mode = MODE_KEYBOARD;
        seq_set_status_str("MODE", "KEYBOARD");
        break;

    case BTN_CHORD:
        g_brain.ui_mode = MODE_GENERATIVE;
        seq_set_status_str("MODE", "GENERATIVE");
        break;

    case BTN_STEP_JUMP:
        g_brain.ui_mode = MODE_STEP_JUMP;
        seq_set_status_str("MODE", "STEP JUMP");
        break;

    case BTN_PART_MUTE:
        g_brain.ui_mode = MODE_PART_MUTE;
        seq_set_status_str("MODE", "PART MUTE");
        break;

    case BTN_PART_ERASE:
        g_brain.ui_mode = MODE_PART_ERASE;
        seq_set_status_str("MODE", "PART ERASE");
        break;

    case BTN_PATTERN_SET:
        g_brain.ui_mode = MODE_PATTERN_SET;
        seq_set_status_str("MODE", "BARS 1-16");
        break;

    case BTN_LPF:
        seq_generate_aphex_track(g_brain.current_track);
        break;

    case BTN_HPF:
        seq_generate_ambient_track(g_brain.current_track);
        break;

    case BTN_BPF:
        trk->direction = (trk->direction + 1) % DIR_COUNT;
        seq_set_status_str("DIRECTION", seq_dir_name(trk->direction));
        break;

    case BTN_AMP_EG:
        g_brain.follow_bars = !g_brain.follow_bars;
        seq_set_status_str("FOLLOW BARS", g_brain.follow_bars ? "ON" : "OFF");
        break;

    case BTN_WRITE:
        if (g_brain.shift_held) {
            seq_load_from_flash(0);
        } else {
            seq_save_to_flash(0);
        }
        break;

    case BTN_EXIT:
        if (g_brain.shift_held) {
            ft_shutdown();
        }
        break;

    default:
        break;
    }
}

static void _encoder_callback(uint8_t index, int8_t raw_val) {
    int delta = (raw_val == 1 || raw_val > 0) ? 1 : -1;
    t_seq_track *trk = &g_brain.tracks[g_brain.current_track];

    /* If holding a step pad in Sequencer mode, encoders edit Step P-Locks! */
    if (g_brain.held_step_pad >= 0 && g_brain.held_step_pad < 16) {
        uint16_t s_idx =
            (uint16_t)(g_brain.current_bar * 16 + g_brain.held_step_pad);
        t_seq_step *st = &trk->steps[s_idx];
        st->flags |= STEP_FLAG_ACTIVE;
        g_brain.plock_modified = true;

        switch (index) {
        case ENC_MAIN: {
            int n = (int)st->note + delta;
            if (n < 0) {
                n = 0;
            }
            if (n > 127) {
                n = 127;
            }
            st->note = (uint8_t)n;
            char n_str[8];
            seq_note_name(st->note, n_str);
            seq_set_status_str("PLOCK NOTE", n_str);
            break;
        }
        case ENC_OSC: {
            int r = (int)(st->cond_ratchet & 0x0F) + delta;
            if (r < 0) {
                r = 0;
            }
            if (r > 5) {
                r = 5;
            }
            st->cond_ratchet = (uint8_t)((st->cond_ratchet & 0xF0) | (r & 0x0F));
            static const uint8_t r_map[6] = {1, 2, 3, 4, 6, 8};
            seq_set_status("PLOCK RATCHET", r_map[r]);
            break;
        }
        case ENC_CUTOFF: {
            int c = (int)((st->cond_ratchet >> 4) & 0x0F) + delta;
            if (c < 0) {
                c = COND_COUNT - 1;
            }
            if (c >= COND_COUNT) {
                c = 0;
            }
            st->cond_ratchet =
                (uint8_t)(((c & 0x0F) << 4) | (st->cond_ratchet & 0x0F));
            seq_set_status_str("PLOCK COND", seq_cond_name((uint8_t)c));
            break;
        }
        case ENC_MOD: {
            int cmp = (int)(st->comp_cclock & 0x07) + delta;
            if (cmp < 0) {
                cmp = COMP_COUNT - 1;
            }
            if (cmp >= COMP_COUNT) {
                cmp = 0;
            }
            st->comp_cclock = (uint8_t)((st->comp_cclock & 0xF8) | (cmp & 0x07));
            seq_set_status_str("STEP COMP", seq_comp_name((uint8_t)cmp));
            break;
        }
        case ENC_IFX: {
            int chd = (int)((st->flags >> 2) & 0x07) + delta;
            if (chd < 0) {
                chd = CHORD_TYPE_COUNT - 1;
            }
            if (chd >= CHORD_TYPE_COUNT) {
                chd = 0;
            }
            st->flags = (uint8_t)((st->flags & ~(0x07 << 2)) | ((chd & 0x07) << 2));
            seq_set_status_str("PLOCK CHORD", seq_chord_name((uint8_t)chd));
            break;
        }
        }
        return;
    }

    /* Normal Encoder Actions (Track / Global) */
    switch (index) {
    case ENC_MAIN:
        if (g_brain.shift_held) {
            /* Track Length 1..256 steps */
            int len = (int)trk->length + delta * 4;
            if (len < 1) {
                len = 1;
            }
            if (len > SEQ_MAX_STEPS) {
                len = SEQ_MAX_STEPS;
            }
            trk->length = (uint16_t)len;
            seq_set_status("TRK STEPS", trk->length);
        } else {
            int bpm = (int)g_brain.bpm_tenths + delta * 5;
            if (bpm < 300) {
                bpm = 300;
            }
            if (bpm > 3000) {
                bpm = 3000;
            }
            g_brain.bpm_tenths = (uint16_t)bpm;
            seq_set_status("BPM x10", g_brain.bpm_tenths);
        }
        break;

    case ENC_OSC:
        if (g_brain.shift_held) {
            int ch = (int)trk->midi_channel + delta;
            if (ch < 0) {
                ch = 15;
            }
            if (ch > 15) {
                ch = 0;
            }
            trk->midi_channel = (uint8_t)ch;
            seq_set_status("MIDI CHAN", trk->midi_channel + 1);
        } else {
            /* Euclidean Pulses K */
            int k = (int)trk->euclid_pulses + delta;
            if (k < 0) {
                k = 0;
            }
            if (k > trk->euclid_steps) {
                k = trk->euclid_steps;
            }
            seq_apply_euclidean(g_brain.current_track, (uint8_t)k,
                                trk->euclid_steps, trk->euclid_offset);
            seq_set_status("EUCLID PULSES", trk->euclid_pulses);
        }
        break;

    case ENC_CUTOFF:
        if (g_brain.shift_held) {
            int sc = (int)g_brain.scale_id + delta;
            if (sc < 0) {
                sc = SCALE_COUNT - 1;
            }
            if (sc >= SCALE_COUNT) {
                sc = 0;
            }
            g_brain.scale_id = (uint8_t)sc;
            seq_set_status_str("SCALE", seq_scale_name(g_brain.scale_id));
        } else {
            /* Clock Scale (/16 .. 4x) */
            int cs = (int)trk->clock_scale + delta;
            if (cs < 0) {
                cs = 0;
            }
            if (cs >= CLK_SCALE_COUNT) {
                cs = CLK_SCALE_COUNT - 1;
            }
            trk->clock_scale = (uint8_t)cs;
            seq_set_status_str("CLK SCALE", seq_clk_name(trk->clock_scale));
        }
        break;

    case ENC_MOD: {
        int dir = (int)trk->direction + delta;
        if (dir < 0) {
            dir = DIR_COUNT - 1;
        }
        if (dir >= DIR_COUNT) {
            dir = 0;
        }
        trk->direction = (uint8_t)dir;
        seq_set_status_str("DIRECTION", seq_dir_name(trk->direction));
        break;
    }

    case ENC_IFX: {
        int len = (int)trk->length + delta;
        if (len < 1) {
            len = 1;
        }
        if (len > SEQ_MAX_STEPS) {
            len = SEQ_MAX_STEPS;
        }
        trk->length = (uint16_t)len;
        seq_set_status("TRK STEPS", trk->length);
        break;
    }
    }
}

static void _knob_callback(uint8_t index, uint8_t value) {
    t_seq_track *trk = &g_brain.tracks[g_brain.current_track];
    uint8_t val7 = value >> 1; /* 0..127 */

    /* If holding a step pad in Sequencer mode, Knobs apply Elektron P-Locks! */
    if (g_brain.held_step_pad >= 0 && g_brain.held_step_pad < 16) {
        uint16_t s_idx =
            (uint16_t)(g_brain.current_bar * 16 + g_brain.held_step_pad);
        t_seq_step *st = &trk->steps[s_idx];
        st->flags |= STEP_FLAG_ACTIVE;
        g_brain.plock_modified = true;

        switch (index) {
        case KNOB_PITCH: {
            st->note = seq_quantize_note(val7, g_brain.root_note,
                                         g_brain.scale_id);
            char n_str[8];
            seq_note_name(st->note, n_str);
            seq_set_status_str("PLOCK NOTE", n_str);
            break;
        }
        case KNOB_LEVEL:
            st->velocity = (val7 < 1) ? 1 : val7;
            seq_set_status("PLOCK VEL", st->velocity);
            break;
        case KNOB_DECAY:
            st->gate = (uint8_t)(1 + (val7 * 95) / 127);
            seq_set_status("PLOCK GATE", st->gate);
            break;
        case KNOB_ATTACK:
            st->microtiming = (int8_t)(((int)val7 - 64) * 23 / 64);
            seq_set_status("PLOCK uTIME", st->microtiming);
            break;
        case KNOB_MOD_DEPTH:
            st->probability = (uint8_t)(1 + (val7 * 99) / 127);
            seq_set_status("PLOCK PROB%", st->probability);
            break;
        case KNOB_MOD_SPEED: {
            uint8_t r = (uint8_t)((val7 * 5) / 127);
            st->cond_ratchet = (uint8_t)((st->cond_ratchet & 0xF0) | (r & 0x0F));
            static const uint8_t r_map[6] = {1, 2, 3, 4, 6, 8};
            seq_set_status("PLOCK RATCH", r_map[r]);
            break;
        }
        case KNOB_RES:
        case KNOB_EG:
            st->cc_val = val7;
            seq_set_status("PLOCK CC", st->cc_val);
            break;
        }
        return;
    }

    /* Normal Live Track / Generative Knobs */
    switch (index) {
    case KNOB_PITCH:
        trk->base_note = seq_quantize_note(val7, g_brain.root_note,
                                           g_brain.scale_id);
        seq_set_status("BASE NOTE", trk->base_note);
        break;

    case KNOB_MOD_DEPTH:
        trk->turing_prob = (uint8_t)((val7 * 100) / 127);
        seq_set_status("TURING %", trk->turing_prob);
        break;

    case KNOB_MOD_SPEED:
        trk->ratchet_prob = (uint8_t)((val7 * 100) / 127);
        seq_set_status("RATCHET %", trk->ratchet_prob);
        break;

    case KNOB_RES:
        ft_send_cc((char)trk->midi_channel, (char)trk->cc_number, (char)val7);
        trk->cc_current = val7;
        seq_set_status("LIVE CC74", val7);
        break;

    case KNOB_EG:
        ft_send_cc((char)trk->midi_channel, 71, (char)val7);
        seq_set_status("LIVE CC71", val7);
        break;

    case KNOB_DECAY: {
        uint8_t new_gate = (uint8_t)(1 + (val7 * 95) / 127);
        for (uint16_t s = 0; s < trk->length; s++) {
            trk->steps[s].gate = new_gate;
        }
        seq_set_status("TRK GATE", new_gate);
        break;
    }

    case KNOB_LEVEL: {
        uint8_t new_vel = (val7 < 1) ? 1 : val7;
        for (uint16_t s = 0; s < trk->length; s++) {
            trk->steps[s].velocity = new_vel;
        }
        seq_set_status("TRK VEL", new_vel);
        break;
    }

    default:
        break;
    }
}

static void _xy_pad_callback(uint32_t x_val, uint32_t y_val) {
    t_seq_track *trk = &g_brain.tracks[g_brain.current_track];
    /* Normalize X/Y to 0..127 */
    uint8_t x7 = (uint8_t)((x_val > 127) ? ((x_val >> 3) & 0x7F) : x_val);
    uint8_t y7 = (uint8_t)((y_val > 127) ? ((y_val >> 3) & 0x7F) : y_val);

    g_brain.xy_active = true;
    g_brain.xy_x = x7;
    g_brain.xy_y = y7;

    /* Send expressive dual MIDI CCs (CC74 Filter + CC1 ModWheel) */
    ft_send_cc((char)trk->midi_channel, (char)trk->cc_number, (char)x7);
    ft_send_cc((char)trk->midi_channel, 1, (char)y7);
}

t_status app_init(void) {
    seq_engine_init();

    g_ugui_device.x_dim = 128;
    g_ugui_device.y_dim = 64;
    g_ugui_device.pset = _ugui_pixel;
    UG_Init(&g_ugui, &g_ugui_device);
    UG_FontSelect(FONT_5X8);
    UG_FillScreen(C_WHITE);

    ft_register_panel_callback(TRIGGER_EVENT, _trigger_callback);
    ft_register_panel_callback(BUTTON_EVENT, _button_callback);
    ft_register_panel_callback(ENCODER_EVENT, _encoder_callback);
    ft_register_panel_callback(KNOB_EVENT, _knob_callback);
    ft_register_panel_callback(XY_PAD_EVENT, _xy_pad_callback);

    /* 1ms high-precision tick for 96 PPQN sequencer engine */
    ft_register_tick_callback(0, _tick_1ms_isr);

    _update_panel_leds();
    _render_oled_ui();

    return SUCCESS;
}

void app_run(void) {
    g_ui_tick_div++;
    if ((g_ui_tick_div & 0x07) == 0) {
        _update_panel_leds();
        _render_oled_ui();
    }
}
