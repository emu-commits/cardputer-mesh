// Cardputer Deck — device entry (bring-up skeleton).
//
// Mirrors the emulator's AppContext wiring, but renders the 53x20 CYD canvas to
// UART1 (Port-A G1 -> CYD) instead of a host terminal. Mesh is the portable
// StubMesh; persist/fs/wiki are stubs for now. Keyboard = the ADV's TCA8418 via
// device::Keyboard. Still stubbed: SD storage, real mesh/radio, built-in screen.
#include <cstdint>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

#include "apps/apps.h"
#include "core/ansi.h"
#include "core/app.h"
#include "core/clipboard.h"
#include "core/mesh.h"
#include "core/notification_center.h"
#include "core/settings.h"
#include "core/stub_mesh.h"
#include "core/text_canvas.h"

#include "device/builtin_display.h"
#include "device/cardputer_keyboard.h"
#include "device/nvs_store.h"
#include "device/sd_fs.h"
#include "device/sdcard.h"
#include "device/uart_terminal.h"
#include "host/sqlite_wiki.h"   // portable: sqlite3 C API only, shared with the emulator

static constexpr int CYD_W = 53, CYD_H = 20;
static constexpr int PORTA_TX_GPIO = 1;       // Grove G1 -> CYD GPIO35 (RX)
static constexpr int CYD_BAUD = 921600;

static inline uint32_t now_ms() { return (uint32_t)(esp_timer_get_time() / 1000); }

extern "C" void app_main(void) {
    static mesh::StubMesh meshf;
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
    mgr.reg("settings", "Settings", apps::make_settings);
    mgr.reg("wizard", "Mesh setup wizard", apps::make_wizard, /*hidden=*/true);
    mgr.reg("presets", "Config presets", apps::make_presets, /*hidden=*/true);

    // Real storage backends (step 3): NVS for scalars/session, SD for files +
    // the offline wiki. SD mount may fail (no card) — the fs/wiki then degrade
    // gracefully (ops return false / wiki.ok()==false), exactly like the stubs did.
    static device::SdCard sd;
    sd.mount();
    static device::NvsStore state;
    state.begin("deck");
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
    panel.self_test();                        // banner + colour bars: prove the glass

    ui::TextCanvas cyd(CYD_W, CYD_H);
    ui::TextCanvas bar(device::BuiltinDisplay::COLS, device::BuiltinDisplay::ROWS);
    ui::AnsiRenderer rend;
    uint32_t last_full = 0;

    for (;;) {
        uint32_t now = now_ms();
        ctx.now_ms = now;

        for (auto& ke : kbd.poll()) mgr.handle_key(ctx, ke);
        meshf.poll(now);
        mgr.apply_pending(ctx);
        mgr.tick(ctx);
        notify.bg_tick(now);

        cyd.clear(ui::White, ui::Black);
        mgr.render(ctx, cyd);

        if (now - last_full >= 1000) { rend.reset(); last_full = now; } // CYD resync heartbeat
        rend.render(cyd, term);

        // Built-in screen: NotificationCenter status strip + stacked notifications
        // (blanks itself after idle; wakes on new traffic — by design).
        notify.render_status(bar, now);
        panel.render(bar);

        vTaskDelay(pdMS_TO_TICKS(33));
    }
}
