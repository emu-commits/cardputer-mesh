// Phase-1 emulator entry. Composes two logical screens into the host terminal:
//   top  = CYD (53x30)  — the primary work surface (launcher / chat / nodes)
//   bot  = built-in 1.14" screen (53x8) — status strip + notification center
// Mesh is a StubMesh generating live traffic; the same MeshFacade seam later
// points at the Muzi R1 Neo (real RF) or, on device, Plai's MeshService.
#include <csignal>
#include <unistd.h>

#include "apps/apps.h"
#include "core/ansi.h"
#include "core/app.h"
#include "core/notification_center.h"
#include "core/stub_mesh.h"
#include "host/host_unix.h"

using namespace ui;

static volatile std::sig_atomic_t g_run = 1;
static void on_sigint(int) { g_run = 0; }

static constexpr int CYD_W = 53, CYD_H = 30;
static constexpr int BAR_H = 8;

int main() {
    std::signal(SIGINT, on_sigint);

    mesh::StubMesh mesh;
    mesh::MessageStore store;
    nc::NotificationCenter notify(&mesh);

    // Single mesh callback fanned out (mirrors one setMessageCallback on device).
    mesh.subscribe([&](const mesh::Message& m) {
        store.append(m);
        notify.on_mesh(m);
    });

    app::AppManager mgr;
    mgr.reg("launcher", "Home", apps::make_launcher);
    mgr.reg("chat", "Mesh chat", apps::make_chat);
    mgr.reg("nodes", "Nodes", apps::make_node_list);

    app::AppContext ctx;
    ctx.apps = &mgr; ctx.mesh = &mesh; ctx.store = &store; ctx.notify = &notify;
    ctx.now_ms = host::now_ms();
    mgr.start("launcher", ctx);

    TextCanvas cyd(CYD_W, CYD_H), bar(CYD_W, BAR_H);
    TextCanvas composite(CYD_W, CYD_H + 1 + BAR_H);
    AnsiRenderer renderer;

    host::StdoutTerminal term;
    host::RawInput input;
    term.on_start();
    input.start();

    // A demo non-mesh event so the notification center shows a Reminder too.
    bool fired_reminder = false;

    while (g_run && !mgr.quit_requested) {
        uint32_t now = host::now_ms();
        ctx.now_ms = now;

        for (auto& ke : input.poll()) mgr.handle_key(ctx, ke);

        mesh.poll(now);
        if (!fired_reminder && now > 12000) {
            notify.add_event(nc::NotifType::Reminder, "calendar", "standup in 5 min", now);
            fired_reminder = true;
        }

        mgr.apply_pending(ctx);
        mgr.tick(ctx);
        notify.bg_tick(now);

        // Render both surfaces and compose.
        cyd.clear(White, Black);
        mgr.render(ctx, cyd);
        notify.render_status(bar, now);

        composite.clear(White, Black);
        composite.blit(cyd, 0, 0);
        composite.hline(CYD_H, 0, CYD_W, U'=', Gray, Black);
        composite.text(CYD_H, 2, " built-in screen ", Gray, Black, ATTR_DIM);
        composite.blit(bar, CYD_H + 1, 0);

        renderer.render(composite, term);
        usleep(33000);
    }

    input.stop();
    term.on_stop();
    return 0;
}
