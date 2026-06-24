#include "core/stub_mesh.h"

namespace mesh {

StubMesh::StubMesh() {
    nodes_ = {
        {0xA1, "basecamp", "BASE", 0, 8},
        {0xB2, "trailhead", "TRLH", 0, 5},
        {0xC3, "summit", "SMIT", 0, 11},
        {0xD4, "river-ford", "RIVR", 0, 3},
    };
}

void StubMesh::emit(const Message& m) {
    for (auto& n : nodes_)
        if (n.id == m.from_id) n.last_heard_ms = m.ts_ms;
    for (auto& s : subs_) s(m);
}

uint32_t StubMesh::send_text(uint32_t dest, uint8_t channel, const std::string& text) {
    uint32_t now = last_now_; // host pumps poll() each frame
    uint32_t pid = next_pkt_id_;
    Message m{our_id_, our_short_, dest, channel, text, now, true, pid,
              dest == BROADCAST ? ACK_NONE : ACK_PENDING};
    emit(m);
    if (dest != BROADCAST) ack_due_.push_back({pid, now + 1500}); // simulate a routing ACK

    // Schedule a fake reply ~2s later.
    const Node& replier = nodes_[(next_pkt_id_) % nodes_.size()];
    Message reply{replier.id, replier.short_name,
                  dest == BROADCAST ? BROADCAST : our_id_,
                  channel,
                  dest == BROADCAST ? "copy that, " + our_short_ : "ack: " + text.substr(0, 16),
                  0, false};
    pending_.push_back({/*at_ms set in poll relative*/ 0, reply});
    pending_.back().at_ms = 0; // filled on next poll using now+2000
    return next_pkt_id_++;
}

void StubMesh::request_traceroute(uint32_t dest) {
    TraceRoute tr; tr.dest = dest; tr.pending = true; tr.ts_ms = last_now_;
    tr_[dest] = tr;
    tr_due_.push_back({dest, last_now_ + 1500}); // simulate a ~1.5s round trip
}

void StubMesh::poll(uint32_t now_ms) {
    last_now_ = now_ms;
    // Deliver due delivery-acks.
    for (auto it = ack_due_.begin(); it != ack_due_.end();) {
        if (now_ms >= it->second) { if (ack_cb_) ack_cb_(it->first, true); it = ack_due_.erase(it); }
        else ++it;
    }
    // Resolve due traceroutes with a plausible route (us -> a relay -> dest).
    for (auto it = tr_due_.begin(); it != tr_due_.end();) {
        if (now_ms >= it->second) {
            TraceRoute& tr = tr_[it->first];
            tr.pending = false; tr.ts_ms = now_ms; tr.route.clear();
            tr.route.push_back(our_id_);
            for (auto& n : nodes_) if (n.id != it->first) { tr.route.push_back(n.id); break; }
            tr.route.push_back(it->first);
            it = tr_due_.erase(it);
        } else ++it;
    }

    // Finalise any reply timers that were just queued (at_ms == 0 sentinel).
    for (auto& p : pending_)
        if (p.at_ms == 0) p.at_ms = now_ms + 2000;

    // Deliver due replies.
    while (!pending_.empty() && pending_.front().at_ms <= now_ms) {
        Message m = pending_.front().msg;
        m.ts_ms = now_ms;
        pending_.pop_front();
        emit(m);
    }

    if (now_ms < next_emit_ms_) return;
    next_emit_ms_ = now_ms + 7000;

    struct Script { int node; uint32_t dest; const char* text; };
    static const Script script[] = {
        {0, 0x00C0FFEE, "you around? ping me when you get this"},      // DM
        {1, BROADCAST,  "CB can you relay today's weather?"},          // @mention
        {3, BROADCAST,  "anyone near the river ford? water's high"},   // channel chatter
        {2, 0x00C0FFEE, "made it to the summit, view is unreal"},      // DM
    };
    const Script& s = script[script_ % 4];
    script_++;
    const Node& n = nodes_[s.node];
    emit(Message{n.id, n.short_name, s.dest, 0, s.text, now_ms, false});
}

} // namespace mesh
