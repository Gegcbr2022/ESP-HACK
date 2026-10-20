#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_input.h>
#include <gui/modules/text_box.h>
#include <notification/notification_messages.h>
#include "../../shared/chat_session.h"
#include "../../shared/chat_radio.h"

#define TAG "EspChat"
#define HISTORY_COUNT 8u
enum { ViewChat, ViewCompose, ViewMenu, ViewRead };
enum { EventCompose = 1, EventPeers, EventRadio, EventOlder, EventNewer, EventRead };
enum { MenuDiscover = 1, MenuReceiveOnly, MenuEnableTx, Menu433, Menu868, MenuPowerLow, MenuPowerZero, MenuPowerHigh, MenuPeerBase = 32 };

typedef struct { uint16_t peer; bool sent; char text[CHAT_MAX_TEXT + 1]; } History;
typedef struct {
    Gui* gui;
    NotificationApp* notifications;
    ViewDispatcher* dispatcher;
    View* main_view;
    TextInput* text_input;
    TextBox* text_box;
    Submenu* menu;
    FuriThread* worker;
    FuriMutex* mutex;
    ChatSession session;
    volatile bool running;
    uint32_t frequency;
    uint8_t power;
    uint16_t recipient;
    History history[HISTORY_COUNT];
    uint8_t history_count, history_index;
    float rssi;
    uint32_t radio_errors, tx_packets, rx_packets;
    char status[32];
    char compose[CHAT_MAX_TEXT + 1];
    char reading[CHAT_MAX_TEXT + 1];
    uint8_t screen;
} App;

static uint32_t now_ms(void) { return (uint32_t)((uint64_t)furi_get_tick() * 1000u / furi_kernel_get_tick_frequency()); }
static void lock(App* app) { furi_mutex_acquire(app->mutex, FuriWaitForever); }
static void unlock(App* app) { furi_mutex_release(app->mutex); }
static void add_history(App* app, uint16_t peer, bool sent, const char* text) {
    if(app->history_count == HISTORY_COUNT) {
        memmove(app->history, app->history + 1, (HISTORY_COUNT - 1) * sizeof(History));
        --app->history_count;
    }
    History* item = &app->history[app->history_count++];
    item->peer = peer; item->sent = sent;
    strncpy(item->text, text, CHAT_MAX_TEXT); item->text[CHAT_MAX_TEXT] = 0;
    app->history_index = app->history_count - 1;
}
static void radio_rx(void) {
    furi_hal_subghz_idle();
    furi_hal_subghz_flush_rx();
    furi_hal_subghz_rx();
}
static void radio_configure(uint32_t frequency, uint8_t power) {
    uint8_t preset[CHAT_RADIO_REGISTER_BYTES + 10];
    memcpy(preset, chat_radio_registers, CHAT_RADIO_REGISTER_BYTES);
    memset(preset + CHAT_RADIO_REGISTER_BYTES, 0, 10);
    preset[CHAT_RADIO_REGISTER_BYTES + 2] = chat_radio_pa(frequency, power);
    furi_hal_subghz_idle();
    furi_hal_subghz_load_custom_preset(preset);
    furi_hal_subghz_set_frequency_and_path(frequency);
    furi_hal_gpio_init(furi_hal_subghz_get_data_gpio(), GpioModeInput, GpioPullNo, GpioSpeedLow);
    radio_rx();
}
static bool radio_transmit(const uint8_t* wire, size_t length) {
    const GpioPin* pin = furi_hal_subghz_get_data_gpio();
    furi_hal_subghz_idle();
    furi_hal_subghz_write_packet(wire, (uint8_t)length);
    if(!furi_hal_subghz_tx()) { radio_rx(); return false; }
    uint32_t start = now_ms();
    bool sync_seen = false, done = false;
    while((uint32_t)(now_ms() - start) < 200u) {
        bool level = furi_hal_gpio_read(pin);
        if(level) sync_seen = true;
        if(sync_seen && !level) { done = true; break; }
        furi_delay_ms(1);
    }
    furi_hal_subghz_idle();
    furi_hal_subghz_flush_tx();
    radio_rx();
    return done;
}
static int32_t radio_worker(void* context) {
    App* app = context;
    uint32_t configured_frequency = 0, receive_since = now_ms();
    uint8_t configured_power = 255;
    uint32_t next_tx = 0, budget_started = now_ms();
    uint32_t airtime_used = 0;
    while(app->running) {
        lock(app);
        uint32_t frequency = app->frequency; uint8_t power = app->power;
        unlock(app);
        if(frequency != configured_frequency || power != configured_power) {
            radio_configure(frequency, power);
            configured_frequency = frequency; configured_power = power; receive_since = now_ms();
        }
        bool notify = false;
        uint32_t now = now_ms();
        bool signal = furi_hal_gpio_read(furi_hal_subghz_get_data_gpio());
        if(!signal && furi_hal_subghz_rx_pipe_not_empty()) {
            /* Official HAL can write up to64 bytes regardless of input size. */
            uint8_t scratch[64], size = 0;
            bool crc_ok = furi_hal_subghz_is_rx_data_crc_valid();
            furi_hal_subghz_idle();
            float rssi = furi_hal_subghz_get_rssi();
            furi_hal_subghz_read_packet(scratch, &size);
            lock(app);
            app->rssi = rssi;
            if(crc_ok && size <= CHAT_MAX_FRAME) {
                ++app->rx_packets;
                if(chat_session_receive(&app->session, scratch, size, now)) {
                    add_history(app, app->session.received.source, false, app->session.received.text);
                    if(!app->recipient) app->recipient = app->session.received.source;
                    notify = true;
                }
            } else ++app->radio_errors;
            unlock(app);
            radio_rx(); receive_since = now;
        } else if((uint32_t)(now - receive_since) >= 1000u) {
            /* Bounds a stuck partial frame or RX overflow, including bad-CRC autoflush. */
            radio_rx(); receive_since = now;
        }
        uint8_t wire[CHAT_MAX_FRAME];
        size_t size = 0;
        lock(app);
        app->rssi = furi_hal_subghz_get_rssi();
        if((uint32_t)(now - budget_started) >= 60000u) { budget_started = now; airtime_used = 0; }
        /* Finite airtime in each local60s window. Reserve the full200ms timeout.
         * Regional sliding-window/channel rules can impose stricter limits. */
        bool busy = signal || app->rssi > -75.0f || !chat_time_reached(now, next_tx) || airtime_used > 400u;
        size = chat_session_poll(&app->session, now, busy, wire, sizeof(wire));
        unlock(app);
        if(size) {
            uint32_t started = now_ms();
            bool allowed = furi_hal_region_is_frequency_allowed(configured_frequency);
            bool ok = allowed && radio_transmit(wire, size);
            uint32_t duration = (uint32_t)(now_ms() - started);
            lock(app);
            chat_session_tx_result(&app->session, ok, now_ms());
            if(allowed) airtime_used += duration + 2u;
            if(ok) ++app->tx_packets;
            else { ++app->radio_errors; snprintf(app->status, sizeof(app->status), "TX blocked/timeout"); }
            unlock(app);
            next_tx = now_ms() + 100u; receive_since = now_ms();
        }
        if(notify) notification_message(app->notifications, &sequence_single_vibro);
        furi_delay_ms(10);
    }
    furi_hal_subghz_idle(); furi_hal_subghz_sleep();
    return 0;
}

static const char* delivery_name(ChatDelivery status) {
    switch(status) {
    case ChatQueued: return "Queued";
    case ChatSending: return "Await ACK";
    case ChatDelivered: return "Delivered";
    case ChatFailed: return "Failed";
    default: return "Ready";
    }
}
static void draw(Canvas* canvas, void* model) {
    App* app = *(App**)model;
    char line[40];
    canvas_clear(canvas); canvas_set_font(canvas, FontSecondary);
    lock(app);
    snprintf(line, sizeof(line), "%lu.%03luMHz %s %ddB", app->frequency / 1000000u, (app->frequency % 1000000u) / 1000u,
             app->session.tx_enabled ? "TX" : "RX", (int)app->rssi);
    canvas_draw_str(canvas, 0, 8, line);
    snprintf(line, sizeof(line), "%04X > %04X %s", app->session.id, app->recipient, delivery_name(app->session.delivery));
    canvas_draw_str(canvas, 0, 18, line);
    if(app->history_count) {
        const History* h = &app->history[app->history_index];
        snprintf(line, sizeof(line), "%s %04X  %u/%u", h->sent ? "To" : "From", h->peer,
                 app->history_index + 1u, app->history_count);
        canvas_draw_str(canvas, 0, 28, line);
        size_t length = strlen(h->text);
        /* Two preview lines; Left/Right select messages. Full text remains in RAM. */
        for(size_t row = 0; row < 2; ++row) {
            size_t offset = row * 25;
            if(offset >= length) break;
            size_t count = length - offset; if(count > 25) count = 25;
            memcpy(line, h->text + offset, count); line[count] = 0;
            for(size_t i = 0; i < count; ++i) if((uint8_t)line[i] < 32 || (uint8_t)line[i] > 126) line[i] = '?';
            canvas_draw_str(canvas, 0, (uint8_t)(38 + row * 9), line);
        }
    } else canvas_draw_str(canvas, 0, 37, app->status);
    canvas_draw_str(canvas, 0, 62, "Up:Peers OK:Write Dn:RF");
    unlock(app);
}
static bool input(InputEvent* event, void* context) {
    App* app = context;
    if(event->type == InputTypeLong && event->key == InputKeyOk) {
        view_dispatcher_send_custom_event(app->dispatcher, EventRead); return true;
    }
    if(event->type != InputTypeShort) return false;
    uint32_t action = 0;
    if(event->key == InputKeyOk) action = EventCompose;
    else if(event->key == InputKeyUp) action = EventPeers;
    else if(event->key == InputKeyDown) action = EventRadio;
    else if(event->key == InputKeyLeft) action = EventOlder;
    else if(event->key == InputKeyRight) action = EventNewer;
    if(!action) return false;
    view_dispatcher_send_custom_event(app->dispatcher, action); return true;
}
static void menu_callback(void* context, uint32_t index);
static void switch_screen(App* app, uint8_t screen) {
    app->screen = screen; view_dispatcher_switch_to_view(app->dispatcher, screen);
}
static void show_peers(App* app) {
    submenu_reset(app->menu); submenu_set_header(app->menu, "Peers / discovery");
    submenu_add_item(app->menu, "Send discovery (TX opt-in)", MenuDiscover, menu_callback, app);
    lock(app);
    for(size_t i = 0; i < CHAT_PEERS; ++i) if(app->session.peers[i].id) {
        char label[40];
        snprintf(label, sizeof(label), "%04X %.16s", app->session.peers[i].id, app->session.peers[i].name);
        submenu_add_item(app->menu, label, MenuPeerBase + (uint32_t)i, menu_callback, app);
    }
    unlock(app); switch_screen(app, ViewMenu);
}
static void show_radio(App* app) {
    submenu_reset(app->menu); submenu_set_header(app->menu, "Lab RF / local rules apply");
    submenu_add_item(app->menu, "Receive only", MenuReceiveOnly, menu_callback, app);
    submenu_add_item(app->menu, "Enable permitted lab TX", MenuEnableTx, menu_callback, app);
    submenu_add_item(app->menu, "433.920 MHz (resets TX)", Menu433, menu_callback, app);
    submenu_add_item(app->menu, "868.350 MHz (resets TX)", Menu868, menu_callback, app);
    submenu_add_item(app->menu, "Power nominal -10dBm", MenuPowerLow, menu_callback, app);
    submenu_add_item(app->menu, "Power nominal 0dBm", MenuPowerZero, menu_callback, app);
    submenu_add_item(app->menu, "Power nominal +10dBm", MenuPowerHigh, menu_callback, app);
    switch_screen(app, ViewMenu);
}
static void menu_callback(void* context, uint32_t index) {
    App* app = context;
    lock(app);
    if(index == MenuDiscover) {
        chat_session_discover(&app->session);
        snprintf(app->status, sizeof(app->status), app->session.tx_enabled ? "Discovery queued" : "Enable lab TX in RF menu");
    } else if(index == MenuReceiveOnly) chat_session_enable_tx(&app->session, false);
    else if(index == MenuEnableTx) {
        bool allowed = furi_hal_region_is_frequency_allowed(app->frequency);
        chat_session_enable_tx(&app->session, allowed);
        snprintf(app->status, sizeof(app->status), allowed ? "Lab TX enabled" : "Blocked by firmware region");
    } else if(index == Menu433 || index == Menu868) {
        chat_session_enable_tx(&app->session, false);
        app->frequency = chat_radio_frequencies[index == Menu868 ? 1 : 0];
        snprintf(app->status, sizeof(app->status), "Frequency changed, RX only");
    } else if(index >= MenuPowerLow && index <= MenuPowerHigh) {
        chat_session_enable_tx(&app->session, false); app->power = (uint8_t)(index - MenuPowerLow);
        snprintf(app->status, sizeof(app->status), "Power changed, RX only");
    } else if(index >= MenuPeerBase && index < MenuPeerBase + CHAT_PEERS)
        app->recipient = app->session.peers[index - MenuPeerBase].id;
    unlock(app); switch_screen(app, ViewChat);
}
static void compose_done(void* context) {
    App* app = context;
    lock(app);
    bool queued = chat_session_send(&app->session, app->recipient, app->compose, strlen(app->compose), now_ms());
    if(queued) add_history(app, app->recipient, true, app->compose);
    snprintf(app->status, sizeof(app->status), queued ? "Message queued" : "No TX/peer or send busy");
    unlock(app); switch_screen(app, ViewChat);
}
static bool custom_event(void* context, uint32_t event) {
    App* app = context;
    if(event == EventPeers) show_peers(app);
    else if(event == EventRadio) show_radio(app);
    else if(event == EventRead) {
        lock(app);
        bool available = app->history_count != 0;
        if(available) memcpy(app->reading, app->history[app->history_index].text, sizeof(app->reading));
        unlock(app);
        if(available) {
            text_box_set_text(app->text_box, app->reading); text_box_set_focus(app->text_box, TextBoxFocusStart);
            switch_screen(app, ViewRead);
        }
    } else if(event == EventCompose) {
        lock(app); bool ready = app->session.tx_enabled && app->recipient;
        if(!ready) snprintf(app->status, sizeof(app->status), "Enable TX, discover/select peer");
        unlock(app);
        if(ready) {
            memset(app->compose, 0, sizeof(app->compose));
            text_input_reset(app->text_input); text_input_set_header_text(app->text_input, "Unencrypted lab text");
            text_input_set_result_callback(app->text_input, compose_done, app, app->compose, sizeof(app->compose), true);
            text_input_set_minimum_length(app->text_input, 1); switch_screen(app, ViewCompose);
        }
    } else {
        lock(app);
        if(event == EventOlder && app->history_index) --app->history_index;
        else if(event == EventNewer && app->history_index + 1 < app->history_count) ++app->history_index;
        unlock(app);
    }
    return true;
}
static bool navigation(void* context) {
    App* app = context;
    if(app->screen != ViewChat) switch_screen(app, ViewChat);
    else view_dispatcher_stop(app->dispatcher);
    return true;
}
static void tick(void* context) {
    App* app = context;
    with_view_model(app->main_view, App** model, { UNUSED(model); }, true);
}

int32_t subghz_chat_app(void* context) {
    UNUSED(context);
    App* app = malloc(sizeof(App));
    memset(app, 0, sizeof(*app));
    app->frequency = chat_radio_frequencies[0];
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    uint32_t seed = furi_hal_random_get();
    const uint8_t* uid = furi_hal_version_uid();
    uint16_t id = chat_crc16(uid, furi_hal_version_uid_size());
    chat_session_init(&app->session, id, (uint16_t)seed, furi_hal_version_get_name_ptr(), seed);
    snprintf(app->status, sizeof(app->status), "RX only. Down opens RF.");
    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    app->dispatcher = view_dispatcher_alloc();
    app->main_view = view_alloc();
    view_allocate_model(app->main_view, ViewModelTypeLocking, sizeof(App*));
    with_view_model(app->main_view, App** model, { *model = app; }, false);
    view_set_context(app->main_view, app); view_set_draw_callback(app->main_view, draw); view_set_input_callback(app->main_view, input);
    app->text_input = text_input_alloc(); app->text_box = text_box_alloc(); app->menu = submenu_alloc();
    view_dispatcher_add_view(app->dispatcher, ViewChat, app->main_view);
    view_dispatcher_add_view(app->dispatcher, ViewCompose, text_input_get_view(app->text_input));
    view_dispatcher_add_view(app->dispatcher, ViewMenu, submenu_get_view(app->menu));
    view_dispatcher_add_view(app->dispatcher, ViewRead, text_box_get_view(app->text_box));
    view_dispatcher_set_event_callback_context(app->dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->dispatcher, custom_event);
    view_dispatcher_set_navigation_event_callback(app->dispatcher, navigation);
    view_dispatcher_set_tick_event_callback(app->dispatcher, tick, furi_ms_to_ticks(100));
    view_dispatcher_attach_to_gui(app->dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    app->running = true;
    app->worker = furi_thread_alloc_ex("ChatRadio", 3072, radio_worker, app);
    furi_thread_start(app->worker); switch_screen(app, ViewChat); view_dispatcher_run(app->dispatcher);
    app->running = false; furi_thread_join(app->worker); furi_thread_free(app->worker);
    view_dispatcher_remove_view(app->dispatcher, ViewChat); view_dispatcher_remove_view(app->dispatcher, ViewCompose);
    view_dispatcher_remove_view(app->dispatcher, ViewMenu);
    view_dispatcher_remove_view(app->dispatcher, ViewRead);
    text_input_free(app->text_input); text_box_free(app->text_box); submenu_free(app->menu); view_free(app->main_view); view_dispatcher_free(app->dispatcher);
    furi_record_close(RECORD_NOTIFICATION); furi_record_close(RECORD_GUI);
    furi_mutex_free(app->mutex); free(app); return 0;
}
