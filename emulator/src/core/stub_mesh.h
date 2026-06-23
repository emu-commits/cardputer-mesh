// StubMesh — a fake mesh that generates live traffic (DMs, @mentions, channel
// chatter) and auto-replies to what you send, so the chat app and notification
// center animate with no hardware. Swap for an R1-Neo serial backend later.
#pragma once
#include <deque>
#include "core/mesh.h"

namespace mesh {

class StubMesh : public MeshFacade {
public:
    StubMesh();

    uint32_t our_id() const override { return our_id_; }
    std::string our_short() const override { return our_short_; }
    std::string our_long() const override { return our_long_; }
    std::vector<Node> nodes() override { return nodes_; }
    uint32_t send_text(uint32_t dest, uint8_t channel, const std::string& text) override;
    void subscribe(Subscriber cb) override { subs_.push_back(std::move(cb)); }
    void poll(uint32_t now_ms) override;

private:
    void emit(const Message& m);

    uint32_t our_id_ = 0x00C0FFEE;
    std::string our_short_ = "CB";
    std::string our_long_ = "cardputer-cb";
    std::vector<Node> nodes_;
    std::vector<Subscriber> subs_;

    struct Pending { uint32_t at_ms; Message msg; };
    std::deque<Pending> pending_;

    uint32_t next_emit_ms_ = 4000;
    int script_ = 0;
    uint32_t next_pkt_id_ = 1;
};

} // namespace mesh
