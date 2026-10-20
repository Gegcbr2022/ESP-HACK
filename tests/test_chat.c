#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../shared/chat_session.h"
#include "../shared/chat_radio.h"

static unsigned checks;
#define CHECK(x) do { ++checks; if(!(x)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); return false; } } while(0)

static ChatPacket packet(uint16_t source, uint16_t session, uint32_t message, uint8_t fragment, uint8_t fragments, const char* text, size_t length) {
    ChatPacket p; memset(&p, 0, sizeof(p));
    p.type = ChatText; p.source = source; p.destination = 2; p.session = session;
    p.message = message; p.fragment = fragment; p.fragments = fragments; p.length = (uint8_t)length;
    memcpy(p.payload, text, length); return p;
}
static bool codec(void) {
    CHECK(chat_crc16((const uint8_t*)"123456789", 9) == 0x29B1u);
    ChatPacket p = packet(1, 0x1234, 0x12345678, 0, 1, "Hello", 5), decoded;
    uint8_t guarded[CHAT_MAX_FRAME + 2]; memset(guarded, 0xA5, sizeof(guarded));
    size_t length = chat_encode(&p, guarded + 1, CHAT_MAX_FRAME);
    CHECK(length == 25); CHECK(guarded[0] == 0xA5 && guarded[61] == 0xA5);
    CHECK(guarded[9] == 0x34 && guarded[10] == 0x12 && guarded[11] == 0x78);
    CHECK(chat_decode(&decoded, guarded + 1, length));
    CHECK(decoded.message == p.message && decoded.session == p.session && decoded.length == 5);
    CHECK(memcmp(decoded.payload, "Hello", 5) == 0);
    for(size_t i = 0; i < length; ++i) {
        guarded[i + 1] ^= 1; CHECK(!chat_decode(&decoded, guarded + 1, length)); guarded[i + 1] ^= 1;
    }
    CHECK(!chat_decode(&decoded, guarded + 1, length - 1));
    CHECK(chat_encode(&p, guarded + 1, 24) == 0);
    p.payload[0] = 0; CHECK(chat_encode(&p, guarded + 1, CHAT_MAX_FRAME) == 0);
    p.payload[0] = 'A'; p.fragments = 5; CHECK(chat_encode(&p, guarded + 1, CHAT_MAX_FRAME) == 0);
    return true;
}
typedef struct { bool used; uint32_t due; unsigned to; uint8_t wire[CHAT_MAX_FRAME]; size_t length; } Channel;
static bool transport(bool drop_data, bool drop_ack, bool duplicate_data, uint32_t ack_delay, uint32_t start) {
    ChatSession nodes[2];
    chat_session_init(&nodes[0], 1, 111, "ESP", 123);
    chat_session_init(&nodes[1], 2, 222, "Flipper", 456);
    chat_session_enable_tx(&nodes[0], true); chat_session_enable_tx(&nodes[1], true);
    char text[CHAT_MAX_TEXT + 1];
    for(size_t i = 0; i < CHAT_MAX_TEXT; ++i) text[i] = (char)('A' + i % 26);
    text[CHAT_MAX_TEXT] = 0;
    CHECK(chat_session_send(&nodes[0], 2, text, CHAT_MAX_TEXT, start));
    Channel channel[16]; memset(channel, 0, sizeof(channel));
    unsigned deliveries = 0, dropped_data = 0, dropped_ack_count = 0;
    for(uint32_t elapsed = 0; elapsed < CHAT_SEND_EXPIRY_MS; elapsed += 10) {
        uint32_t now = start + elapsed;
        for(size_t e = 0; e < 16; ++e) if(channel[e].used && chat_time_reached(now, channel[e].due)) {
            if(chat_session_receive(&nodes[channel[e].to], channel[e].wire, channel[e].length, now)) ++deliveries;
            channel[e].used = false;
        }
        for(unsigned from = 0; from < 2; ++from) {
            uint8_t wire[CHAT_MAX_FRAME]; size_t length = chat_session_poll(&nodes[from], now, false, wire, sizeof(wire));
            if(!length) continue;
            ChatPacket p; CHECK(chat_decode(&p, wire, length));
            chat_session_tx_result(&nodes[from], true, now);
            if(p.type == ChatText && drop_data && dropped_data++ == 0) continue;
            if(p.type == ChatAck && drop_ack && dropped_ack_count++ == 0) continue;
            unsigned copies = p.type == ChatText && duplicate_data ? 2 : 1;
            for(unsigned copy = 0; copy < copies; ++copy) {
                size_t e; for(e = 0; e < 16; ++e) if(!channel[e].used) break;
                CHECK(e < 16);
                channel[e].used = true; channel[e].to = 1 - from;
                channel[e].due = now + (p.type == ChatAck ? ack_delay : 10u) + copy * 20u;
                channel[e].length = length; memcpy(channel[e].wire, wire, length);
            }
        }
        if(nodes[0].delivery == ChatDelivered) break;
    }
    CHECK(nodes[0].delivery == ChatDelivered); CHECK(deliveries == 1);
    CHECK(nodes[1].received.length == CHAT_MAX_TEXT);
    CHECK(memcmp(nodes[1].received.text, text, CHAT_MAX_TEXT) == 0);
    CHECK(nodes[1].ack_count <= CHAT_ACK_QUEUE);
    return true;
}
static bool reassembly_and_bounds(void) {
    ChatSession s; chat_session_init(&s, 2, 222, "RX", 1); chat_session_enable_tx(&s, true);
    char text[160]; memset(text, 'z', sizeof(text));
    uint8_t wire[60];
    unsigned order[] = {3, 1, 1, 0, 2}; unsigned delivered = 0;
    for(size_t i = 0; i < sizeof(order)/sizeof(order[0]); ++i) {
        ChatPacket p = packet(1, 5, 77, (uint8_t)order[i], 4, text, 40);
        size_t n = chat_encode(&p, wire, sizeof(wire)); CHECK(n == 60);
        if(chat_session_receive(&s, wire, n, (uint32_t)i)) ++delivered;
    }
    CHECK(delivered == 1); CHECK(s.received.length == 160); CHECK(s.duplicates == 1);
    CHECK(s.ack_count == CHAT_ACK_QUEUE - 1); /* final arrived before complete: no premature final ACK */
    ChatPacket p = packet(1, 5, 77, 0, 4, text, 40);
    size_t n = chat_encode(&p, wire, sizeof(wire));
    CHECK(!chat_session_receive(&s, wire, n, 20)); CHECK(s.duplicates == 2);
    CHECK(s.ack_count == CHAT_ACK_QUEUE - 1);
    p.session = 6; p.fragments = 1; p.length = 2;
    n = chat_encode(&p, wire, sizeof(wire)); CHECK(chat_session_receive(&s, wire, n, 30)); /* reboot */
    chat_session_init(&s, 2, 222, "RX", 1);
    for(size_t i = 0; i < CHAT_RX_SLOTS; ++i) {
        p = packet((uint16_t)(i + 10), 5, 88, 0, 2, text, 40);
        n = chat_encode(&p, wire, sizeof(wire)); CHECK(!chat_session_receive(&s, wire, n, 0));
    }
    p.source = 100; n = chat_encode(&p, wire, sizeof(wire));
    CHECK(!chat_session_receive(&s, wire, n, 1)); CHECK(s.ack_count == 0); /* receive-only never emits ACK */
    CHECK(!chat_session_receive(&s, wire, n, CHAT_RX_EXPIRY_MS + 1));
    CHECK(s.rx_expired == CHAT_RX_SLOTS);
    return true;
}
static bool retries_and_timeouts(void) {
    ChatSession s; chat_session_init(&s, 1, 11, "A", 99);
    CHECK(!chat_session_send(&s, 2, "x", 1, 0));
    chat_session_enable_tx(&s, true);
    CHECK(!chat_session_send(&s, 2, "x", 161, 0));
    CHECK(!chat_session_send(&s, CHAT_BROADCAST, "x", 1, 0));
    CHECK(chat_session_send(&s, 2, "x", 1, 0));
    unsigned sends = 0;
    for(uint32_t now = 0; now < 10000; now += 10) {
        uint8_t wire[60]; size_t n = chat_session_poll(&s, now, false, wire, sizeof(wire));
        if(n) { ++sends; chat_session_tx_result(&s, false, now); }
    }
    CHECK(sends == CHAT_MAX_ATTEMPTS); CHECK(s.delivery == ChatFailed);
    CHECK(chat_session_send(&s, 2, "x", 1, 0));
    for(uint32_t now = 0; now <= CHAT_SEND_EXPIRY_MS; now += 100) {
        uint8_t wire[60]; CHECK(chat_session_poll(&s, now, true, wire, sizeof(wire)) == 0);
    }
    CHECK(s.delivery == ChatFailed);
    CHECK(chat_session_send(&s, 2, "x", 1, 0));
    uint8_t wire[60]; size_t n = chat_session_poll(&s, 1000, false, wire, sizeof(wire)); CHECK(n > 0);
    chat_session_tx_result(&s, true, 1000);
    ChatPacket ack; CHECK(chat_decode(&ack, wire, n));
    ack.type = ChatAck; ack.source = 2; ack.destination = 1; ack.length = 0; ++ack.session;
    n = chat_encode(&ack, wire, sizeof(wire)); CHECK(!chat_session_receive(&s, wire, n, 1100));
    CHECK(s.waiting_ack); --ack.session;
    n = chat_encode(&ack, wire, sizeof(wire)); CHECK(!chat_session_receive(&s, wire, n, 1200));
    CHECK(s.delivery == ChatDelivered);
    chat_session_discover(&s); CHECK(s.announce); chat_session_enable_tx(&s, false);
    CHECK(!s.announce && !s.ack_count && !s.tx_enabled);
    return true;
}
static bool fuzz_and_profile(void) {
    uint32_t random = 23456;
    uint8_t wire[80]; ChatPacket p;
    for(size_t trial = 0; trial < 100000; ++trial) {
        for(size_t i = 0; i < sizeof(wire); ++i) { random = random * 1664525u + 1013904223u; wire[i] = (uint8_t)(random >> 24); }
        size_t n = random % sizeof(wire);
        if(chat_decode(&p, wire, n)) CHECK(chat_packet_valid(&p));
    }
    CHECK(CHAT_RADIO_REGISTER_BYTES % 2 == 0);
    for(size_t i = 0; i < CHAT_RADIO_REGISTER_BYTES; i += 2) CHECK(chat_radio_registers[i] > 0 && chat_radio_registers[i] <= 0x2E);
    CHECK(chat_radio_pa(433920000, 0) == 0x34); CHECK(chat_radio_pa(868350000, 0) == 0x27);
    CHECK(chat_radio_pa(433920000, 2) == 0xC0); CHECK(chat_radio_pa(868350000, 2) == 0xC2);
    return true;
}
int main(void) {
    if(!codec() || !transport(false, false, false, 10, 0) ||
       !transport(true, true, true, 250, 0) || !transport(false, false, false, 500, 0xFFFFFF00u) ||
       !reassembly_and_bounds() || !retries_and_timeouts() || !fuzz_and_profile()) return 1;
    printf("chat: PASS (%u checks, 100000 malformed-frame trials)\n", checks); return 0;
}
