/*----------------------------------------------------------------------
 * CuartiTribe Brain - Interactive TRS MIDI Cable & Adapter Checker
 * Diagnoses both directions:
 *   1. Electribe MIDI OUT -> Interface MIDI IN
 *   2. Interface MIDI OUT -> Electribe MIDI IN (SysEx Handshake + Visual Note Trigger)
 *----------------------------------------------------------------------*/

#include <CoreFoundation/CoreFoundation.h>
#include <CoreMIDI/CoreMIDI.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile int g_rx_total_bytes = 0;
static volatile int g_rx_note_or_cc = 0;
static volatile int g_rx_clock = 0;
static volatile bool g_sysex_reply_ok = false;
static uint8_t g_sysex_buf[256];
static size_t g_sysex_len = 0;
static uint8_t g_last_msg[3];

static void _midi_in_cb(const MIDIPacketList *pktlist, void *refCon,
                        void *connRefCon) {
    (void)refCon;
    (void)connRefCon;
    const MIDIPacket *pkt = &pktlist->packet[0];
    for (UInt32 i = 0; i < pktlist->numPackets; i++) {
        for (UInt16 b = 0; b < pkt->length; b++) {
            uint8_t byte = pkt->data[b];
            g_rx_total_bytes++;

            if (byte == 0xF8 || byte == 0xFA || byte == 0xFC || byte == 0xFE) {
                g_rx_clock++;
            } else if ((byte & 0xF0) == 0x90 || (byte & 0xF0) == 0x80 ||
                       (byte & 0xF0) == 0xB0) {
                g_rx_note_or_cc++;
                g_last_msg[0] = byte;
                if (b + 1 < pkt->length) g_last_msg[1] = pkt->data[b + 1];
                if (b + 2 < pkt->length) g_last_msg[2] = pkt->data[b + 2];
            }

            if (byte == 0xF0) {
                g_sysex_len = 0;
                g_sysex_buf[g_sysex_len++] = byte;
            } else if (g_sysex_len > 0 && g_sysex_len < sizeof(g_sysex_buf)) {
                g_sysex_buf[g_sysex_len++] = byte;
                if (byte == 0xF7) {
                    if (g_sysex_len >= 6 &&
                        ((g_sysex_buf[1] == 0x42 && g_sysex_buf[2] == 0x50) ||
                         (g_sysex_buf[1] == 0x7E))) {
                        g_sysex_reply_ok = true;
                    }
                    g_sysex_len = 0;
                }
            }
        }
        pkt = MIDIPacketNext(pkt);
    }
}

static void _send_bytes(MIDIPortRef out_port, MIDIEndpointRef dest,
                        const uint8_t *data, size_t len) {
    Byte buf[512];
    MIDIPacketList *pl = (MIDIPacketList *)buf;
    MIDIPacket *p = MIDIPacketListInit(pl);
    p = MIDIPacketListAdd(pl, sizeof(buf), p, 0, len, data);
    if (p) {
        MIDISend(out_port, dest, pl);
    }
}

static void _get_name(MIDIEndpointRef ep, char *buf, size_t maxlen) {
    CFStringRef str = NULL;
    MIDIObjectGetStringProperty(ep, kMIDIPropertyName, &str);
    if (str) {
        CFStringGetCString(str, buf, (CFIndex)maxlen, kCFStringEncodingUTF8);
        CFRelease(str);
    } else {
        snprintf(buf, maxlen, "Unknown");
    }
}

int main(int argc, char *argv[]) {
    int listen_secs = 8;
    if (argc >= 2) {
        listen_secs = atoi(argv[1]);
        if (listen_secs < 2) listen_secs = 2;
    }

    MIDIClientRef client = 0;
    MIDIPortRef in_port = 0, out_port = 0;
    MIDIClientCreate(CFSTR("CuartiTribe MIDI Checker"), NULL, NULL, &client);
    MIDIInputPortCreate(client, CFSTR("In"), _midi_in_cb, NULL, &in_port);
    MIDIOutputPortCreate(client, CFSTR("Out"), &out_port);

    ItemCount num_src = MIDIGetNumberOfSources();
    ItemCount num_dst = MIDIGetNumberOfDestinations();

    printf("\n=================================================================\n");
    printf("   CuartiTribe — Diagnóstico de Cables y Adaptadores TRS-MIDI\n");
    printf("=================================================================\n");
    printf("NOTA SOBRE ADAPTADORES TRS-MIDI:\n");
    printf("  • Korg Electribe 2 usa estándar TIPO A (Korg / Akai / Make Noise).\n");
    printf("  • Arturia (BeatStep Pro) y Novation usan TIPO B (Tip y Ring invertidos).\n");
    printf("    Si usás un adaptador de BeatStep Pro en la Electribe, NO pasa señal.\n\n");

    if (num_src == 0 || num_dst == 0) {
        printf("[ERROR] No se encontró ninguna interfaz MIDI conectada por USB a la Mac.\n");
        return 1;
    }

    MIDIEndpointRef src_ep = MIDIGetSource(0);
    MIDIEndpointRef dst_ep = MIDIGetDestination(0);
    for (ItemCount i = 0; i < num_src; i++) {
        char name[128];
        _get_name(MIDIGetSource(i), name, sizeof(name));
        if (strstr(name, "CuartiTribe") == NULL) {
            src_ep = MIDIGetSource(i);
            break;
        }
    }
    for (ItemCount i = 0; i < num_dst; i++) {
        char name[128];
        _get_name(MIDIGetDestination(i), name, sizeof(name));
        if (strstr(name, "CuartiTribe") == NULL) {
            dst_ep = MIDIGetDestination(i);
            break;
        }
    }

    char in_name[128], out_name[128];
    _get_name(src_ep, in_name, sizeof(in_name));
    _get_name(dst_ep, out_name, sizeof(out_name));
    MIDIPortConnectSource(in_port, src_ep, NULL);

    printf("Interfaz MIDI seleccionada:\n");
    printf("  • MIDI IN : %s\n", in_name);
    printf("  • MIDI OUT: %s\n\n", out_name);

    /* --- PRUEBA 1: Ping SysEx bidireccional + Disparo de Notas --- */
    printf("--> PRUEBA 1: Enviando Ping SysEx + Notas MIDI (Scarlett OUT -> Electribe IN)...\n");
    uint8_t korg_search[] = {0xF0, 0x42, 0x50, 0x00, 0x00, 0xF7};
    uint8_t univ_inq[]    = {0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7};
    _send_bytes(out_port, dst_ep, korg_search, sizeof(korg_search));
    usleep(100000);
    _send_bytes(out_port, dst_ep, univ_inq, sizeof(univ_inq));
    usleep(300000);

    if (g_sysex_reply_ok) {
        printf("\n  [OK TOTAL!] La Electribe 2 respondió al Ping SysEx!\n");
        printf("  Ambos adaptadores TRS (MIDI IN y MIDI OUT) son los correctos (Tipo A)\n");
        printf("  y la comunicación bidireccional está 100%% lista para cargar CuartiTribe.\n\n");
        return 0;
    }

    printf("    (Todavía no hubo respuesta automática por SysEx).\n\n");
    printf("--> PRUEBA 2: Diagnóstico separado de cada adaptador (%d segundos):\n",
           listen_secs);
    printf("    1) Para probar [Scarlett OUT -> Electribe IN]:\n");
    printf("       Estoy disparando notas C4 en el Canal 1 (Parte 1 de la Electribe).\n");
    printf("       Mirá si el Pad 1 de la Electribe se ilumina o suena!\n");
    printf("    2) Para probar [Electribe OUT -> Scarlett IN]:\n");
    printf("       Tocá AHORA varios Pads, dale PLAY o mové una perilla en la Electribe...\n\n");

    for (int s = 0; s < listen_secs * 4; s++) {
        /* Send a test note on Ch 1 (Part 1) every 500ms so user sees Pad 1 light up on Electribe */
        if ((s % 2) == 0) {
            uint8_t note_on[] = {0x90, 60, 110};
            _send_bytes(out_port, dst_ep, note_on, 3);
        } else {
            uint8_t note_off[] = {0x80, 60, 0};
            _send_bytes(out_port, dst_ep, note_off, 3);
            _send_bytes(out_port, dst_ep, korg_search, sizeof(korg_search));
        }

        usleep(250000);

        printf("\r    Escuchando... Bytes recibidos: %d (Notas/CC: %d | Clock: %d | SysEx: %s)   ",
               g_rx_total_bytes, g_rx_note_or_cc, g_rx_clock,
               g_sysex_reply_ok ? "SI!" : "NO");
        fflush(stdout);

        if (g_sysex_reply_ok) {
            break;
        }
    }
    printf("\n\n=== RESULTADO DEL DIAGNÓSTICO ===\n");

    if (g_sysex_reply_ok) {
        printf("✅ [OK BIDIRECCIONAL] ¡Ambos adaptadores y cables funcionan perfecto!\n");
        return 0;
    } else if (g_rx_total_bytes > 0) {
        printf("✅ [Electribe MIDI OUT -> Scarlett MIDI IN]: FUNCIONA (recibimos %d bytes desde la Electribe).\n",
               g_rx_total_bytes);
        printf("❌ [Scarlett MIDI OUT -> Electribe MIDI IN]: La Electribe no respondió al SysEx.\n");
        printf("   Posibles causas en el lado MIDI IN de la Electribe:\n");
        printf("   1. El adaptador conectado en 'MIDI IN' de la Electribe es Tipo B (ej. de Arturia/Novation) en vez de Tipo A (Korg).\n");
        printf("   2. O el cable MIDI de Scarlett OUT -> Electribe IN está desconectado/invertido.\n");
        printf("   3. O en el menú de la Electribe (GLOBAL -> MIDI Receive Filter) está filtrado SysEx.\n\n");
        return 2;
    } else {
        printf("❌ [Electribe MIDI OUT -> Scarlett MIDI IN]: No entró ningún dato (0 bytes) desde la Electribe.\n");
        printf("   • Si tocaste pads/Play en la Electribe y llegó 0 bytes, el adaptador en 'MIDI OUT' de la Electribe\n");
        printf("     NO es Tipo A (probablemente es Tipo B de un BeatStep Pro / Arturia), o los cables IN/OUT están cruzados.\n");
        printf("   • Si viste que el Pad 1 de la Electribe parpadeó durante la prueba, entonces UNO de tus adaptadores\n");
        printf("     (el que pusiste en MIDI IN) SÍ es Tipo A y el otro no!\n\n");
        return 1;
    }
}
