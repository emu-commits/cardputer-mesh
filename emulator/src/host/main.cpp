// Phase-1 emulator entry. Composes two logical screens into the host terminal:
//   top  = CYD (53x20)  — the primary work surface (launcher / chat / nodes)
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
#include "host/file_store.h"
#include "host/host_unix.h"

using namespace ui;

static volatile std::sig_atomic_t g_run = 1;
static void on_sigint(int) { g_run = 0; }

// CYD grid: 53x20 from a 6x12 font on the 320x240 panel (4:3 landscape). 20 rows
// (not 30 from a 6x8 font) keeps cols/rows ~= 8/3 so the emulator reads landscape
// in a terminal too — terminal cells are ~2x taller than wide. See docs §presentation.
static constexpr int CYD_W = 53, CYD_H = 20;
static constexpr int BAR_H = 8;

int main(int argc, char** argv) {
    std::signal(SIGINT, on_sigint);

    bool real = false;
    bool pty_mode = false;
    std::string port = "/dev/ttyACM0";
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--real") == 0) real = true;
        else if (std::strcmp(argv[i], "--pty") == 0) pty_mode = true;
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
    mgr.reg("calc", "Calc", apps::make_calc);
    mgr.reg("calcurse", "Calendar / Todo", apps::make_calcurse);
    mgr.reg("editor", "Editor", apps::make_editor);
    mgr.reg("timer", "Timer", apps::make_timer);

    host::FileStore state("emu_state.dat"); // return-to-last-position (ARCHITECTURE §6)

    app::AppContext ctx;
    ctx.apps = &mgr; ctx.mesh = &meshf; ctx.store = &store; ctx.notify = &notify;
    ctx.state = &state;
    ctx.now_ms = host::now_ms();
    mgr.restore_session(ctx); // resume last app, else launcher

    TextCanvas cyd(CYD_W, CYD_H), bar(CYD_W, BAR_H);
    TextCanvas composite(CYD_W, CYD_H + 1 + BAR_H);
    TextCanvas panel(CYD_W, 2 + BAR_H); // built-in panel when CYD is on a PTY
    AnsiRenderer r_stdout, r_pty;

    host::StdoutTerminal term;
    host::RawInput input;

    // In --pty mode the CYD renders to its own pseudo-terminal (the faithful
    // "CYD on a serial line" sink), matching the hardware: CYD over UART, the
    // built-in 1.14" screen separate. Attach a terminal to the printed path.
    std::unique_ptr<host::PtyTerminal> cyd_pty;
    if (pty_mode) {
        cyd_pty = std::make_unique<host::PtyTerminal>();
        if (cyd_pty->ok()) {
            if (FILE* f = std::fopen("cyd_pty.path", "w")) {
                std::fprintf(f, "%s\n", cyd_pty->slave_path().c_str());
                std::fclose(f);
            }
            cyd_pty->on_start();
        } else {
            pty_mode = false; // fall back to combined view
        }
    }
    bool use_pty = pty_mode && cyd_pty && cyd_pty->ok();

    term.on_start();
    input.start();

    // A demo non-mesh event so the notification center shows a Reminder too.
    bool fired_reminder = false;
    uint32_t pty_last_full = 0; // periodic CYD full-repaint so a late-attached
                                // terminal (or a CYD that resets) re-syncs

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

        cyd.clear(White, Black);
        mgr.render(ctx, cyd);
        notify.render_status(bar, now);

        if (use_pty) {
            // CYD -> its own PTY; built-in screen (+ the CYD's path) -> this terminal.
            if (now - pty_last_full >= 1000) { r_pty.reset(); pty_last_full = now; }
            r_pty.render(cyd, *cyd_pty);
            if (cyd_pty->consume_dropped()) r_pty.reset(); // re-sync once attached

            panel.clear(White, Black);
            panel.text(0, 0, "CYD -> " + cyd_pty->slave_path() + "  (attach: screen <path>)",
                       BrightCyan, Black, ATTR_BOLD);
            panel.hline(1, 0, CYD_W, U'=', Gray, Black);
            panel.blit(bar, 2, 0);
            r_stdout.render(panel, term);
        } else {
            composite.clear(White, Black);
            composite.blit(cyd, 0, 0);
            composite.hline(CYD_H, 0, CYD_W, U'=', Gray, Black);
            composite.text(CYD_H, 2, " built-in screen ", Gray, Black, ATTR_DIM);
            composite.blit(bar, CYD_H + 1, 0);
            r_stdout.render(composite, term);
        }
        usleep(33000);
    }

    mgr.shutdown(ctx); // persist current app + resume tokens
    input.stop();
    term.on_stop();
    if (cyd_pty && cyd_pty->ok()) cyd_pty->on_stop();
    return 0;
}
