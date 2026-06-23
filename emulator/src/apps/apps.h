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

} // namespace apps
