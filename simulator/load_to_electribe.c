/*----------------------------------------------------------------------
 * CuartiTribe Brain - Safe RAM Uploader for Korg Electribe 2 (CoreMIDI)
 * Native C replacement for scripts/execute_freetribe.py (no Python deps).
 * Uploads cpu.bin into volatile CPU RAM (0x80000000) via TRS MIDI In/Out.
 *----------------------------------------------------------------------*/

#include <CoreFoundation/CoreFoundation.h>
#include <CoreMIDI/CoreMIDI.h>
#include <mach/mach_time.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile bool g_sysex_received = false;
static uint8_t g_rx_buf[1024];
static size_t g_rx_len = 0;
static uint8_t g_work_buf[1024];
static size_t g_work_len = 0;

static void _midi_read_proc(const MIDIPacketList *pktlist, void *refCon,
                            void *connRefCon) {
    (void)refCon;
    (void)connRefCon;
    const MIDIPacket *pkt = &pktlist->packet[0];
    for (UInt32 i = 0; i < pktlist->numPackets; i++) {
        for (UInt16 b = 0; b < pkt->length; b++) {
            uint8_t byte = pkt->data[b];
            if (byte >= 0xF8) {
                continue; /* Ignore interleaved MIDI Clock / Active Sensing */
            }
            if (byte == 0xF0) {
                g_work_len = 0;
                g_work_buf[g_work_len++] = byte;
            } else if (g_work_len > 0 && g_work_len < sizeof(g_work_buf)) {
                g_work_buf[g_work_len++] = byte;
                if (byte == 0xF7) {
                    memcpy(g_rx_buf, g_work_buf, g_work_len);
                    g_rx_len = g_work_len;
                    g_sysex_received = true;
                    g_work_len = 0;
                }
            }
        }
        pkt = MIDIPacketNext(pkt);
    }
}

/* Korg 7-bit SysEx encoding (matches scripts/e2_syx_codec.py) */
static size_t _syx_enc(const uint8_t *src, size_t len, uint8_t *dst) {
    size_t out_idx = 0;
    size_t i = 0;
    while (i < len) {
        size_t chunk = (len - i >= 7) ? 7 : (len - i);
        uint8_t msb = 0;
        for (size_t j = 0; j < chunk; j++) {
            if (src[i + j] & 0x80) {
                msb |= (1U << j);
            }
        }
        dst[out_idx++] = msb;
        for (size_t j = 0; j < chunk; j++) {
            dst[out_idx++] = src[i + j] & 0x7F;
        }
        i += chunk;
    }
    return out_idx;
}

static bool _send_and_wait(MIDIPortRef out_port, MIDIEndpointRef dest,
                           const uint8_t *sysex, size_t len, int timeout_ms) {
    g_sysex_received = false;
    Byte pkt_buf[1024];
    MIDIPacketList *pl = (MIDIPacketList *)pkt_buf;
    MIDIPacket *p = MIDIPacketListInit(pl);
    p = MIDIPacketListAdd(pl, sizeof(pkt_buf), p, 0, len, sysex);
    if (!p) {
        return false;
    }
    MIDISend(out_port, dest, pl);

    int waited = 0;
    while (!g_sysex_received && waited < timeout_ms) {
        usleep(1000);
        waited++;
    }
    return g_sysex_received;
}

static void _get_endpoint_name(MIDIEndpointRef ep, char *buf, size_t maxlen) {
    CFStringRef str = NULL;
    MIDIObjectGetStringProperty(ep, kMIDIPropertyName, &str);
    if (str) {
        CFStringGetCString(str, buf, (CFIndex)maxlen, kCFStringEncodingUTF8);
        CFRelease(str);
    } else {
        snprintf(buf, maxlen, "Unknown Port");
    }
}

int main(int argc, char *argv[]) {
    const char *bin_path = "freetribe/cpu/build/cpu.bin";
    if (argc >= 2) {
        bin_path = argv[1];
    }

    FILE *f = fopen(bin_path, "rb");
    if (!f) {
        bin_path = "../freetribe/cpu/build/cpu.bin";
        f = fopen(bin_path, "rb");
    }
    if (!f) {
        fprintf(stderr, "Error: No se encontro el archivo %s\n", bin_path);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *code = (uint8_t *) calloc(1, (size_t)file_size + 256);
    fread(code, 1, (size_t)file_size, f);
    fclose(f);

    MIDIClientRef client = 0;
    MIDIPortRef in_port = 0, out_port = 0;
    MIDIClientCreate(CFSTR("CuartiTribe Loader"), NULL, NULL, &client);
    MIDIInputPortCreate(client, CFSTR("In"), _midi_read_proc, NULL, &in_port);
    MIDIOutputPortCreate(client, CFSTR("Out"), &out_port);

    ItemCount num_src = MIDIGetNumberOfSources();
    ItemCount num_dst = MIDIGetNumberOfDestinations();

    printf("\n=== CuartiTribe Brain — Cargador Seguro en RAM (0%% Flash Risk) ===\n");
    printf("Archivo: %s (%ld bytes)\n\n", bin_path, file_size);

    if (num_src == 0 || num_dst == 0) {
        printf("No se detectaron interfaces MIDI físicas conectadas a la Mac.\n");
        printf("Recordá conectar MIDI IN y MIDI OUT (por los jacks TRS) entre tu interfaz MIDI y la Electribe 2 (con Hacktribe encendido).\n");
        return 1;
    }

    printf("Puertos MIDI IN disponibles:\n");
    for (ItemCount i = 0; i < num_src; i++) {
        char name[128];
        _get_endpoint_name(MIDIGetSource(i), name, sizeof(name));
        printf("  [%lu] %s\n", (unsigned long)i, name);
    }

    printf("\nPuertos MIDI OUT disponibles:\n");
    for (ItemCount i = 0; i < num_dst; i++) {
        char name[128];
        _get_endpoint_name(MIDIGetDestination(i), name, sizeof(name));
        printf("  [%lu] %s\n", (unsigned long)i, name);
    }

    int src_idx = 0, dst_idx = 0;
    if (argc >= 4) {
        src_idx = atoi(argv[2]);
        dst_idx = atoi(argv[3]);
    } else if (num_src > 1 || num_dst > 1) {
        printf("\nElegí número de puerto MIDI IN: ");
        fflush(stdout);
        if (scanf("%d", &src_idx) != 1) return 1;
        printf("Elegí número de puerto MIDI OUT: ");
        fflush(stdout);
        if (scanf("%d", &dst_idx) != 1) return 1;
    }

    MIDIEndpointRef src_ep = MIDIGetSource((ItemCount)src_idx);
    MIDIEndpointRef dst_ep = MIDIGetDestination((ItemCount)dst_idx);
    MIDIPortConnectSource(in_port, src_ep, NULL);
    usleep(300000);

    /* 1. Search device */
    printf("\n1. Buscando Electribe 2 (Hacktribe)...\n");
    uint8_t search_msg[] = {0xF0, 0x42, 0x50, 0x00, 0x00, 0xF7};
    if (!_send_and_wait(out_port, dst_ep, search_msg, sizeof(search_msg), 2000)) {
        printf("ERROR: La Electribe no respondió al Search Device.\n");
        printf("Verificá que esté encendida en Hacktribe y que los cables TRS MIDI In y Out estén ambos conectados.\n");
        return 1;
    }
    uint8_t global_ch = (g_rx_len > 4) ? (g_rx_buf[4] & 0x0F) : 0;
    uint8_t dev_id = (g_rx_len > 6) ? g_rx_buf[6] : 0x24;
    printf("   Electribe detectada! (Global Ch: %d, ID: 0x%02X)\n",
           global_ch + 1, dev_id);

    /* 2. Pivot to Freetribe Loader (SysEx 0x58) */
    printf("2. Activando Freetribe RAM Loader (0x58)...\n");
    uint8_t zeros[8] = {0};
    uint8_t enc_zeros[16];
    size_t enc_len = _syx_enc(zeros, 8, enc_zeros);
    uint8_t pivot_msg[32] = {0xF0, 0x42, (uint8_t)(0x30 + global_ch),
                             0x00, 0x01, dev_id, 0x58};
    memcpy(pivot_msg + 7, enc_zeros, enc_len);
    pivot_msg[7 + enc_len] = 0xF7;
    _send_and_wait(out_port, dst_ep, pivot_msg, 8 + enc_len, 1000);
    sleep(1);

    /* 3. Send Magic Word (0x64 0x01 0x23 0x45 0x67) */
    printf("3. Verificando handshake con el RAM Loader...\n");
    uint8_t magic_msg[] = {0xF0, 0x42, 0x30, 0x00, 0x01, 0x24,
                           0x64, 0x01, 0x23, 0x45, 0x67, 0xF7};
    if (!_send_and_wait(out_port, dst_ep, magic_msg, sizeof(magic_msg), 2000)) {
        printf("ERROR: Sin respuesta del Freetribe Loader (recordá que requiere MIDI por los puertos físicos TRS In/Out, no USB).\n");
        return 1;
    }
    if (g_rx_len < 9 || g_rx_buf[5] != 0x76 || g_rx_buf[6] != 0x54 ||
        g_rx_buf[7] != 0x32 || g_rx_buf[8] != 0x10) {
        printf("ERROR: Respuesta inesperada del loader.\n");
        return 1;
    }
    printf("   Handshake OK (76 54 32 10)!\n");

    /* 4. Upload 256-byte chunks to RAM (0x54) */
    size_t num_chunks = ((size_t)file_size + 255) / 256;
    printf("4. Subiendo %zu bloques de 256 bytes a la RAM (0x80000000)...\n",
           num_chunks);

    for (size_t c = 0; c < num_chunks; c++) {
        uint8_t enc_chunk[300];
        size_t elen = _syx_enc(code + c * 256, 256, enc_chunk);
        uint8_t load_msg[320] = {0xF0, 0x42, 0x30, 0x00, 0x01, 0x24, 0x54};
        memcpy(load_msg + 7, enc_chunk, elen);
        load_msg[7 + elen] = 0xF7;

        if (!_send_and_wait(out_port, dst_ep, load_msg, 8 + elen, 2000) ||
            g_rx_buf[5] != 0x21) {
            printf("\nERROR al subir el bloque %zu.\n", c);
            return 1;
        }
        printf("\r   Progreso: [%zu / %zu] (%zu%%)", c + 1, num_chunks,
               ((c + 1) * 100) / num_chunks);
        fflush(stdout);
    }

    /* 5. Execute CPU RAM at 0x80000000 (0x57) */
    printf("\n5. Ejecutando CuartiTribe Brain en RAM (0x80000000)...\n");
    uint8_t exec_payload[8] = {0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00};
    uint8_t enc_exec[16];
    size_t ex_len = _syx_enc(exec_payload, 8, enc_exec);
    uint8_t exec_msg[32] = {0xF0, 0x42, 0x30, 0x00, 0x01, 0x24, 0x57};
    memcpy(exec_msg + 7, enc_exec, ex_len);
    exec_msg[7 + ex_len] = 0xF7;
    _send_and_wait(out_port, dst_ep, exec_msg, 8 + ex_len, 2000);

    printf("\n¡LISTO! CuartiTribe Brain está corriendo en la RAM de tu Electribe 2.\n");
    printf("Para volver a Hacktribe normal en cualquier momento, simplemente apagá y volvé a encender la Electribe.\n\n");
    free(code);
    return 0;
}
