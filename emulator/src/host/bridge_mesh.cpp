#include "host/bridge_mesh.h"
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

namespace host {

// Split into at most `maxparts` fields; the final field keeps any remaining
// delimiters (like Python's str.split(d, maxparts-1)).
static std::vector<std::string> splitn(const std::string& s, char d, int maxparts) {
    std::vector<std::string> v;
    size_t pos = 0;
    while ((int)v.size() < maxparts - 1) {
        size_t t = s.find(d, pos);
        if (t == std::string::npos) break;
        v.push_back(s.substr(pos, t - pos));
        pos = t + 1;
    }
    v.push_back(s.substr(pos));
    return v;
}

BridgeMesh::BridgeMesh(const std::string& port, const std::string& python, const std::string& script) {
    int to_c[2], from_c[2];
    if (pipe(to_c) != 0 || pipe(from_c) != 0) return;

    child_pid_ = fork();
    if (child_pid_ == 0) {
        // child
        dup2(to_c[0], STDIN_FILENO);
        dup2(from_c[1], STDOUT_FILENO);
        int logfd = open("mesh_bridge.log", O_CREAT | O_WRONLY | O_APPEND, 0644);
        if (logfd >= 0) { dup2(logfd, STDERR_FILENO); close(logfd); }
        close(to_c[0]); close(to_c[1]); close(from_c[0]); close(from_c[1]);
        char* argv[] = {const_cast<char*>(python.c_str()),
                        const_cast<char*>(script.c_str()),
                        const_cast<char*>("--port"),
                        const_cast<char*>(port.c_str()),
                        nullptr};
        execv(python.c_str(), argv);
        _exit(127); // exec failed
    }

    // parent
    close(to_c[0]);
    close(from_c[1]);
    in_fd_ = to_c[1];
    out_fd_ = from_c[0];
    int fl = fcntl(out_fd_, F_GETFL, 0);
    fcntl(out_fd_, F_SETFL, fl | O_NONBLOCK);
}

BridgeMesh::~BridgeMesh() {
    if (in_fd_ >= 0) { const char* q = "QUIT\n"; ssize_t r = ::write(in_fd_, q, 5); (void)r; close(in_fd_); }
    if (out_fd_ >= 0) close(out_fd_);
    if (child_pid_ > 0) {
        kill(child_pid_, SIGTERM);
        int st; waitpid(child_pid_, &st, 0);
    }
}

std::vector<mesh::Node> BridgeMesh::nodes() {
    std::vector<mesh::Node> v;
    v.reserve(nodes_.size());
    for (auto& kv : nodes_) v.push_back(kv.second);
    return v;
}

uint32_t BridgeMesh::send_text(uint32_t dest, uint8_t channel, const std::string& text) {
    if (in_fd_ < 0) return 0;
    std::string t = text;
    for (auto& c : t) if (c == '\n' || c == '\r' || c == '\t') c = ' ';
    std::string line = "SEND\t" + std::to_string(dest) + "\t" + std::to_string((int)channel) + "\t" + t + "\n";
    ssize_t r = ::write(in_fd_, line.data(), line.size());
    (void)r;
    return next_pkt_++;
}

void BridgeMesh::handle_line(const std::string& line, uint32_t now_ms) {
    if (line.empty()) return;
    if (line.rfind("RX\t", 0) == 0) {
        auto p = splitn(line, '\t', 7); // RX from name dest ch ts text
        if (p.size() < 7) return;
        mesh::Message m;
        m.from_id = (uint32_t)strtoul(p[1].c_str(), nullptr, 10);
        m.from_name = p[2];
        m.dest = (uint32_t)strtoul(p[3].c_str(), nullptr, 10);
        m.channel = (uint8_t)strtoul(p[4].c_str(), nullptr, 10);
        m.text = p[6];
        m.ts_ms = now_ms;
        m.outgoing = false;
        for (auto& s : subs_) s(m);
    } else if (line.rfind("NODE\t", 0) == 0) {
        auto p = splitn(line, '\t', 6); // NODE id short long snr last
        if (p.size() < 6) return;
        mesh::Node n;
        n.id = (uint32_t)strtoul(p[1].c_str(), nullptr, 10);
        n.short_name = p[2];
        n.long_name = p[3];
        n.snr = (int)strtod(p[4].c_str(), nullptr);
        n.last_heard_ms = (uint32_t)strtoul(p[5].c_str(), nullptr, 10);
        nodes_[n.id] = n;
    } else if (line.rfind("READY\t", 0) == 0) {
        auto p = splitn(line, '\t', 4);
        if (p.size() >= 4) {
            our_id_ = (uint32_t)strtoul(p[1].c_str(), nullptr, 10);
            our_short_ = p[2];
            our_long_ = p[3];
        }
    }
    // LOG lines: ignored (diagnostics go to mesh_bridge.log)
    (void)now_ms;
}

void BridgeMesh::poll(uint32_t now_ms) {
    if (out_fd_ < 0) return;
    char tmp[2048];
    ssize_t n;
    while ((n = ::read(out_fd_, tmp, sizeof tmp)) > 0)
        buf_.append(tmp, (size_t)n);
    size_t nl;
    while ((nl = buf_.find('\n')) != std::string::npos) {
        handle_line(buf_.substr(0, nl), now_ms);
        buf_.erase(0, nl + 1);
    }
}

} // namespace host
