// Phase-1 emulator entry. Composes two logical screens into the host terminal:
//   top  = CYD (53x30)  — the primary work surface (launcher / chat / nodes)
//   bot  = built-in 1.14" screen (53x8) — status strip + notification center
// Mesh is a StubMesh generating live traffic; the same MeshFacade seam later
// points at the Muzi R1 Neo (real RF) or, on device, Plai's MeshService.
#include <csignal>
#include <cstring>
#include <memory>
#include <string>
#include <unistd.h>

#include "apps/apps.h"
#include "core/ansi.h"
#include "core/app.h"
#include "core/notification_center.h"
#include "core/mesh.h"
#include "core/stub_mesh.h"
#include "host/bridge_mesh.h"
#include "host/host_unix.h"

using namespace ui;

static volatile std::sig_atomic_t g_run = 1;
static void on_sigint(int) { g_run = 0; }

static constexpr int CYD_W = 53, CYD_H = 30;
static constexpr int BAR_H = 8;

int main(int argc, char** argv) {
    std::signal(SIGINT, on_sigint);

    bool real = false;
    std::string port = "/dev/ttyACM0";
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--real") == 0) real = true;
        else if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc) port = argv[++i];
    }

    // Mesh backend: real R1 Neo over serial, or the stub. Same MeshFacade seam.
    std::unique_ptr<mesh::MeshFacade> mesh_backend;
    if (real)
        mesh_backend = std::make_unique<host::BridgeMesh>(port, "./.venv/bin/python", "src/host/mesh_bridge.py");
    else
        mesh_backend = std::make_unique<mesh::StubMesh>();
    mesh::MeshFacade& meshf = *mesh_backend;

    mesh::MessageStore store;
    nc::NotificationCenter notify(&meshf);

    // Single mesh callback fanned out (mirrors one setMessageCallback on device).
    meshf.subscribe([&](const mesh::Message& m) {
        store.append(m);
        notify.on_mesh(m);
    });

    app::AppManager mgr;
    mgr.reg("launcher", "Home", apps::make_launcher);
    mgr.reg("chat", "Mesh chat", apps::make_chat);
    mgr.reg("nodes", "Nodes", apps::make_node_list);

    app::AppContext ctx;
    ctx.apps = &mgr; ctx.mesh = &meshf; ctx.store = &store; ctx.notify = &notify;
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

        meshf.poll(now);
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
