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

struct Message {
    uint32_t from_id;
    std::string from_name;
    uint32_t dest;      // BROADCAST or a node id
    uint8_t channel;
    std::string text;
    uint32_t ts_ms;
    bool outgoing;      // true if we sent it
};

using Subscriber = std::function<void(const Message&)>;

class MeshFacade {
public:
    virtual ~MeshFacade() = default;
    virtual uint32_t our_id() const = 0;
    virtual std::string our_short() const = 0;
    virtual std::string our_long() const = 0;
    virtual std::vector<Node> nodes() = 0;
    virtual uint32_t send_text(uint32_t dest, uint8_t channel, const std::string& text) = 0;
    virtual void subscribe(Subscriber cb) = 0;
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
};

// Shared rolling history both chat (reads) and the host (appends) use.
// Bounded two ways so it can't grow without limit: a hard count cap and a
// 30-day age window (mesh traffic older than 30 days is dropped). On device the
// file-backed history is pruned the same way using real timestamps.
class MessageStore {
public:
    static constexpr uint32_t MAX_AGE_MS = 30u * 24 * 3600 * 1000; // 30 days

    void append(const Message& m) {
        if (msgs_.size() >= cap_) msgs_.erase(msgs_.begin());
        msgs_.push_back(m);
        prune(m.ts_ms);
    }
    // Drop messages older than 30 days relative to `now_ms` (the newest ts).
    void prune(uint32_t now_ms) {
        if (now_ms < MAX_AGE_MS) return; // not enough elapsed time to prune
        uint32_t cutoff = now_ms - MAX_AGE_MS;
        size_t i = 0;
        while (i < msgs_.size() && msgs_[i].ts_ms < cutoff) ++i;
        if (i) msgs_.erase(msgs_.begin(), msgs_.begin() + i);
    }
    const std::vector<Message>& all() const { return msgs_; }
private:
    std::vector<Message> msgs_;
    size_t cap_ = 500;
};

} // namespace mesh
