// BridgeMesh — a MeshFacade backed by a real Meshtastic node over USB serial,
// via the mesh_bridge.py helper (canonical meshtastic lib). Host-only: on device
// this role is filled by Plai's MeshService over SPI. Same MeshFacade seam.
#pragma once
#include <map>
#include <set>
#include <string>
#include <sys/types.h>
#include <vector>
#include "core/mesh.h"

namespace host {

class BridgeMesh : public mesh::MeshFacade {
public:
    BridgeMesh(const std::string& port, const std::string& python, const std::string& script);
    ~BridgeMesh() override;

    bool ok() const { return child_pid_ > 0; }

    uint32_t our_id() const override { return our_id_; }
    std::string our_short() const override { return our_short_; }
    std::string our_long() const override { return our_long_; }
    std::vector<mesh::Node> nodes() override;
    uint32_t send_text(uint32_t dest, uint8_t channel, const std::string& text) override;
    void subscribe(mesh::Subscriber cb) override { subs_.push_back(std::move(cb)); }
    void poll(uint32_t now_ms) override;
    // The connected node is the user's live node; never write config to it.
    bool config_writable() const override { return false; }
    // EMULATOR-ONLY safeguard: tracked locally, NOT pushed to the live R1 Neo dev
    // node. The production device build (Plai MeshService) DOES apply favorites
    // for real (admin set_favorite_node on its own node DB) — see mesh.h.
    void set_favorite(uint32_t id, bool fav) override { if (fav) favs_.insert(id); else favs_.erase(id); }
    bool is_favorite(uint32_t id) const override { return favs_.count(id) > 0; }

private:
    void handle_line(const std::string& line, uint32_t now_ms);

    int in_fd_ = -1;   // -> child stdin
    int out_fd_ = -1;  // <- child stdout
    pid_t child_pid_ = -1;
    std::string buf_;

    uint32_t our_id_ = 0;
    std::string our_short_ = "?";
    std::string our_long_ = "r1-neo";
    std::map<uint32_t, mesh::Node> nodes_;
    std::set<uint32_t> favs_;
    std::vector<mesh::Subscriber> subs_;
    uint32_t next_pkt_ = 1;
};

} // namespace host
