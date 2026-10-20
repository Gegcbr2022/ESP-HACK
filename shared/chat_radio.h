#ifndef ESPHACK_CHAT_RADIO_H
#define ESPHACK_CHAT_RADIO_H
#include <stddef.h>
#include <stdint.h>

/* CC1101 26MHz crystal. See TI SWRS061, sections 13-17 and register descriptions.
 * 2-FSK, 9595.87bit/s, deviation19042.97Hz, bandwidth101562.5Hz, sync D391,
 * 4-byte preamble, 30/32-bit sync qualifier, whitening, variable-length + CRC16.
 * End of RX/TX returns IDLE; caller must flush/read and explicitly rearm RX.
 * Reg0 is deliberately omitted: Flipper's exported preset parser uses address0
 * as terminator. IOCFG0 is the packet sync/EOP signal; IOCFG2 stays reset default.
 */
static const uint8_t chat_radio_registers[] = {
    0x01,0x2E, 0x02,0x06, 0x03,0x07, 0x04,0xD3, 0x05,0x91,
    0x06,0x3C, 0x07,0x0C, 0x08,0x45, 0x09,0x00, 0x0A,0x00,
    0x0B,0x06, 0x0C,0x00, 0x10,0xCA, 0x11,0x83, 0x12,0x03,
    0x13,0x22, 0x14,0xF8, 0x15,0x34, 0x16,0x07, 0x17,0x00,
    0x18,0x18, 0x19,0x16, 0x1A,0x6C, 0x1B,0x43, 0x1C,0x40,
    0x1D,0x91, 0x21,0x56, 0x22,0x10, 0x23,0xE9, 0x24,0x2A,
    0x25,0x00, 0x26,0x1F, 0x29,0x59, 0x2C,0x81, 0x2D,0x35,
    0x2E,0x09,
};
#define CHAT_RADIO_REGISTER_BYTES (sizeof(chat_radio_registers))
/* These are laboratory choices, not a declaration of legality in any country. */
static const uint32_t chat_radio_frequencies[] = {433920000u, 868350000u};
/* TI SWRS061I table39, multilayer inductors; conducted/ERP power still needs measurement. */
static inline uint8_t chat_radio_pa(uint32_t frequency, uint8_t power_index) {
    static const uint8_t pa433[] = {0x34, 0x60, 0xC0}; /* -10, 0, +10dBm nominal */
    static const uint8_t pa868[] = {0x27, 0x50, 0xC2};
    return (frequency < 600000000u ? pa433 : pa868)[power_index > 2 ? 0 : power_index];
}
#endif
