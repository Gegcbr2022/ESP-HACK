#ifndef ESPHACK_FLIPPER_CHAT_COMPAT_H
#define ESPHACK_FLIPPER_CHAT_COMPAT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Plaintext wire stream used by the official CLI and Enhanced Sub-GHz Chat.
 * No ESP-HACK frame header, ACK, addressing, retry or application CRC is added.
 * Source/profile pins and local receiver-only differences are documented in
 * docs/FLIPPER_CHAT_COMPATIBILITY.md. All storage and per-call work are bounded.
 */
#define FLIPPER_CHAT_TEXT 160u
#define FLIPPER_CHAT_PACKET 60u
#define FLIPPER_CHAT_QUIET_MS 500u
#define FLIPPER_CHAT_WINDOW_MS 4000u
#define FLIPPER_CHAT_TX_MS 10000u

static const uint8_t flipper_chat_registers[] = {
    0x01,0x2E, 0x02,0x06, 0x03,0x47, 0x04,0x46, 0x05,0x4C,
    0x06,0x3C, 0x07,0x0C, 0x08,0x05, 0x09,0x00, 0x0A,0x00,
    0x0B,0x06, 0x0C,0x00, 0x10,0xC8, 0x11,0x93, 0x12,0x12,
    0x13,0x22, 0x14,0xF8, 0x15,0x34, 0x17,0x30, 0x18,0x18,
    0x19,0x16, 0x1B,0x43, 0x1C,0x40, 0x1D,0x91, 0x20,0xFB,
    0x21,0x56, 0x22,0x10,
};
#define FLIPPER_CHAT_REGISTER_BYTES (sizeof(flipper_chat_registers))

typedef struct { char text[FLIPPER_CHAT_TEXT + 1]; bool truncated; } FlipperChatMessage;
typedef struct {
    char text[FLIPPER_CHAT_TEXT + 1];
    uint16_t length;
    uint8_t escape, escape_bytes;
    uint32_t started, last;
    bool active, truncated, invalid;
    uint32_t rejected, truncations;
} FlipperChatRx;

static inline void flipper_chat_rx_init(FlipperChatRx* s) { memset(s, 0, sizeof(*s)); }
static inline void flipper_chat_rx_clear(FlipperChatRx* s) {
    s->length = 0; s->escape = s->escape_bytes = 0;
    s->active = s->truncated = s->invalid = false;
}
static inline bool flipper_chat_rx_finish(FlipperChatRx* s, FlipperChatMessage* message) {
    bool ready = s->active && s->length && !s->invalid && !s->escape;
    if(ready) {
        s->text[s->length] = 0;
        if(s->truncated) {
            static const char suffix[] = " [truncated]";
            const size_t suffix_length = sizeof(suffix) - 1;
            memcpy(s->text + FLIPPER_CHAT_TEXT - suffix_length, suffix, sizeof(suffix));
            ++s->truncations;
        }
        memcpy(message->text, s->text, s->length + 1); message->truncated = s->truncated;
    } else if(s->active && (s->invalid || s->escape)) ++s->rejected;
    flipper_chat_rx_clear(s); return ready;
}
/* Call poll before feeding a new RF packet, and on every worker iteration.
 * CRLF completes a line; 500ms idle also completes a legacy terminal fragment.
 * Continuous streams exceeding4s are rejected until a line/quiet boundary.
 */
static inline bool flipper_chat_rx_poll(FlipperChatRx* s, uint32_t now, FlipperChatMessage* message) {
    if(!s->active) return false;
    if((uint32_t)(now - s->last) >= FLIPPER_CHAT_QUIET_MS) return flipper_chat_rx_finish(s, message);
    if((uint32_t)(now - s->started) >= FLIPPER_CHAT_WINDOW_MS) s->invalid = true;
    return false;
}
static inline bool flipper_chat_rx_byte(FlipperChatRx* s, uint8_t byte, uint32_t now, FlipperChatMessage* message) {
    if(!s->active) { s->active = true; s->started = now; }
    s->last = now;
    if((uint32_t)(now - s->started) >= FLIPPER_CHAT_WINDOW_MS) s->invalid = true;
    if(byte == '\n') return flipper_chat_rx_finish(s, message);
    if(s->invalid) return false;
    // CLI SGR escapes can cross RF packet boundaries. Only CSI is accepted;
    // escape length is capped, and incomplete/other control strings are rejected.
    if(s->escape) {
        if(++s->escape_bytes > 24) { s->invalid = true; return false; }
        if(s->escape == 1) {
            if(byte == '[') s->escape = 2; else s->invalid = true;
        } else if(byte >= 0x40 && byte <= 0x7E) s->escape = s->escape_bytes = 0;
        else if(byte < 0x20 || byte > 0x3F) s->invalid = true;
        return false;
    }
    if(byte == 0x1B) { s->escape = 1; s->escape_bytes = 0; return false; }
    if(byte == '\r' || byte == 0) return false; // Some older ESP bridges append NUL padding.
    if(byte == '\t') byte = ' ';
    if(byte < 32 || byte > 126) { s->invalid = true; return false; }
    if(s->length < FLIPPER_CHAT_TEXT) s->text[s->length++] = (char)byte;
    else s->truncated = true;
    return false;
}

typedef enum { FlipperChatIdle, FlipperChatQueued, FlipperChatSending, FlipperChatSent, FlipperChatFailed } FlipperChatDelivery;
typedef struct {
    uint8_t wire[FLIPPER_CHAT_TEXT + 14]; // ESP-HACK: <text> CRLF, at most172bytes.
    uint16_t length, offset, pending;
    uint32_t started;
    FlipperChatDelivery delivery;
} FlipperChatTx;
static inline void flipper_chat_tx_init(FlipperChatTx* s) { memset(s, 0, sizeof(*s)); }
static inline bool flipper_chat_tx_queue(FlipperChatTx* s, const char* text, size_t length, uint32_t now) {
    static const char prefix[] = "ESP-HACK: ";
    if(!text || !length || length > FLIPPER_CHAT_TEXT || s->delivery == FlipperChatQueued || s->delivery == FlipperChatSending)
        return false;
    for(size_t i = 0; i < length; ++i) if((uint8_t)text[i] < 32 || (uint8_t)text[i] > 126) return false;
    memcpy(s->wire, prefix, sizeof(prefix) - 1);
    memcpy(s->wire + sizeof(prefix) - 1, text, length);
    s->length = (uint16_t)(sizeof(prefix) - 1 + length);
    s->wire[s->length++] = '\r'; s->wire[s->length++] = '\n';
    s->offset = s->pending = 0; s->started = now; s->delivery = FlipperChatQueued; return true;
}
/* Poll never retries a failed RF chunk, since the peer protocol has no dedup or ACK. */
static inline size_t flipper_chat_tx_poll(FlipperChatTx* s, uint32_t now, bool busy, uint8_t* packet, size_t capacity) {
    if(s->delivery != FlipperChatQueued && s->delivery != FlipperChatSending) return 0;
    if((uint32_t)(now - s->started) >= FLIPPER_CHAT_TX_MS) { s->pending = 0; s->delivery = FlipperChatFailed; return 0; }
    if(busy || s->pending || !packet) return 0;
    size_t length = s->length - s->offset;
    if(length > FLIPPER_CHAT_PACKET) length = FLIPPER_CHAT_PACKET;
    if(length > capacity) return 0;
    memcpy(packet, s->wire + s->offset, length); s->pending = (uint16_t)length;
    s->delivery = FlipperChatSending; return length;
}
static inline void flipper_chat_tx_result(FlipperChatTx* s, bool success) {
    if(!s->pending || s->delivery != FlipperChatSending) return;
    if(!success) { s->delivery = FlipperChatFailed; s->pending = 0; return; }
    s->offset += s->pending; s->pending = 0;
    s->delivery = s->offset == s->length ? FlipperChatSent : FlipperChatQueued;
}
#endif
