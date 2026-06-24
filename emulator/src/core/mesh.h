// MeshFacade — the thin boundary every app uses to reach the mesh.
// On device this wraps Plai's MeshService (hal->mesh()): sendText + a single
// setMessageCallback fanned out to subscribers. In the emulator the dev backend
// is StubMesh now, the Muzi R1 Neo over /dev/ttyACM* next.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace mesh {

constexpr uint32_t BROADCAST = 0xFFFFFFFFu;

struct Node {
    uint32_t id;
    std::string long_name;
    std::string short_name;
    uint32_t last_heard_ms;
    int snr;
    bool is_favorite = false; // Meshtastic node-DB favorite flag (pins + biases routing)
};

enum AckState : uint8_t { ACK_NONE = 0, ACK_PENDING = 1, ACK_OK = 2, ACK_FAIL = 3 };

struct Message {
    uint32_t from_id;
    std::string from_name;
    uint32_t dest;      // BROADCAST or a node id
    uint8_t channel;
    std::string text;
    uint32_t ts_ms;
    bool outgoing;      // true if we sent it
    uint32_t id = 0;    // packet id (for matching delivery acks)
    uint8_t ack = ACK_NONE; // delivery state for outgoing DMs
};

// Result of a traceroute request to a destination node: the discovered route of
// node ids (us .. dest). pending until a reply arrives (or it times out).
struct TraceRoute {
    uint32_t dest = 0;
    std::vector<uint32_t> route; // ordered hops, including us and dest
    bool pending = true;
    bool failed = false;
    uint32_t ts_ms = 0;
};

using Subscriber = std::function<void(const Message&)>;
// Delivery-ack notification: packet `id` was acked (ok) or failed/timed out.
using AckCallback = std::function<void(uint32_t id, bool ok)>;

class MeshFacade {
public:
    virtual ~MeshFacade() = default;
    virtual uint32_t our_id() const = 0;
    virtual std::string our_short() const = 0;
    virtual std::string our_long() const = 0;
    virtual std::vector<Node> nodes() = 0;
    virtual uint32_t send_text(uint32_t dest, uint8_t channel, const std::string& text) = 0;
    virtual void subscribe(Subscriber cb) = 0;
    virtual void on_ack(AckCallback) {} // register delivery-ack notifications
    virtual void poll(uint32_t now_ms) = 0; // pump (mirrors MeshService::update())
    // Whether the device config may be edited. False for a connected real node
    // we must not reconfigure (e.g. the live R1 Neo) -> config screens read-only.
    virtual bool config_writable() const { return true; }

    // Meshtastic node favorite (is_favorite in the node DB; pins + biases routing).
    // PRODUCTION (device build, Plai MeshService): MUST apply for real via the
    // admin set_favorite_node on the device's OWN node DB — it affects routing.
    // EMULATOR ONLY: the BridgeMesh backend keeps this local-only because it
    // points at the user's *separate* live R1 Neo dev node, which we must not
    // mutate. The StubMesh tracks it in memory. (See [[project]] notes.)
    virtual void set_favorite(uint32_t id, bool fav) { (void)id; (void)fav; }
    virtual bool is_favorite(uint32_t id) const { (void)id; return false; }

    // Traceroute: send a TRACEROUTE_APP request toward `dest`; the result is
    // collected asynchronously and read back via get_traceroute(). (This DOES
    // transmit on the mesh, like any normal packet — it is not config.)
    virtual void request_traceroute(uint32_t dest) { (void)dest; }
    virtual bool get_traceroute(uint32_t dest, TraceRoute& out) { (void)dest; (void)out; return false; }

    // Ignore list: suppress a node (no display/notify). Local node-DB flag.
    virtual void set_ignored(uint32_t id, bool ign) { (void)id; (void)ign; }
    virtual bool is_ignored(uint32_t id) const { (void)id; return false; }
};

// Selects one chat "window": a channel, or a DM with a peer. Lets the log be
// queried without pulling the whole history into RAM.
struct LogQuery {
    bool dm = false;
    uint32_t peer = 0;     // DM peer node id (when dm)
    uint8_t channel = 0;   // channel index (when !dm)
    uint32_t our_id = 0;   // to classify DM direction
    bool matches(const Message& m) const {
        if (dm) return (!m.outgoing && m.dest == our_id && m.from_id == peer) ||
                       (m.outgoing && m.dest == peer);
        return m.dest == BROADCAST && m.channel == channel;
    }
};

// MessageLog — append-only chat history behind a pageable seam (same idea as the
// fs / settings / persist seams). Consumers use window()/scan_from() rather than
// walking the whole log, so the DEVICE backing can page from an SD file and keep
// only the queried window resident (500+ messages never sit in 512 KB SRAM).
class MessageLog {
public:
    virtual ~MessageLog() = default;
    virtual void append(const Message& m) = 0;
    virtual void mark_ack(uint32_t id, bool ok) = 0;
    // Monotonic count of all messages ever appended (a stable cursor base for
    // scan_from, unaffected by front-trimming).
    virtual size_t count() const = 0;
    // Up to `limit` messages matching q, chronological, ending `from_end` matches
    // before the newest (for scrollback). Only this window is materialized.
    virtual std::vector<Message> window(const LogQuery& q, int limit, int from_end) = 0;
    virtual int match_count(const LogQuery& q) = 0;
    // Deliver messages at absolute indices [cursor, count()) to fn; return count().
    virtual size_t scan_from(size_t cursor, const std::function<void(const Message&)>& fn) = 0;
};

// RamMessageLog — dev/host backing: a bounded in-RAM ring. Bounded two ways so it
// can't grow without limit: a resident-window cap and a 30-day age window. On
// device this is replaced by an SD-file-backed log (same interface), pruned the
// same way using real timestamps.
class RamMessageLog : public MessageLog {
public:
    static constexpr uint32_t MAX_AGE_MS = 30u * 24 * 3600 * 1000; // 30 days

    void append(const Message& m) override {
        msgs_.push_back(m);
        while (msgs_.size() > cap_) { msgs_.erase(msgs_.begin()); ++base_; }
        prune(m.ts_ms);
    }
    void mark_ack(uint32_t id, bool ok) override {
        for (auto it = msgs_.rbegin(); it != msgs_.rend(); ++it)
            if (it->outgoing && it->id == id) { it->ack = ok ? ACK_OK : ACK_FAIL; return; }
    }
    size_t count() const override { return base_ + msgs_.size(); }
    std::vector<Message> window(const LogQuery& q, int limit, int from_end) override {
        std::vector<Message> hit;
        for (auto& m : msgs_) if (q.matches(m)) hit.push_back(m);
        int n = (int)hit.size();
        int end = n - from_end; if (end < 0) end = 0;
        int start = end - limit; if (start < 0) start = 0;
        return std::vector<Message>(hit.begin() + start, hit.begin() + end);
    }
    int match_count(const LogQuery& q) override {
        int n = 0; for (auto& m : msgs_) if (q.matches(m)) ++n; return n;
    }
    size_t scan_from(size_t cursor, const std::function<void(const Message&)>& fn) override {
        size_t start = cursor > base_ ? cursor - base_ : 0; // missed-if-pruned is acceptable
        for (size_t i = start; i < msgs_.size(); ++i) fn(msgs_[i]);
        return count();
    }

private:
    void prune(uint32_t now_ms) {
        if (now_ms < MAX_AGE_MS) return;
        uint32_t cutoff = now_ms - MAX_AGE_MS;
        size_t i = 0;
        while (i < msgs_.size() && msgs_[i].ts_ms < cutoff) ++i;
        if (i) { msgs_.erase(msgs_.begin(), msgs_.begin() + i); base_ += i; }
    }
    std::vector<Message> msgs_;
    size_t cap_ = 200;   // resident window cap; deeper history pages from SD on device
    size_t base_ = 0;    // count trimmed off the front (keeps absolute cursors stable)
};

} // namespace mesh
