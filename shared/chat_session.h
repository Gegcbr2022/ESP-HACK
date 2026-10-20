#ifndef ESPHACK_CHAT_SESSION_H
#define ESPHACK_CHAT_SESSION_H
#include "chat_protocol.h"

#define CHAT_PEERS 8u
#define CHAT_RX_SLOTS 4u
#define CHAT_ACK_QUEUE 4u
#define CHAT_DUPLICATES 8u
#define CHAT_ACK_MS 700u
#define CHAT_RX_EXPIRY_MS 12000u
#define CHAT_SEND_EXPIRY_MS 20000u
#define CHAT_MAX_ATTEMPTS 4u

typedef enum { ChatIdle, ChatQueued, ChatSending, ChatDelivered, ChatFailed } ChatDelivery;
typedef struct { uint16_t id; uint32_t seen; char name[17]; } ChatPeer;
typedef struct {
    bool used;
    uint16_t source, session;
    uint32_t message, seen;
    uint8_t fragments, mask, last_length;
    uint8_t data[CHAT_MAX_TEXT];
} ChatAssembly;
typedef struct { uint16_t source, session; uint32_t message; } ChatSeen;
typedef struct {
    uint16_t source;
    uint32_t message;
    size_t length;
    char text[CHAT_MAX_TEXT + 1];
} ChatMessage;
typedef struct {
    uint16_t id, session, destination;
    uint32_t next_message, random, outgoing_id, due, started;
    char name[17];
    uint8_t outgoing[CHAT_MAX_TEXT];
    size_t outgoing_length;
    uint8_t fragment, fragments, attempts;
    ChatDelivery delivery;
    bool waiting_ack, announce, tx_enabled;
    uint8_t inflight; /* 0:none, 1:ack, 2:text, 3:discovery */
    ChatPacket ack[CHAT_ACK_QUEUE];
    uint8_t ack_count;
    ChatAssembly assembly[CHAT_RX_SLOTS];
    ChatSeen seen[CHAT_DUPLICATES];
    uint8_t seen_next;
    ChatPeer peers[CHAT_PEERS];
    ChatMessage received;
    uint32_t invalid_frames, duplicates, dropped_acks, rx_expired, rx_complete;
} ChatSession;

static inline bool chat_time_reached(uint32_t now, uint32_t deadline) { return (int32_t)(now - deadline) >= 0; }
static inline uint32_t chat_jitter(ChatSession* s) {
    s->random = s->random * 1664525u + 1013904223u;
    return 30u + s->random % 121u;
}
static inline void chat_session_init(ChatSession* s, uint16_t id, uint16_t boot_session, const char* name, uint32_t seed) {
    memset(s, 0, sizeof(*s));
    s->id = (!id || id == CHAT_BROADCAST) ? 1 : id;
    s->session = boot_session ? boot_session : 1;
    s->random = seed ? seed : 1;
    s->next_message = seed ? seed : 1;
    if(name) {
        strncpy(s->name, name, 16); s->name[16] = 0;
        for(size_t i = 0; s->name[i]; ++i) if((uint8_t)s->name[i] < 32 || (uint8_t)s->name[i] > 126) s->name[i] = '?';
    }
}
static inline void chat_session_enable_tx(ChatSession* s, bool enabled) {
    s->tx_enabled = enabled;
    if(!enabled) {
        s->announce = false; s->ack_count = 0;
        if(s->delivery == ChatQueued || s->delivery == ChatSending) s->delivery = ChatFailed;
        s->waiting_ack = false; s->inflight = 0;
    }
}
static inline void chat_session_discover(ChatSession* s) { if(s->tx_enabled) s->announce = true; }
static inline bool chat_session_send(ChatSession* s, uint16_t destination, const char* text, size_t length, uint32_t now) {
    if(!s->tx_enabled || !text || !length || length > CHAT_MAX_TEXT || !destination ||
       destination == CHAT_BROADCAST || destination == s->id || s->delivery == ChatQueued ||
       s->delivery == ChatSending || s->inflight) return false;
    for(size_t i = 0; i < length; ++i) if((uint8_t)text[i] < 32 || (uint8_t)text[i] > 126) return false;
    memcpy(s->outgoing, text, length); s->outgoing_length = length; s->destination = destination;
    s->outgoing_id = ++s->next_message;
    if(!s->outgoing_id) s->outgoing_id = ++s->next_message;
    s->fragment = 0; s->fragments = (uint8_t)((length + CHAT_FRAGMENT_BYTES - 1) / CHAT_FRAGMENT_BYTES);
    s->attempts = 0; s->waiting_ack = false; s->delivery = ChatQueued;
    s->due = now + chat_jitter(s); s->started = now;
    return true;
}
static inline void chat_session_peer(ChatSession* s, const ChatPacket* p, uint32_t now) {
    size_t slot = CHAT_PEERS;
    for(size_t i = 0; i < CHAT_PEERS; ++i) if(s->peers[i].id == p->source) { slot = i; break; }
    if(slot == CHAT_PEERS) {
        slot = 0;
        for(size_t i = 0; i < CHAT_PEERS; ++i) {
            if(!s->peers[i].id) { slot = i; break; }
            if((uint32_t)(now - s->peers[i].seen) > (uint32_t)(now - s->peers[slot].seen)) slot = i;
        }
        memset(&s->peers[slot], 0, sizeof(s->peers[slot]));
    }
    s->peers[slot].id = p->source; s->peers[slot].seen = now;
    if(p->type == ChatDiscovery) {
        memcpy(s->peers[slot].name, p->payload, p->length); s->peers[slot].name[p->length] = 0;
    }
}
static inline void chat_session_queue_ack(ChatSession* s, const ChatPacket* p) {
    if(!s->tx_enabled) return;
    for(size_t i = 0; i < s->ack_count; ++i)
        if(s->ack[i].destination == p->source && s->ack[i].session == p->session &&
           s->ack[i].message == p->message && s->ack[i].fragment == p->fragment) return;
    if(s->ack_count == CHAT_ACK_QUEUE) { ++s->dropped_acks; return; }
    ChatPacket* a = &s->ack[s->ack_count++];
    memset(a, 0, sizeof(*a)); a->type = ChatAck; a->source = s->id; a->destination = p->source;
    a->session = p->session; a->message = p->message; a->fragment = p->fragment; a->fragments = p->fragments;
}
/* Returns true once per complete text. Read s->received immediately on true. */
static inline bool chat_session_receive(ChatSession* s, const uint8_t* wire, size_t length, uint32_t now) {
    ChatPacket p;
    if(!chat_decode(&p, wire, length)) { ++s->invalid_frames; return false; }
    if(p.source == s->id || (p.destination != s->id && p.destination != CHAT_BROADCAST)) return false;
    chat_session_peer(s, &p, now);
    if(p.type == ChatDiscovery) return false;
    if(p.type == ChatAck) {
        if(s->waiting_ack && p.source == s->destination && p.session == s->session &&
           p.message == s->outgoing_id && p.fragment == s->fragment && p.fragments == s->fragments) {
            s->waiting_ack = false; s->attempts = 0;
            if(++s->fragment == s->fragments) s->delivery = ChatDelivered;
            else { s->delivery = ChatQueued; s->due = now + chat_jitter(s); }
        }
        return false;
    }
    /* Acknowledgements mean stored fragment, not merely valid radio reception. */
    for(size_t i = 0; i < CHAT_DUPLICATES; ++i)
        if(s->seen[i].source == p.source && s->seen[i].session == p.session && s->seen[i].message == p.message) {
            ++s->duplicates; chat_session_queue_ack(s, &p); return false;
        }
    size_t slot = CHAT_RX_SLOTS;
    for(size_t i = 0; i < CHAT_RX_SLOTS; ++i) {
        ChatAssembly* a = &s->assembly[i];
        if(a->used && (uint32_t)(now - a->seen) >= CHAT_RX_EXPIRY_MS) { a->used = false; ++s->rx_expired; }
        if(a->used && a->source == p.source && a->session == p.session && a->message == p.message) { slot = i; break; }
    }
    if(slot == CHAT_RX_SLOTS) {
        for(size_t i = 0; i < CHAT_RX_SLOTS; ++i) if(!s->assembly[i].used) { slot = i; break; }
        if(slot == CHAT_RX_SLOTS) return false; /* Full: no ACK, sender retries. */
        memset(&s->assembly[slot], 0, sizeof(ChatAssembly));
        s->assembly[slot].used = true; s->assembly[slot].source = p.source;
        s->assembly[slot].session = p.session; s->assembly[slot].message = p.message;
        s->assembly[slot].fragments = p.fragments;
    }
    ChatAssembly* a = &s->assembly[slot];
    if(a->fragments != p.fragments) { ++s->invalid_frames; return false; }
    uint8_t bit = (uint8_t)(1u << p.fragment);
    if(a->mask & bit) {
        if(memcmp(a->data + p.fragment * CHAT_FRAGMENT_BYTES, p.payload, p.length) ||
           (p.fragment == p.fragments - 1 && a->last_length != p.length)) { ++s->invalid_frames; return false; }
        ++s->duplicates;
    } else {
        memcpy(a->data + p.fragment * CHAT_FRAGMENT_BYTES, p.payload, p.length); a->mask |= bit;
        if(p.fragment == p.fragments - 1) a->last_length = p.length;
    }
    a->seen = now;
    bool complete = a->mask == (uint8_t)((1u << a->fragments) - 1u);
    /* Final ACK certifies complete assembly. A receiver reboot between fragments
     * must produce a timeout rather than a false "delivered" on the sender. */
    if(p.fragment != p.fragments - 1 || complete) chat_session_queue_ack(s, &p);
    if(!complete) return false;
    s->received.source = a->source; s->received.message = a->message;
    s->received.length = (size_t)(a->fragments - 1) * CHAT_FRAGMENT_BYTES + a->last_length;
    memcpy(s->received.text, a->data, s->received.length); s->received.text[s->received.length] = 0;
    s->seen[s->seen_next].source = a->source; s->seen[s->seen_next].session = a->session;
    s->seen[s->seen_next].message = a->message;
    s->seen_next = (uint8_t)((s->seen_next + 1) % CHAT_DUPLICATES);
    a->used = false; ++s->rx_complete; return true;
}

/* Poll after RX service. It returns one wire frame, then requires tx_result().
 * carrier_busy provides RSSI-based listen-before-talk, never modulation inference. */
static inline size_t chat_session_poll(ChatSession* s, uint32_t now, bool carrier_busy, uint8_t* wire, size_t capacity) {
    if(!s->tx_enabled || s->inflight) return 0;
    bool active = s->delivery == ChatQueued || s->delivery == ChatSending;
    if(active && (uint32_t)(now - s->started) >= CHAT_SEND_EXPIRY_MS) {
        s->delivery = ChatFailed; s->waiting_ack = false; active = false;
    }
    if(s->waiting_ack && chat_time_reached(now, s->due)) {
        s->waiting_ack = false;
        if(s->attempts >= CHAT_MAX_ATTEMPTS) { s->delivery = ChatFailed; active = false; }
        else { s->delivery = ChatQueued; s->due = now + chat_jitter(s); }
    }
    if(carrier_busy) { if(active && !s->waiting_ack) s->due = now + chat_jitter(s); return 0; }
    ChatPacket p;
    memset(&p, 0, sizeof(p));
    if(s->ack_count) { p = s->ack[0]; s->inflight = 1; }
    else if(active && !s->waiting_ack && chat_time_reached(now, s->due)) {
        p.type = ChatText; p.source = s->id; p.destination = s->destination; p.session = s->session;
        p.message = s->outgoing_id; p.fragment = s->fragment; p.fragments = s->fragments;
        size_t remaining = s->outgoing_length - (size_t)p.fragment * CHAT_FRAGMENT_BYTES;
        p.length = (uint8_t)(remaining > CHAT_FRAGMENT_BYTES ? CHAT_FRAGMENT_BYTES : remaining);
        memcpy(p.payload, s->outgoing + (size_t)p.fragment * CHAT_FRAGMENT_BYTES, p.length);
        s->inflight = 2;
    } else if(s->announce) {
        p.type = ChatDiscovery; p.source = s->id; p.destination = CHAT_BROADCAST; p.session = s->session;
        p.message = ++s->next_message; if(!p.message) p.message = ++s->next_message;
        p.fragments = 1; p.length = (uint8_t)strlen(s->name); memcpy(p.payload, s->name, p.length); s->inflight = 3;
    } else return 0;
    size_t size = chat_encode(&p, wire, capacity);
    if(!size) s->inflight = 0;
    return size;
}
static inline void chat_session_tx_result(ChatSession* s, bool success, uint32_t now) {
    uint8_t kind = s->inflight; s->inflight = 0;
    if(kind == 1) {
        /* ACK queue is finite; a lost ACK is recovered by the sender's retry. */
        if(s->ack_count) { --s->ack_count; memmove(s->ack, s->ack + 1, s->ack_count * sizeof(ChatPacket)); }
    } else if(kind == 2) {
        ++s->attempts;
        if(success) { s->waiting_ack = true; s->delivery = ChatSending; s->due = now + CHAT_ACK_MS; }
        else if(s->attempts >= CHAT_MAX_ATTEMPTS) { s->delivery = ChatFailed; s->waiting_ack = false; }
        else { s->delivery = ChatQueued; s->due = now + chat_jitter(s); }
    } else if(kind == 3) s->announce = false;
}
#endif
