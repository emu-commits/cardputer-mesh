// Phase-1 apps: Launcher, Chat, NodeList. Text views over the "everything is a
// list, everything else is an overlay" contract. Plai's app_nodes/app_channels
// are the logic references; these are the text re-authoring.
#pragma once
#include <memory>
#include "core/app.h"

namespace apps {

std::unique_ptr<app::App> make_launcher();
std::unique_ptr<app::App> make_chat();
std::unique_ptr<app::App> make_node_list();
std::unique_ptr<app::App> make_calc();
std::unique_ptr<app::App> make_editor();
std::unique_ptr<app::App> make_timer();
std::unique_ptr<app::App> make_calcurse();
std::unique_ptr<app::App> make_files();
std::unique_ptr<app::App> make_contacts();
std::unique_ptr<app::App> make_settings();
std::unique_ptr<app::App> make_wizard();
std::unique_ptr<app::App> make_mesh_status();
std::unique_ptr<app::App> make_channels();
std::unique_ptr<app::App> make_journal();
std::unique_ptr<app::App> make_presets();

} // namespace apps
