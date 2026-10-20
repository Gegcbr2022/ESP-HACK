#ifndef ESPHACK_CHAT_PROTOCOL_H
#define ESPHACK_CHAT_PROTOCOL_H

/* Portable wire format. No packed structures, heap, endianness or SDK dependencies. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define CHAT_VERSION 1u
#define CHAT_MAGIC 0xECu
#define CHAT_HEADER_SIZE 18u
#define CHAT_FRAGMENT_BYTES 40u
#define CHAT_MAX_FRAGMENTS 4u
#define CHAT_MAX_TEXT (CHAT_FRAGMENT_BYTES * CHAT_MAX_FRAGMENTS)
#define CHAT_MAX_FRAME 60u
#define CHAT_BROADCAST 0xFFFFu

typedef enum { ChatDiscovery = 1, ChatText = 2, ChatAck = 3 } ChatPacketType;
typedef struct {
    uint8_t type;
    uint16_t source, destination, session;
    uint32_t message;
    uint8_t fragment, fragments, length;
    uint8_t payload[CHAT_FRAGMENT_BYTES];
} ChatPacket;

static inline uint16_t chat_crc16(const uint8_t* data, size_t length) {
    uint16_t crc = 0xFFFFu;
    while(length--) {
        crc ^= (uint16_t)*data++ << 8;
        for(uint8_t bit = 0; bit < 8; ++bit)
            crc = (uint16_t)((crc & 0x8000u) ? ((uint32_t)crc << 1) ^ 0x1021u : (uint32_t)crc << 1);
    }
    return crc;
}
static inline void chat_put16(uint8_t* p, uint16_t n) { p[0] = (uint8_t)n; p[1] = (uint8_t)(n >> 8); }
static inline void chat_put32(uint8_t* p, uint32_t n) {
    chat_put16(p, (uint16_t)n); chat_put16(p + 2, (uint16_t)(n >> 16));
}
static inline uint16_t chat_get16(const uint8_t* p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }
static inline uint32_t chat_get32(const uint8_t* p) { return chat_get16(p) | ((uint32_t)chat_get16(p + 2) << 16); }

static inline bool chat_packet_valid(const ChatPacket* p) {
    if(!p || !p->source || p->source == CHAT_BROADCAST || !p->destination ||
       !p->session || !p->message || p->fragments == 0 || p->fragments > CHAT_MAX_FRAGMENTS ||
       p->fragment >= p->fragments || p->length > CHAT_FRAGMENT_BYTES) return false;
    if(p->type == ChatDiscovery) {
        if(p->destination != CHAT_BROADCAST || p->fragment != 0 || p->fragments != 1 || p->length > 16) return false;
        for(size_t i = 0; i < p->length; ++i) if(p->payload[i] < 32 || p->payload[i] > 126) return false;
        return true;
    }
    if(p->type == ChatAck) return p->destination != CHAT_BROADCAST && p->length == 0;
    if(p->type != ChatText || p->destination == CHAT_BROADCAST || p->length == 0) return false;
    for(size_t i = 0; i < p->length; ++i) if(p->payload[i] < 32 || p->payload[i] > 126) return false;
    /* Canonical fragments prevent gaps/ambiguous reassembly lengths. */
    return p->fragment == p->fragments - 1 || p->length == CHAT_FRAGMENT_BYTES;
}

static inline size_t chat_encode(const ChatPacket* p, uint8_t* out, size_t capacity) {
    if(!chat_packet_valid(p) || !out || capacity < CHAT_HEADER_SIZE + p->length + 2u) return 0;
    out[0] = CHAT_MAGIC; out[1] = CHAT_VERSION; out[2] = p->type; out[3] = 0;
    chat_put16(out + 4, p->source); chat_put16(out + 6, p->destination);
    chat_put16(out + 8, p->session); chat_put32(out + 10, p->message);
    out[14] = p->fragment; out[15] = p->fragments; out[16] = p->length; out[17] = 0;
    memcpy(out + CHAT_HEADER_SIZE, p->payload, p->length);
    size_t size = CHAT_HEADER_SIZE + p->length;
    chat_put16(out + size, chat_crc16(out, size));
    return size + 2u;
}

static inline bool chat_decode(ChatPacket* p, const uint8_t* wire, size_t size) {
    if(!p || !wire || size < CHAT_HEADER_SIZE + 2u || size > CHAT_MAX_FRAME ||
       wire[0] != CHAT_MAGIC || wire[1] != CHAT_VERSION || wire[3] || wire[17] ||
       wire[16] > CHAT_FRAGMENT_BYTES || size != CHAT_HEADER_SIZE + wire[16] + 2u ||
       chat_get16(wire + size - 2) != chat_crc16(wire, size - 2)) return false;
    memset(p, 0, sizeof(*p));
    p->type = wire[2]; p->source = chat_get16(wire + 4); p->destination = chat_get16(wire + 6);
    p->session = chat_get16(wire + 8); p->message = chat_get32(wire + 10);
    p->fragment = wire[14]; p->fragments = wire[15]; p->length = wire[16];
    memcpy(p->payload, wire + CHAT_HEADER_SIZE, p->length);
    return chat_packet_valid(p);
}
#endif
