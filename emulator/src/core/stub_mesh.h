// StubMesh — a fake mesh that generates live traffic (DMs, @mentions, channel
// chatter) and auto-replies to what you send, so the chat app and notification
// center animate with no hardware. Swap for an R1-Neo serial backend later.
#pragma once
#include <deque>
#include <map>
#include <set>
#include "core/mesh.h"

namespace mesh {

class StubMesh : public MeshFacade {
public:
    StubMesh();

    uint32_t our_id() const override { return our_id_; }
    std::string our_short() const override { return our_short_; }
    std::string our_long() const override { return our_long_; }
    std::vector<Node> nodes() override {
        std::vector<Node> out = nodes_;
        for (auto& n : out) n.is_favorite = favs_.count(n.id) > 0;
        return out;
    }
    uint32_t send_text(uint32_t dest, uint8_t channel, const std::string& text) override;
    void subscribe(Subscriber cb) override { subs_.push_back(std::move(cb)); }
    void poll(uint32_t now_ms) override;
    void set_favorite(uint32_t id, bool fav) override { if (fav) favs_.insert(id); else favs_.erase(id); }
    bool is_favorite(uint32_t id) const override { return favs_.count(id) > 0; }
    void request_traceroute(uint32_t dest) override;
    bool get_traceroute(uint32_t dest, TraceRoute& out) override {
        auto it = tr_.find(dest); if (it == tr_.end()) return false; out = it->second; return true;
    }
    void set_ignored(uint32_t id, bool ign) override { if (ign) ignored_.insert(id); else ignored_.erase(id); }
    bool is_ignored(uint32_t id) const override { return ignored_.count(id) > 0; }

private:
    void emit(const Message& m);

    uint32_t our_id_ = 0x00C0FFEE;
    std::string our_short_ = "CB";
    std::string our_long_ = "cardputer-cb";
    std::vector<Node> nodes_;
    std::set<uint32_t> favs_;
    std::set<uint32_t> ignored_;
    std::map<uint32_t, TraceRoute> tr_;
    std::vector<std::pair<uint32_t, uint32_t>> tr_due_; // (dest, resolve_at_ms)
    uint32_t last_now_ = 0;
    std::vector<Subscriber> subs_;

    struct Pending { uint32_t at_ms; Message msg; };
    std::deque<Pending> pending_;

    uint32_t next_emit_ms_ = 4000;
    int script_ = 0;
    uint32_t next_pkt_id_ = 1;
};

} // namespace mesh
