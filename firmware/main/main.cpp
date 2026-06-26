// Cardputer Deck — device entry (bring-up skeleton).
//
// Mirrors the emulator's AppContext wiring, but renders the 53x20 CYD canvas to
// UART1 (Port-A G1 -> CYD) instead of a host terminal. Mesh is the portable
// StubMesh; persist/fs/wiki are stubs for now. Keyboard = the ADV's TCA8418 via
// device::Keyboard. Still stubbed: SD storage, real mesh/radio, built-in screen.
#include <cstdint>
#include <cstdio>
#include <string>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_task_wdt.h"
#include "driver/gpio.h"

#include "apps/apps.h"
#include "core/ansi.h"
#include "core/app.h"
#include "core/clipboard.h"
#include "core/lora_phy.h"
#include "core/mesh.h"
#include "core/notification_center.h"
#include "core/settings.h"
#include "core/text_canvas.h"
#include "core/wallclock.h"
#include "esp_mac.h"

#include "device/battery.h"
#include "device/builtin_display.h"
#include "device/cardputer_keyboard.h"
#include "device/nvs_store.h"
#include "device/sd_fs.h"
#include "device/sdcard.h"
#include "device/uart_terminal.h"
#include "device/radio_mesh.h"
#include "host/sqlite_wiki.h"   // portable: sqlite3 C API only, shared with the emulator

static constexpr int CYD_W = 53, CYD_H = 20;
static constexpr int PORTA_TX_GPIO = 1;       // Grove G1 -> CYD GPIO35 (RX)
static constexpr int CYD_BAUD = 921600;
static constexpr uint32_t CYD_IDLE_MS = 120000;  // blank the CYD backlight after 2 min idle

static inline uint32_t now_ms() { return (uint32_t)(esp_timer_get_time() / 1000); }

// Heap OOM hook: a failed allocation otherwise throws bad_alloc (caught by the
// app guard) or aborts silently — this logs the size/caps so the soak trail
// shows exactly what couldn't be satisfied. Runs in alloc context, so it uses
// the DRAM-safe logger only.
static void on_alloc_failed(size_t size, uint32_t caps, const char* fn) {
    ESP_DRAM_LOGE("oom", "alloc failed: %u bytes caps=0x%x in %s",
                  (unsigned)size, (unsigned)caps, fn ? fn : "?");
}

// Map a 1-10 brightness level to an 8-bit backlight duty along a perceptual
// curve. The eye is roughly logarithmic, so equal duty steps bunch up near full
// (87% looks the same as 100% — the slider feels dead). This spreads the visible
// change evenly and keeps level 1 dim-but-on. Same curve for both panels.
static int bl_level_to_duty(int level) {
    static const uint8_t lut[10] = { 8, 16, 28, 44, 64, 90, 122, 160, 205, 255 };
    if (level < 1) level = 1;
    if (level > 10) level = 10;
    return lut[level - 1];
}

extern "C" void app_main(void) {
    heap_caps_register_failed_alloc_callback(on_alloc_failed);

    static device::RadioMesh meshf;
    static mesh::RamMessageLog store;
    static nc::NotificationCenter notify(&meshf);
    meshf.subscribe([](const mesh::Message& m) { store.append(m); notify.on_mesh(m); });
    meshf.on_ack([](uint32_t id, bool ok) { store.mark_ack(id, ok); });

    static app::AppManager mgr;
    mgr.reg("launcher", "Home", apps::make_launcher);
    mgr.reg("chat", "Mesh chat", apps::make_chat);
    mgr.reg("nodes", "Nodes", apps::make_node_list);
    mgr.reg("calc", "Calc", apps::make_calc);
    mgr.reg("calcurse", "Calendar / Todo", apps::make_calcurse);
    mgr.reg("editor", "Editor", apps::make_editor);
    mgr.reg("journal", "Journal", apps::make_journal);
    mgr.reg("wiki", "Wiki", apps::make_wiki);
    mgr.reg("search", "Search", apps::make_search);
    mgr.reg("clock", "Clock", apps::make_clock);
    mgr.reg("timer", "Timer", apps::make_timer);
    mgr.reg("files", "Files", apps::make_files);
    mgr.reg("contacts", "Contacts", apps::make_contacts);
    mgr.reg("meshstatus", "Mesh status", apps::make_mesh_status);
    mgr.reg("channels", "Channels", apps::make_channels);
    mgr.reg("system", "System", apps::make_system);
    mgr.reg("cyberhack", "CyberHack", apps::make_cyberhack);
    mgr.reg("settings", "Settings", apps::make_settings);
    mgr.reg("wizard", "Mesh setup wizard", apps::make_wizard, /*hidden=*/true);
    mgr.reg("presets", "Config presets", apps::make_presets, /*hidden=*/true);

    // Real storage backends (step 3): NVS for scalars/session, SD for files +
    // the offline wiki. SD mount may fail (no card) — the fs/wiki then degrade
    // gracefully (ops return false / wiki.ok()==false), exactly like the stubs did.
    // The microSD and the LoRa radio share SPI2. Park the radio's chip-select
    // (GPIO5) HIGH before touching the card so the not-yet-configured SX1262
    // can't drive MISO and corrupt the card's SPI replies (which showed up as
    // sdmmc_card_init ESP_ERR_INVALID_RESPONSE 0x108).
    {
        gpio_config_t cs = {};
        cs.pin_bit_mask = 1ULL << 5;          // LoRa CS = GPIO5 (Plai board.h)
        cs.mode = GPIO_MODE_OUTPUT;
        gpio_config(&cs);
        gpio_set_level(GPIO_NUM_5, 1);
    }
    static device::SdCard sd;
    sd.mount();
    static device::NvsStore state;
    state.begin("deck");
    wallclock::init(state.get_int("clock.offset_s", 0));  // restore a manually-set clock
    static device::SdFs filesystem;            // rooted at /sdcard
    static clip::Clipboard clipboard;
    static cfg::Settings settings;
    settings.build_default();
    settings.load(state);                      // restores persisted Meshtastic config
    static host::SqliteWiki wiki("/sdcard/wiki.db");

    static app::AppContext ctx;
    ctx.apps = &mgr; ctx.mesh = &meshf; ctx.log = &store; ctx.notify = &notify;
    ctx.state = &state; ctx.fs = &filesystem; ctx.clip = &clipboard; ctx.settings = &settings;
    ctx.wiki = &wiki;
    ctx.free_heap = [] { return (size_t)esp_get_free_heap_size(); };  // low-heap guard seam
    ctx.now_ms = now_ms();

    // Re-apply persisted node favorite/ignore flags (NodeList writes them as CSV).
    {
        auto apply_ids = [&](const std::string& csv, bool fav) {
            size_t i = 0;
            while (i < csv.size()) {
                size_t c = csv.find(',', i);
                std::string tok = csv.substr(i, c == std::string::npos ? std::string::npos : c - i);
                if (!tok.empty()) {
                    uint32_t id = (uint32_t)std::strtoul(tok.c_str(), nullptr, 10);
                    if (fav) meshf.set_favorite(id, true); else meshf.set_ignored(id, true);
                }
                if (c == std::string::npos) break;
                i = c + 1;
            }
        };
        apply_ids(state.get("nodes.favs", ""), true);
        apply_ids(state.get("nodes.ignored", ""), false);
    }

    // First-run provisioning: fresh install (not provisioned, no saved session)
    // opens the config wizard; otherwise resume the last app (else launcher).
    if (state.get("cfg.provisioned", "").empty() && state.get("session.active", "").empty())
        mgr.start("wizard", ctx);
    else
        mgr.restore_session(ctx);

    static device::UartTerminal term(UART_NUM_1);
    term.begin(PORTA_TX_GPIO, CYD_BAUD);

    static device::Keyboard kbd;
    kbd.begin();                              // TCA8418 on I2C1 (SDA8/SCL9)

    // Built-in 1.14" ST7789 (status strip + notification center) on its own SPI3.
    static device::BuiltinDisplay panel;
    panel.begin();
    panel.self_test();                        // big-font boot banner: prove the glass
    vTaskDelay(pdMS_TO_TICKS(1200));          // brief banner, then the screen sleeps until a notification

    // Step 4 (increment 2): bring up the SX1262 for real — init + setConfig +
    // raw LoRa TX/RX on a DIO1-IRQ task (RadioLink). Shared SPI2 bus with the SD
    // (SCK40/MOSI14/MISO39, CS5/RST3/BUSY6/DIO1=4 — rear LoRa header). The PHY is
    // derived from the LoRa settings (region + modem preset + channel slot), the
    // Meshtastic way, so RX lands on the mesh and logs the R1 Neo's raw frames.
    // Not Meshtastic-framed yet; the protocol stack replacing StubMesh is next.
    HAL::SX1262Pins radio_pins = {};
    radio_pins.spi_host = SPI2_HOST;
    radio_pins.sck = 40; radio_pins.mosi = 14; radio_pins.miso = 39;
    radio_pins.cs = 5; radio_pins.rst = 3; radio_pins.busy = 6; radio_pins.dio1 = 4;
    radio_pins.rxen = -1; radio_pins.txen = -1;   // antenna switch via DIO2 (default)
    lora::Phy applied_phy = lora::phy_from_settings(settings);
    ESP_LOGI("deck", "LoRa cfg: region=%s preset=%s -> %.3f MHz SF%d (valid=%d)",
             settings.get("lora", "region").c_str(), settings.get("lora", "modem_preset").c_str(),
             applied_phy.freq_hz / 1e6, applied_phy.sf, applied_phy.valid);
    // Node identity: node num = low 4 bytes of the MAC (Meshtastic convention),
    // owner names from NVS (or a default). The primary channel hash uses the
    // modem-preset name (Meshtastic hashes the preset name for an empty channel).
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    uint32_t node_id = ((uint32_t)mac[2] << 24) | ((uint32_t)mac[3] << 16) | ((uint32_t)mac[4] << 8) | mac[5];
    std::string own_short = state.get("owner.short", "DECK");
    std::string own_long  = state.get("owner.long",  "Cardputer Deck");
    meshf.configure(node_id, own_short.c_str(), own_long.c_str(), &state);
    ESP_LOGI("deck", "node !%08lx \"%s\" (%s)", (unsigned long)node_id, own_long.c_str(), own_short.c_str());
    bool radio_ok = meshf.begin(radio_pins, applied_phy, settings.get("lora", "modem_preset").c_str());
    if (!radio_ok) ESP_LOGW("deck", "no LoRa hat — radio disabled");

    static device::Battery batt;
    batt.begin();

    ui::TextCanvas cyd(CYD_W, CYD_H);
    ui::TextCanvas bar(device::BuiltinDisplay::COLS, device::BuiltinDisplay::ROWS);
    ui::AnsiRenderer rend;
    uint32_t last_full = 0, last_stats = 0, last_input = now_ms();
    // Keep the CYD awake during a no-keypress animation (CyberHack's auto-crawl):
    // the app calls this each step so the idle-sleep timer below never fires mid-dive.
    ctx.keep_awake = [&last_input] { last_input = now_ms(); };
    bool screen_was_on = false, cyd_asleep = false;
    int applied_cyd_bl = -1, applied_builtin_bl = -1;   // -1 = not yet applied
    float v_prev = 0;

    // The CYD is a dumb terminal; send it a private CSI (ESC [ <duty> p) that it
    // parses into a backlight PWM duty (0 = off; ignored by old CYD firmware).
    auto cyd_backlight = [&](int duty) {
        char esc[16]; int n = std::snprintf(esc, sizeof esc, "\x1b[%dp", duty);
        term.write(std::string(esc, n));
    };
    auto cyd_duty_setting = [&]() { return bl_level_to_duty((int)settings.get_num("system", "brightness")); };

    // Battery + heap telemetry -> the status surfaces (System app, status strip).
    // No fuel-gauge IC on this board: percent is the ADC voltage through a LiPo
    // curve, and "+" (on charger) is INFERRED from the pack sitting high or
    // rising — there is no charge/VBUS pin to read. Heap min-ever + largest free
    // block are the numbers that expose no-PSRAM fragmentation over long uptime.
    auto read_telemetry = [&]() {
        char b[24];
        if (batt.present()) {
            float v = batt.voltage();
            bool chg = v >= 4.18f || (v_prev > 0 && v - v_prev > 0.03f);
            v_prev = v;
            std::snprintf(b, sizeof b, "%d%%%s", device::Battery::level(v), chg ? "+" : "");
        } else {
            std::snprintf(b, sizeof b, "USB");
        }
        notify.set_battery(b);
        size_t freeb = esp_get_free_heap_size();
        size_t minb  = esp_get_minimum_free_heap_size();
        size_t blk   = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
        char r[48];
        std::snprintf(r, sizeof r, "%uK free / min %uK / blk %uK",
                      (unsigned)(freeb >> 10), (unsigned)(minb >> 10), (unsigned)(blk >> 10));
        notify.set_ram(r);
        ESP_LOGI("mem", "free=%uK min=%uK blk=%uK batt=%s",          // soak trail (10 s cadence)
                 (unsigned)(freeb >> 10), (unsigned)(minb >> 10), (unsigned)(blk >> 10), b);
    };
    read_telemetry();

    notify.add_event(nc::NotifType::Generic, "deck", "ready", now_ms());  // light up once at boot

    // Watchdog: the main UI task joins the Task WDT (the radio task subscribes
    // itself). A real freeze then trips the WDT -> panic -> reboot instead of a
    // silent hang; the timeout (sdkconfig) is generous so a slow-but-legitimate
    // synchronous wiki FTS5 search can't false-trip it.
    esp_task_wdt_add(NULL);

    for (;;) {
        esp_task_wdt_reset();
        uint32_t now = now_ms();
        ctx.now_ms = now;

        auto keys = kbd.poll();
        for (auto& ke : keys) mgr.handle_key(ctx, ke);
        meshf.poll(now);
        mgr.apply_pending(ctx);
        mgr.tick(ctx);
        notify.bg_tick(now);

        cyd.clear(ui::White, ui::Black);
        mgr.render(ctx, cyd);

        if (now - last_full >= 1000) {                  // CYD resync heartbeat (~1 Hz)
            rend.reset();
            last_full = now;
            // Apply the two backlight settings (1-10) when they change, mapped to
            // an 8-bit duty on a perceptual curve. The built-in panel dims via
            // LEDC; the CYD via its CSI. Don't poke the CYD backlight while it's
            // idle-asleep (it would relight); the new level is used on next wake.
            int bb = (int)settings.get_num("system", "brightness_builtin");
            if (bb != applied_builtin_bl) { applied_builtin_bl = bb; panel.set_brightness(bl_level_to_duty(bb)); }
            int cb = (int)settings.get_num("system", "brightness");
            if (cb != applied_cyd_bl) { applied_cyd_bl = cb; if (!cyd_asleep) cyd_backlight(bl_level_to_duty(cb)); }
            // Retune the radio live if the LoRa settings changed (no reboot).
            if (radio_ok) {
                lora::Phy np = lora::phy_from_settings(settings);
                if (!lora::phy_same(np, applied_phy)) { applied_phy = np; meshf.request_retune(np); }
            }
        }
        if (now - last_stats >= 10000) { last_stats = now; read_telemetry(); }

        rend.render(cyd, term);

        // Built-in screen sleeps (backlight off) between notifications; a new
        // notification lights it up for OFF_MS, then it goes dark again. Render
        // the fresh frame BEFORE turning the backlight on so the wake is clean.
        bool screen_on = notify.screen_on(now);
        bool notif_wake = (!screen_was_on && screen_on);   // a notification just arrived
        if (screen_on) {
            if (!screen_was_on) panel.clear();        // wipe stale content before waking
            notify.render_status(bar, now);
            panel.render(bar);
            if (!screen_was_on) panel.backlight(1);
        } else if (screen_was_on) {
            panel.backlight(0);
        }
        screen_was_on = screen_on;

        // CYD idle-sleep: the PRIMARY screen blanks its backlight after the user
        // has been away from the keyboard for CYD_IDLE_MS, relighting on the next
        // key or a fresh notification (no lid sensor -> keyboard idle is the
        // proxy for "not looking at it"). Frames keep streaming while dark so the
        // content is already current the instant it wakes.
        if (!keys.empty() || notif_wake) last_input = now;
        if (!cyd_asleep && (now - last_input) >= CYD_IDLE_MS) { cyd_backlight(0); cyd_asleep = true; }
        else if (cyd_asleep && (!keys.empty() || notif_wake)) { cyd_asleep = false; cyd_backlight(cyd_duty_setting()); last_full = 0; }

        vTaskDelay(pdMS_TO_TICKS(33));
    }
}
