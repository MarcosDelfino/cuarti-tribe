/*----------------------------------------------------------------------
 * CuartiTribe Brain - macOS Hardware Emulator (SDL2 + CoreMIDI)
 * Runs the exact same C firmware code (seq_engine.c + brain_app.c + UGUI)
 * and includes a live Paper-Ink (e-Paper) companion screen preview.
 *----------------------------------------------------------------------*/

#include <SDL.h>
#include <CoreMIDI/CoreMIDI.h>
#include <mach/mach_time.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freetribe.h"
#include "seq_engine.h"
#include "ugui.h"

/* Panel Button / Knob / Encoder IDs matching brain_app.c */
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

#define KNOB_LEVEL       0x00
#define KNOB_PITCH       0x02
#define KNOB_RES         0x03
#define KNOB_EG          0x04
#define KNOB_MOD_DEPTH   0x05
#define KNOB_ATTACK      0x06
#define KNOB_DECAY       0x08
#define KNOB_MOD_SPEED   0x0a

#define ENC_MAIN         0x00
#define ENC_OSC          0x01
#define ENC_CUTOFF       0x02
#define ENC_MOD          0x03
#define ENC_IFX          0x04

/* --- Emulated Hardware State --- */
static bool g_oled_fb[128][64];
static uint8_t g_leds[LED_COUNT];

static void (*g_tick_cb)(void) = NULL;
static void (*g_btn_cb)(uint8_t, bool) = NULL;
static void (*g_enc_cb)(uint8_t, int8_t) = NULL;
static void (*g_knob_cb)(uint8_t, uint8_t) = NULL;
static void (*g_trig_cb)(uint8_t, uint8_t, bool) = NULL;
static void (*g_xy_cb)(uint32_t, uint32_t) = NULL;

/* --- CoreMIDI State --- */
static MIDIClientRef g_midi_client = 0;
static MIDIEndpointRef g_midi_source = 0;
static MIDIPortRef g_midi_out_port = 0;
static int g_midi_dest_idx = -1; /* -1 = Virtual Port Only, >=0 = Hardware/IAC Dest */
static char g_midi_dest_name[64] = "Virtual: CuartiTribe Brain Out";

#define MIDI_LOG_LINES 10
static char g_midi_log[MIDI_LOG_LINES][40];
static int g_midi_log_head = 0;

static void _log_midi(const char *msg) {
    strncpy(g_midi_log[g_midi_log_head], msg, 39);
    g_midi_log[g_midi_log_head][39] = '\0';
    g_midi_log_head = (g_midi_log_head + 1) % MIDI_LOG_LINES;
}

static void _send_coremidi_bytes(const uint8_t *data, size_t len) {
    if (!g_midi_client) {
        return;
    }
    Byte packet_buf[128];
    MIDIPacketList *pkt_list = (MIDIPacketList *)packet_buf;
    MIDIPacket *pkt = MIDIPacketListInit(pkt_list);
    pkt = MIDIPacketListAdd(pkt_list, sizeof(packet_buf), pkt,
                            mach_absolute_time(), len, data);
    if (pkt) {
        if (g_midi_source) {
            MIDIReceived(g_midi_source, pkt_list);
        }
        if (g_midi_out_port && g_midi_dest_idx >= 0) {
            ItemCount n_dests = MIDIGetNumberOfDestinations();
            if ((ItemCount)g_midi_dest_idx < n_dests) {
                MIDIEndpointRef dst = MIDIGetDestination((ItemCount)g_midi_dest_idx);
                MIDISend(g_midi_out_port, dst, pkt_list);
            }
        }
    }
}

static void _cycle_midi_dest(void) {
    ItemCount n = MIDIGetNumberOfDestinations();
    g_midi_dest_idx++;
    if (g_midi_dest_idx >= (int)n) {
        g_midi_dest_idx = -1;
        snprintf(g_midi_dest_name, sizeof(g_midi_dest_name),
                 "Virtual: CuartiTribe Brain Out");
    } else {
        MIDIEndpointRef dst = MIDIGetDestination((ItemCount)g_midi_dest_idx);
        CFStringRef cfs = NULL;
        MIDIObjectGetStringProperty(dst, kMIDIPropertyName, &cfs);
        if (cfs) {
            char tmp[48];
            CFStringGetCString(cfs, tmp, sizeof(tmp), kCFStringEncodingUTF8);
            CFRelease(cfs);
            snprintf(g_midi_dest_name, sizeof(g_midi_dest_name),
                     "Out+Dest[%d]: %s", g_midi_dest_idx + 1, tmp);
        }
    }
}

/* --- Freetribe HAL Implementation for Simulator --- */

void ft_register_tick_callback(uint32_t divisor, void (*callback)(void)) {
    (void)divisor;
    g_tick_cb = callback;
}

void ft_put_pixel(uint16_t pos_x, uint16_t pos_y, bool state) {
    if (pos_x < 128 && pos_y < 64) {
        g_oled_fb[pos_x][pos_y] = state;
    }
}

int8_t ft_fill_frame(uint16_t x_start, uint16_t y_start, uint16_t x_end,
                     uint16_t y_end, bool state) {
    for (uint16_t x = x_start; x <= x_end && x < 128; x++) {
        for (uint16_t y = y_start; y <= y_end && y < 64; y++) {
            g_oled_fb[x][y] = state;
        }
    }
    return 0;
}

void ft_register_panel_callback(t_panel_event event, void *callback) {
    switch (event) {
    case BUTTON_EVENT:
        g_btn_cb = (void (*)(uint8_t, bool))callback;
        break;
    case ENCODER_EVENT:
        g_enc_cb = (void (*)(uint8_t, int8_t))callback;
        break;
    case KNOB_EVENT:
        g_knob_cb = (void (*)(uint8_t, uint8_t))callback;
        break;
    case TRIGGER_EVENT:
        g_trig_cb = (void (*)(uint8_t, uint8_t, bool))callback;
        break;
    case XY_PAD_EVENT:
        g_xy_cb = (void (*)(uint32_t, uint32_t))callback;
        break;
    default:
        break;
    }
}

void ft_set_led(t_led_index led_index, uint8_t brightness) {
    if (led_index < LED_COUNT) {
        g_leds[led_index] = brightness;
    }
}

void ft_toggle_led(t_led_index led_index) {
    if (led_index < LED_COUNT) {
        g_leds[led_index] = g_leds[led_index] ? 0 : 255;
    }
}

void ft_send_note_on(char chan, char note, char vel) {
    uint8_t msg[3] = {
        (uint8_t)(0x90 | (chan & 0x0F)),
        (uint8_t)(note & 0x7F),
        (uint8_t)(vel & 0x7F)
    };
    _send_coremidi_bytes(msg, 3);
    char n_str[8];
    seq_note_name((uint8_t)note, n_str);
    char log_b[40];
    snprintf(log_b, sizeof(log_b), "NOTE ON  Ch%02d %-3s (%3d) V%03d",
             (chan & 0x0F) + 1, n_str, (uint8_t)note, (uint8_t)vel);
    _log_midi(log_b);
}

void ft_send_note_off(char chan, char note, char vel) {
    (void)vel;
    uint8_t msg[3] = {
        (uint8_t)(0x80 | (chan & 0x0F)),
        (uint8_t)(note & 0x7F),
        0
    };
    _send_coremidi_bytes(msg, 3);
}

void ft_send_cc(char chan, char index, char val) {
    uint8_t msg[3] = {
        (uint8_t)(0xB0 | (chan & 0x0F)),
        (uint8_t)(index & 0x7F),
        (uint8_t)(val & 0x7F)
    };
    _send_coremidi_bytes(msg, 3);
    char log_b[40];
    snprintf(log_b, sizeof(log_b), "MIDI CC  Ch%02d CC#%03d = %03d",
             (chan & 0x0F) + 1, (uint8_t)index, (uint8_t)val);
    _log_midi(log_b);
}

void svc_midi_send_byte(uint8_t byte) {
    _send_coremidi_bytes(&byte, 1);
    if (byte == 0xFA) {
        _log_midi("CLOCK    START (0xFA)");
    } else if (byte == 0xFC) {
        _log_midi("CLOCK    STOP  (0xFC)");
    }
}

void ft_shutdown(void) {
    exit(0);
}

/* Simulated SPI Flash persistence backed by local file */
void flash_write(uint32_t dest, uint8_t *p_src, uint32_t len) {
    FILE *f = fopen("brain_flash.bin", "r+b");
    if (!f) {
        f = fopen("brain_flash.bin", "wb");
    }
    if (f) {
        fseek(f, (long)(dest & 0x7FFFF), SEEK_SET);
        fwrite(p_src, 1, len, f);
        fclose(f);
    }
}

void flash_read(uint32_t src, uint8_t *p_dest, uint32_t len) {
    FILE *f = fopen("brain_flash.bin", "rb");
    if (f) {
        fseek(f, (long)(src & 0x7FFFF), SEEK_SET);
        size_t r = fread(p_dest, 1, len, f);
        (void)r;
        fclose(f);
    } else {
        memset(p_dest, 0xFF, len);
    }
}

/* --- Tiny 6x8 Font Renderer using UGUI's FONT_6X8 for SDL UI Labels --- */
static void _draw_char(SDL_Renderer *ren, int x, int y, char c, int scale) {
    const uint8_t *p = (const uint8_t *)FONT_6X8 + 14 + ((uint32_t)(uint8_t)c) * 8U;
    for (int row = 0; row < 8; row++) {
        uint8_t bits = p[row];
        for (int col = 0; col < 6; col++) {
            /* UGUI stores bit 0 as the leftmost pixel */
            if (bits & (1U << col)) {
                if (scale == 1) {
                    SDL_RenderDrawPoint(ren, x + col, y + row);
                } else {
                    SDL_Rect r = {x + col * scale, y + row * scale, scale, scale};
                    SDL_RenderFillRect(ren, &r);
                }
            }
        }
    }
}

static void _draw_text(SDL_Renderer *ren, int x, int y, const char *str,
                       SDL_Color col, int scale) {
    SDL_SetRenderDrawColor(ren, col.r, col.g, col.b, col.a);
    int cx = x;
    while (*str) {
        _draw_char(ren, cx, y, *str, scale);
        cx += 6 * scale;
        str++;
    }
}

/* --- Interactive UI Controls Layout --- */
typedef struct {
    SDL_Rect rect;
    uint8_t btn_id;
    t_led_index led_id;
    const char *label;
} t_sim_button;

typedef struct {
    SDL_Rect rect;
    uint8_t id;
    bool is_encoder;
    uint8_t value; /* 0..255 for knobs */
    const char *label;
    const char *sublabel;
} t_sim_dial;

static t_sim_button g_sim_buttons[] = {
    /* Transport & Shift Row */
    {{30,  310, 72, 34}, BTN_PLAY,        LED_PLAY,        "PLAY [Spc]"},
    {{110, 310, 65, 34}, BTN_STOP,        LED_STOP,        "STOP"},
    {{183, 310, 65, 34}, BTN_REC,         LED_REC,         "REC [R]"},
    {{256, 310, 72, 34}, BTN_TAP,         LED_TAP,         "FILL [F]"},
    {{336, 310, 68, 34}, BTN_SHIFT,       LED_SHIFT,       "SHIFT"},
    {{412, 310, 72, 34}, BTN_WRITE,       LED_WRITE,       "WRITE [S]"},

    /* Navigation & Quick Generators Row */
    {{30,  354, 72, 32}, BTN_LEFT,        LED_LEFT,        "< TRK"},
    {{110, 354, 72, 32}, BTN_RIGHT,       LED_RIGHT,       "TRK >"},
    {{190, 354, 72, 32}, BTN_BACK,        LED_BACK,        "< BAR"},
    {{270, 354, 72, 32}, BTN_FORWARD,     LED_FORWARD,     "BAR >"},
    {{350, 354, 80, 32}, BTN_LPF,         LED_LPF,         "APHEX GEN"},
    {{438, 354, 85, 32}, BTN_HPF,         LED_HPF,         "AMBIENT GEN"},
    {{531, 354, 65, 32}, BTN_BPF,         LED_BPF,         "DIR"},
    {{604, 354, 76, 32}, BTN_AMP_EG,      LED_AMP_EG,      "FOLLOW"},

    /* 8 Pad Mode Buttons */
    {{30,  435, 76, 32}, BTN_SEQUENCER,   LED_SEQUENCER,   "SEQUENCER"},
    {{112, 435, 76, 32}, BTN_TRIGGER,     LED_TRIGGER,     "TRIGGER"},
    {{194, 435, 76, 32}, BTN_KEYBOARD,    LED_KEYBOARD,    "KEYBOARD"},
    {{276, 435, 76, 32}, BTN_CHORD,       LED_CHORD,       "GEN/CHORD"},
    {{358, 435, 76, 32}, BTN_STEP_JUMP,   LED_STEP_JUMP,   "STEP JUMP"},
    {{440, 435, 76, 32}, BTN_PART_MUTE,   LED_PART_MUTE,   "PART MUTE"},
    {{522, 435, 76, 32}, BTN_PART_ERASE,  LED_PART_ERASE,  "ERASE"},
    {{604, 435, 76, 32}, BTN_PATTERN_SET, LED_PATTERN_SET, "BARS 1-16"}
};
#define NUM_SIM_BUTTONS (sizeof(g_sim_buttons) / sizeof(g_sim_buttons[0]))

static t_sim_dial g_sim_dials[] = {
    /* 5 Encoders (Top Right of OLED) */
    {{440, 50,  58, 52}, ENC_MAIN,   true,  128, "MAIN ENC", "BPM / Note"},
    {{504, 50,  58, 52}, ENC_OSC,    true,  128, "OSC ENC",  "Euclid/Ratch"},
    {{568, 50,  58, 52}, ENC_CUTOFF, true,  128, "CLK ENC",  "ClkDiv/Cond"},
    {{632, 50,  58, 52}, ENC_MOD,    true,  128, "DIR ENC",  "Dir / Comp"},
    {{696, 50,  58, 52}, ENC_IFX,    true,  128, "LEN ENC",  "Steps/Chord"},

    /* 8 Knobs (Middle Row) */
    {{440, 120, 58, 56}, KNOB_PITCH,     false, 120, "PITCH",   "Note"},
    {{504, 120, 58, 56}, KNOB_LEVEL,     false, 200, "LEVEL",   "Velocity"},
    {{568, 120, 58, 56}, KNOB_DECAY,     false, 60,  "DECAY",   "Gate Len"},
    {{632, 120, 58, 56}, KNOB_ATTACK,    false, 128, "ATTACK",  "uTiming"},
    {{440, 192, 58, 56}, KNOB_MOD_DEPTH, false, 0,   "TURING",  "Drift/Prob%"},
    {{504, 192, 58, 56}, KNOB_MOD_SPEED, false, 0,   "RATCHET", "Glitch/Burst"},
    {{568, 192, 58, 56}, KNOB_RES,       false, 128, "CC 74",   "Filter Lock"},
    {{632, 192, 58, 56}, KNOB_EG,        false, 128, "CC 71",   "Reso Lock"}
};
#define NUM_SIM_DIALS (sizeof(g_sim_dials) / sizeof(g_sim_dials[0]))

static SDL_Rect _pad_rect(int pad) {
    int row = pad / 8;
    int col = pad % 8;
    SDL_Rect r = {30 + col * 82, 480 + row * 102, 74, 88};
    return r;
}

static SDL_Rect g_xy_rect = {504, 262, 250, 80};
static SDL_Rect g_midi_btn_rect = {30, 12, 390, 24};

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    /* Initialise CoreMIDI Virtual Source + Output Port */
    if (MIDIClientCreate(CFSTR("CuartiTribe Brain"), NULL, NULL,
                         &g_midi_client) == noErr) {
        MIDISourceCreate(g_midi_client, CFSTR("CuartiTribe Brain Out"),
                         &g_midi_source);
        MIDIOutputPortCreate(g_midi_client, CFSTR("CuartiTribe OutPort"),
                             &g_midi_out_port);
    }
    _log_midi("READY    Virtual MIDI Port Active");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *win = SDL_CreateWindow(
        "CuartiTribe Brain — Korg Electribe 2 Sequencer Emulator",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 780, 725,
        SDL_WINDOW_SHOWN);
    SDL_Renderer *ren = SDL_CreateRenderer(
        win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    /* Initialise the actual Freetribe Brain Application */
    app_init();

    bool running = true;
    int active_dial = -1;
    int drag_start_y = 0;
    bool xy_dragging = false;
    int latched_pad = -1; /* Right-click latches pad for easy P-Lock editing */

    uint64_t last_ms = SDL_GetTicks64();

    while (running) {
        /* 1. Run 1ms hardware timer ticks accurately */
        uint64_t now_ms = SDL_GetTicks64();
        while (last_ms < now_ms) {
            if (g_tick_cb) {
                g_tick_cb();
            }
            last_ms++;
        }

        /* 2. Process SDL Events */
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                running = false;
            } else if (ev.type == SDL_KEYDOWN && !ev.key.repeat) {
                SDL_Keycode k = ev.key.keysym.sym;
                if (k == SDLK_SPACE) {
                    if (g_btn_cb) g_btn_cb(BTN_PLAY, true);
                } else if (k == SDLK_r) {
                    if (g_btn_cb) g_btn_cb(BTN_REC, true);
                } else if (k == SDLK_f) {
                    if (g_btn_cb) g_btn_cb(BTN_TAP, true);
                } else if (k == SDLK_LSHIFT || k == SDLK_RSHIFT) {
                    if (g_btn_cb) g_btn_cb(BTN_SHIFT, true);
                } else if (k == SDLK_LEFT) {
                    if (g_btn_cb) g_btn_cb(BTN_LEFT, true);
                } else if (k == SDLK_RIGHT) {
                    if (g_btn_cb) g_btn_cb(BTN_RIGHT, true);
                } else if (k == SDLK_LEFTBRACKET) {
                    if (g_btn_cb) g_btn_cb(BTN_BACK, true);
                } else if (k == SDLK_RIGHTBRACKET) {
                    if (g_btn_cb) g_btn_cb(BTN_FORWARD, true);
                } else if (k == SDLK_g) {
                    if (g_btn_cb) g_btn_cb(BTN_LPF, true); /* Aphex Gen */
                } else if (k == SDLK_h) {
                    if (g_btn_cb) g_btn_cb(BTN_HPF, true); /* Ambient Gen */
                } else if (k == SDLK_s) {
                    if (g_btn_cb) g_btn_cb(BTN_WRITE, true);
                }
                /* Keyboard shortcuts for Pads 1..16: 1-8 and Q-I */
                static const SDL_Keycode pad_keys[16] = {
                    SDLK_1, SDLK_2, SDLK_3, SDLK_4, SDLK_5, SDLK_6, SDLK_7, SDLK_8,
                    SDLK_q, SDLK_w, SDLK_e, SDLK_r, SDLK_t, SDLK_y, SDLK_u, SDLK_i
                };
                for (int p = 0; p < 16; p++) {
                    if (k == pad_keys[p] && k != SDLK_r) {
                        if (g_trig_cb) g_trig_cb((uint8_t)p, 110, true);
                    }
                }
            } else if (ev.type == SDL_KEYUP) {
                SDL_Keycode k = ev.key.keysym.sym;
                if (k == SDLK_f) {
                    if (g_btn_cb) g_btn_cb(BTN_TAP, false);
                } else if (k == SDLK_LSHIFT || k == SDLK_RSHIFT) {
                    if (g_btn_cb) g_btn_cb(BTN_SHIFT, false);
                }
                static const SDL_Keycode pad_keys[16] = {
                    SDLK_1, SDLK_2, SDLK_3, SDLK_4, SDLK_5, SDLK_6, SDLK_7, SDLK_8,
                    SDLK_q, SDLK_w, SDLK_e, SDLK_r, SDLK_t, SDLK_y, SDLK_u, SDLK_i
                };
                for (int p = 0; p < 16; p++) {
                    if (k == pad_keys[p] && k != SDLK_r) {
                        if (g_trig_cb) g_trig_cb((uint8_t)p, 0, false);
                    }
                }
            } else if (ev.type == SDL_MOUSEBUTTONDOWN) {
                SDL_Point pt = {ev.button.x, ev.button.y};

                if (SDL_PointInRect(&pt, &g_midi_btn_rect)) {
                    _cycle_midi_dest();
                }

                /* Check 16 Trigger Pads */
                for (int p = 0; p < 16; p++) {
                    SDL_Rect pr = _pad_rect(p);
                    if (SDL_PointInRect(&pt, &pr)) {
                        if (ev.button.button == SDL_BUTTON_RIGHT) {
                            /* Right-click toggles P-Lock latch on pad */
                            if (latched_pad == p) {
                                latched_pad = -1;
                                g_brain.plock_modified = true;
                                if (g_trig_cb) g_trig_cb((uint8_t)p, 0, false);
                            } else {
                                if (latched_pad >= 0 && g_trig_cb) {
                                    g_brain.plock_modified = true;
                                    g_trig_cb((uint8_t)latched_pad, 0, false);
                                }
                                latched_pad = p;
                                if (g_trig_cb) g_trig_cb((uint8_t)p, 110, true);
                            }
                        } else {
                            if (latched_pad >= 0) {
                                g_brain.plock_modified = true;
                                if (g_trig_cb) g_trig_cb((uint8_t)latched_pad, 0, false);
                                latched_pad = -1;
                            }
                            if (g_trig_cb) g_trig_cb((uint8_t)p, 110, true);
                        }
                    }
                }

                /* Check Panel Buttons */
                for (size_t b = 0; b < NUM_SIM_BUTTONS; b++) {
                    if (SDL_PointInRect(&pt, &g_sim_buttons[b].rect)) {
                        if (g_sim_buttons[b].btn_id == BTN_SHIFT) {
                            if (g_btn_cb) g_btn_cb(BTN_SHIFT, !g_brain.shift_held);
                        } else if (g_sim_buttons[b].btn_id == BTN_TAP) {
                            if (g_btn_cb) g_btn_cb(BTN_TAP, !g_brain.fill_active);
                        } else {
                            if (g_btn_cb) g_btn_cb(g_sim_buttons[b].btn_id, true);
                        }
                    }
                }

                /* Check Dials (Encoders & Knobs) */
                for (size_t d = 0; d < NUM_SIM_DIALS; d++) {
                    if (SDL_PointInRect(&pt, &g_sim_dials[d].rect)) {
                        active_dial = (int)d;
                        drag_start_y = ev.button.y;
                    }
                }

                /* Check X/Y Pad */
                if (SDL_PointInRect(&pt, &g_xy_rect)) {
                    xy_dragging = true;
                    uint32_t x = (uint32_t)((pt.x - g_xy_rect.x) * 127 / g_xy_rect.w);
                    uint32_t y = (uint32_t)((g_xy_rect.h - (pt.y - g_xy_rect.y)) * 127 / g_xy_rect.h);
                    if (g_xy_cb) g_xy_cb(x, y);
                }
            } else if (ev.type == SDL_MOUSEBUTTONUP) {
                SDL_Point pt = {ev.button.x, ev.button.y};
                if (ev.button.button == SDL_BUTTON_LEFT) {
                    for (int p = 0; p < 16; p++) {
                        SDL_Rect pr = _pad_rect(p);
                        if (SDL_PointInRect(&pt, &pr) && latched_pad != p) {
                            if (g_trig_cb) g_trig_cb((uint8_t)p, 0, false);
                        }
                    }
                }
                active_dial = -1;
                if (xy_dragging) {
                    xy_dragging = false;
                    g_brain.xy_active = false;
                }
            } else if (ev.type == SDL_MOUSEMOTION) {
                if (active_dial >= 0) {
                    int dy = drag_start_y - ev.motion.y;
                    if (abs(dy) >= 4) {
                        int steps = dy / 4;
                        drag_start_y = ev.motion.y;
                        t_sim_dial *dl = &g_sim_dials[active_dial];
                        if (dl->is_encoder) {
                            if (g_enc_cb) {
                                g_enc_cb(dl->id, steps > 0 ? 1 : -1);
                            }
                        } else {
                            int nv = (int)dl->value + steps * 4;
                            if (nv < 0) nv = 0;
                            if (nv > 255) nv = 255;
                            dl->value = (uint8_t)nv;
                            if (g_knob_cb) {
                                g_knob_cb(dl->id, dl->value);
                            }
                        }
                    }
                }
                if (xy_dragging) {
                    int rx = ev.motion.x - g_xy_rect.x;
                    int ry = ev.motion.y - g_xy_rect.y;
                    if (rx < 0) rx = 0;
                    if (rx > g_xy_rect.w) rx = g_xy_rect.w;
                    if (ry < 0) ry = 0;
                    if (ry > g_xy_rect.h) ry = g_xy_rect.h;
                    uint32_t x = (uint32_t)(rx * 127 / g_xy_rect.w);
                    uint32_t y = (uint32_t)((g_xy_rect.h - ry) * 127 / g_xy_rect.h);
                    if (g_xy_cb) g_xy_cb(x, y);
                }
            } else if (ev.type == SDL_MOUSEWHEEL) {
                int mx, my;
                SDL_GetMouseState(&mx, &my);
                SDL_Point pt = {mx, my};
                for (size_t d = 0; d < NUM_SIM_DIALS; d++) {
                    if (SDL_PointInRect(&pt, &g_sim_dials[d].rect)) {
                        t_sim_dial *dl = &g_sim_dials[d];
                        if (dl->is_encoder) {
                            if (g_enc_cb) {
                                g_enc_cb(dl->id, ev.wheel.y > 0 ? 1 : -1);
                            }
                        } else {
                            int nv = (int)dl->value + (ev.wheel.y > 0 ? 6 : -6);
                            if (nv < 0) nv = 0;
                            if (nv > 255) nv = 255;
                            dl->value = (uint8_t)nv;
                            if (g_knob_cb) {
                                g_knob_cb(dl->id, dl->value);
                            }
                        }
                    }
                }
            }
        }

        /* 3. Run Freetribe Application Loop */
        app_run();

        /* 4. Render Hardware Panel + Paper-Ink Companion Display */
        SDL_SetRenderDrawColor(ren, 26, 28, 34, 255);
        SDL_RenderClear(ren);

        SDL_Color c_white = {235, 238, 245, 255};
        SDL_Color c_muted = {145, 152, 168, 255};
        SDL_Color c_cyan  = {80,  200, 255, 255};

        /* MIDI Output Selector Button */
        SDL_SetRenderDrawColor(ren, 42, 46, 58, 255);
        SDL_RenderFillRect(ren, &g_midi_btn_rect);
        SDL_SetRenderDrawColor(ren, 80, 140, 220, 255);
        SDL_RenderDrawRect(ren, &g_midi_btn_rect);
        _draw_text(ren, g_midi_btn_rect.x + 8, g_midi_btn_rect.y + 8,
                   g_midi_dest_name, c_cyan, 1);
        _draw_text(ren, 435, 18,
                   "Right-Click Pad = Latch P-Lock | Drag/Wheel Knobs", c_muted, 1);

        /* --- 128x64 OLED Screen (3x Zoom: 384 x 192) --- */
        SDL_Rect oled_bezel = {26, 46, 396, 204};
        SDL_SetRenderDrawColor(ren, 12, 14, 18, 255);
        SDL_RenderFillRect(ren, &oled_bezel);
        SDL_SetRenderDrawColor(ren, 75, 85, 105, 255);
        SDL_RenderDrawRect(ren, &oled_bezel);

        for (int x = 0; x < 128; x++) {
            for (int y = 0; y < 64; y++) {
                if (g_oled_fb[x][y]) {
                    SDL_SetRenderDrawColor(ren, 195, 240, 255, 255);
                } else {
                    SDL_SetRenderDrawColor(ren, 18, 24, 32, 255);
                }
                SDL_Rect px = {32 + x * 3, 52 + y * 3, 2, 2};
                SDL_RenderFillRect(ren, &px);
            }
        }

        /* --- 4 Bar LEDs (BAR 1..4 with Red/Blue/Purple Bank Indication) --- */
        static const t_led_index bar_r[4] = {
            LED_BAR_0_RED, LED_BAR_1_RED, LED_BAR_2_RED, LED_BAR_3_RED
        };
        static const t_led_index bar_b[4] = {
            LED_BAR_0_BLUE, LED_BAR_1_BLUE, LED_BAR_2_BLUE, LED_BAR_3_BLUE
        };
        for (int b = 0; b < 4; b++) {
            SDL_Rect br = {30 + b * 98, 262, 86, 28};
            uint8_t r = g_leds[bar_r[b]];
            uint8_t bl = g_leds[bar_b[b]];
            SDL_SetRenderDrawColor(ren, 35 + (r * 220) / 255, 35,
                                   40 + (bl * 215) / 255, 255);
            SDL_RenderFillRect(ren, &br);
            SDL_SetRenderDrawColor(ren, 110, 120, 140, 255);
            SDL_RenderDrawRect(ren, &br);
            char blbl[16];
            snprintf(blbl, sizeof(blbl), "BAR %d (B%d)",
                     b + 1, (g_brain.current_bar / 4) * 4 + b + 1);
            _draw_text(ren, br.x + 10, br.y + 10, blbl, c_white, 1);
        }

        /* --- Encoders & Knobs --- */
        for (size_t d = 0; d < NUM_SIM_DIALS; d++) {
            t_sim_dial *dl = &g_sim_dials[d];
            SDL_SetRenderDrawColor(ren, dl->is_encoder ? 48 : 38,
                                   dl->is_encoder ? 54 : 42, 66, 255);
            SDL_RenderFillRect(ren, &dl->rect);
            SDL_SetRenderDrawColor(ren, dl->is_encoder ? 100 : 80,
                                   dl->is_encoder ? 175 : 120, 230, 255);
            SDL_RenderDrawRect(ren, &dl->rect);

            _draw_text(ren, dl->rect.x + 4, dl->rect.y + 6, dl->label, c_white, 1);
            _draw_text(ren, dl->rect.x + 4, dl->rect.y + 18, dl->sublabel, c_muted, 1);

            if (!dl->is_encoder) {
                /* Draw value bar inside knob */
                SDL_Rect vbar = {
                    dl->rect.x + 4, dl->rect.y + 38,
                    (dl->rect.w - 8) * dl->value / 255, 8
                };
                SDL_SetRenderDrawColor(ren, 90, 195, 255, 255);
                SDL_RenderFillRect(ren, &vbar);
            } else {
                _draw_text(ren, dl->rect.x + 8, dl->rect.y + 34, "[- / +]", c_cyan, 1);
            }
        }

        /* --- X/Y Kaoss Touchpad --- */
        SDL_SetRenderDrawColor(ren, 20, 24, 34, 255);
        SDL_RenderFillRect(ren, &g_xy_rect);
        SDL_SetRenderDrawColor(ren, 120, 90, 220, 255);
        SDL_RenderDrawRect(ren, &g_xy_rect);
        _draw_text(ren, g_xy_rect.x + 8, g_xy_rect.y + 6,
                   "X/Y KAOSS PAD (Ratchet Chaos & CC74/CC1)", c_muted, 1);
        int cx = g_xy_rect.x + (g_brain.xy_x * g_xy_rect.w) / 127;
        int cy = g_xy_rect.y + g_xy_rect.h - (g_brain.xy_y * g_xy_rect.h) / 127;
        SDL_SetRenderDrawColor(ren, 255, 90, 180, 255);
        SDL_RenderDrawLine(ren, cx, g_xy_rect.y, cx, g_xy_rect.y + g_xy_rect.h);
        SDL_RenderDrawLine(ren, g_xy_rect.x, cy, g_xy_rect.x + g_xy_rect.w, cy);

        /* --- Panel Buttons --- */
        for (size_t b = 0; b < NUM_SIM_BUTTONS; b++) {
            t_sim_button *bt = &g_sim_buttons[b];
            uint8_t led = (bt->led_id < LED_COUNT) ? g_leds[bt->led_id] : 0;
            SDL_SetRenderDrawColor(ren, 40 + (led * 180) / 255,
                                   44 + (led * 40) / 255,
                                   54 + (led * 160) / 255, 255);
            SDL_RenderFillRect(ren, &bt->rect);
            SDL_SetRenderDrawColor(ren, led ? 255 : 90, led ? 180 : 95,
                                   led ? 255 : 110, 255);
            SDL_RenderDrawRect(ren, &bt->rect);
            _draw_text(ren, bt->rect.x + 6, bt->rect.y + 12, bt->label, c_white, 1);
        }

        /* --- 16 Velocity RGB Trigger Pads --- */
        static const t_led_index pad_r[16] = {
            LED_PAD_0_RED,  LED_PAD_1_RED,  LED_PAD_2_RED,  LED_PAD_3_RED,
            LED_PAD_4_RED,  LED_PAD_5_RED,  LED_PAD_6_RED,  LED_PAD_7_RED,
            LED_PAD_8_RED,  LED_PAD_9_RED,  LED_PAD_10_RED, LED_PAD_11_RED,
            LED_PAD_12_RED, LED_PAD_13_RED, LED_PAD_14_RED, LED_PAD_15_RED
        };
        static const t_led_index pad_b[16] = {
            LED_PAD_0_BLUE,  LED_PAD_1_BLUE,  LED_PAD_2_BLUE,  LED_PAD_3_BLUE,
            LED_PAD_4_BLUE,  LED_PAD_5_BLUE,  LED_PAD_6_BLUE,  LED_PAD_7_BLUE,
            LED_PAD_8_BLUE,  LED_PAD_9_BLUE,  LED_PAD_10_BLUE, LED_PAD_11_BLUE,
            LED_PAD_12_BLUE, LED_PAD_13_BLUE, LED_PAD_14_BLUE, LED_PAD_15_BLUE
        };
        t_seq_track *cur_trk = &g_brain.tracks[g_brain.current_track];
        for (int p = 0; p < 16; p++) {
            SDL_Rect pr = _pad_rect(p);
            uint8_t r = g_leds[pad_r[p]];
            uint8_t bl = g_leds[pad_b[p]];
            SDL_SetRenderDrawColor(ren, 35 + (r * 215) / 255,
                                   38 + (bl * 70) / 255,
                                   45 + (bl * 210) / 255, 255);
            SDL_RenderFillRect(ren, &pr);
            SDL_SetRenderDrawColor(ren, (r || bl) ? 230 : 85,
                                   (r || bl) ? 200 : 90, 255, 255);
            SDL_RenderDrawRect(ren, &pr);

            char plbl[16];
            uint16_t s_idx = (uint16_t)(g_brain.current_bar * 16 + p);
            snprintf(plbl, sizeof(plbl), "PAD %02d", p + 1);
            _draw_text(ren, pr.x + 6, pr.y + 8, plbl, c_white, 1);

            if (g_brain.ui_mode == MODE_SEQUENCER) {
                t_seq_step *st = &cur_trk->steps[s_idx];
                char n_str[8];
                seq_note_name(st->note, n_str);
                _draw_text(ren, pr.x + 6, pr.y + 26, n_str, c_white, 1);
                uint8_t r_idx = st->cond_ratchet & 0x0F;
                if (r_idx > 0) {
                    static const uint8_t rm[6] = {1, 2, 3, 4, 6, 8};
                    snprintf(plbl, sizeof(plbl), "R:%dx", rm[r_idx % 6]);
                    _draw_text(ren, pr.x + 6, pr.y + 42, plbl, c_cyan, 1);
                }
                uint8_t cond = (st->cond_ratchet >> 4) & 0x0F;
                if (cond > 0 || st->probability < 100) {
                    snprintf(plbl, sizeof(plbl), "%s %d%%",
                             seq_cond_name(cond), st->probability);
                    _draw_text(ren, pr.x + 6, pr.y + 56, plbl, c_muted, 1);
                }
            } else if (g_brain.ui_mode == MODE_GENERATIVE) {
                static const char *gen_lbl[16] = {
                    "Euc 3/8", "Euc 5/16", "Euc 7/16", "Euc11/16",
                    "APHEX!",  "AMBIENT!", "Tur 20%",  "Tur 70%",
                    "FWD ->",  "<- REV",   "PEND <>",  "DRNK ?~",
                    "RND ??",  "CLK -",    "CLK +",    "CLEAR"
                };
                _draw_text(ren, pr.x + 6, pr.y + 36, gen_lbl[p], c_white, 1);
            }
        }

        /* Compact Bottom MIDI Output Monitor Bar */
        SDL_Rect m_bar = {30, 682, 724, 32};
        SDL_SetRenderDrawColor(ren, 18, 20, 26, 255);
        SDL_RenderFillRect(ren, &m_bar);
        SDL_SetRenderDrawColor(ren, 65, 72, 90, 255);
        SDL_RenderDrawRect(ren, &m_bar);
        int last_idx = (g_midi_log_head + MIDI_LOG_LINES - 1) % MIDI_LOG_LINES;
        int prev_idx = (g_midi_log_head + MIDI_LOG_LINES - 2) % MIDI_LOG_LINES;
        _draw_text(ren, 40, 688, "MIDI OUT:", c_cyan, 1);
        _draw_text(ren, 106, 688, g_midi_log[last_idx], c_white, 1);
        _draw_text(ren, 430, 688, g_midi_log[prev_idx], c_muted, 1);

        SDL_RenderPresent(ren);
    }

    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
