// Cardputer Deck — device entry (bring-up skeleton).
//
// Mirrors the emulator's AppContext wiring, but renders the 53x20 CYD canvas to
// UART1 (Port-A G1 -> CYD) instead of a host terminal. Mesh is the portable
// StubMesh; persist/fs/wiki are stubs for now. No keyboard yet — this milestone
// proves: (1) the portable C++ cross-compiles + fits, (2) the UART sink works,
// (3) the physical Cardputer->CYD link renders the real UI.
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

#include "device/uart_terminal.h"
#include "stubs/stub_seams.h"

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

    static stubs::RamStore state;
    state.set("cfg.provisioned", "1");        // skip first-run wizard on the skeleton
    static stubs::NullFs filesystem;
    static clip::Clipboard clipboard;
    static cfg::Settings settings;
    settings.build_default();
    settings.load(state);
    static stubs::NullWiki wiki;

    static app::AppContext ctx;
    ctx.apps = &mgr; ctx.mesh = &meshf; ctx.log = &store; ctx.notify = &notify;
    ctx.state = &state; ctx.fs = &filesystem; ctx.clip = &clipboard; ctx.settings = &settings;
    ctx.wiki = &wiki;
    ctx.now_ms = now_ms();
    mgr.start("launcher", ctx);

    static device::UartTerminal term(UART_NUM_1);
    term.begin(PORTA_TX_GPIO, CYD_BAUD);

    ui::TextCanvas cyd(CYD_W, CYD_H);
    ui::AnsiRenderer rend;
    uint32_t last_full = 0;

    for (;;) {
        uint32_t now = now_ms();
        ctx.now_ms = now;

        meshf.poll(now);
        mgr.apply_pending(ctx);
        mgr.tick(ctx);
        notify.bg_tick(now);

        cyd.clear(ui::White, ui::Black);
        mgr.render(ctx, cyd);

        if (now - last_full >= 1000) { rend.reset(); last_full = now; } // CYD resync heartbeat
        rend.render(cyd, term);

        vTaskDelay(pdMS_TO_TICKS(33));
    }
}
