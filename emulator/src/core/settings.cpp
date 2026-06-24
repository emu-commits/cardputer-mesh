#include "core/settings.h"
#include <cstdlib>

namespace cfg {

std::vector<std::string> Settings::split(const std::string& csv, char sep) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i <= csv.size()) {
        size_t j = csv.find(sep, i);
        out.push_back(csv.substr(i, j == std::string::npos ? std::string::npos : j - i));
        if (j == std::string::npos) break;
        i = j + 1;
    }
    return out;
}

const Item* Group::find(const std::string& key) const {
    for (auto& it : items) if (it.key == key) return &it;
    return nullptr;
}
Item* Group::find(const std::string& key) {
    for (auto& it : items) if (it.key == key) return &it;
    return nullptr;
}

bool Item::visible(const Group& g) const {
    if (when_key.empty()) return true;
    const Item* dep = g.find(when_key);
    return dep && dep->value == when_val;
}

namespace {
Item en(const char* key, const char* label, const char* opts, const char* def, const char* hint,
        const char* wk = "", const char* wv = "") {
    Item i; i.key = key; i.label = label; i.type = ENUM; i.value = def; i.hint = hint;
    i.options = Settings::split(opts); i.when_key = wk; i.when_val = wv; return i;
}
Item num(const char* key, const char* label, long mn, long mx, const char* def, const char* hint,
         const char* wk = "", const char* wv = "") {
    Item i; i.key = key; i.label = label; i.type = NUMBER; i.min = mn; i.max = mx; i.value = def;
    i.hint = hint; i.when_key = wk; i.when_val = wv; return i;
}
Item bl(const char* key, const char* label, bool def, const char* hint) {
    Item i; i.key = key; i.label = label; i.type = BOOL; i.value = def ? "1" : "0"; i.hint = hint; return i;
}
Item str(const char* key, const char* label, const char* def, const char* hint) {
    Item i; i.key = key; i.label = label; i.type = STRING; i.value = def; i.hint = hint; return i;
}
} // namespace

void Settings::build_default() {
    groups_.clear();

    Group lora; lora.name = "LoRa"; lora.ns = "lora";
    lora.items = {
        // Meshtastic-spec defaults (region UNSET, LongFast, slot 0, 3 hops); the
        // nyme.sh NYC config is shipped as a *preset* (see the Presets screen).
        en("region", "Region",
           "UNSET;US;EU_433;EU_868;CN;JP;ANZ;KR;TW;RU;IN;NZ_865;TH;LORA_24;UA_433;UA_868;MY_919;SG_923;BR_902",
           "UNSET", "LoRa region code (regulatory)"),
        en("modem_preset", "Modem preset",
           "LongFast;LongSlow;VeryLongSlow;MediumSlow;MediumFast;ShortFast;ShortSlow;LongModerate;ShortTurbo;LongTurbo;Custom",
           "LongFast", "Modem preset (Custom = manual BW/CR/SF)"),
        num("bandwidth", "Bandwidth kHz", 0, 1600, "250", "Only used when preset=Custom", "modem_preset", "Custom"),
        num("coding_rate", "Coding rate 4/N", 5, 8, "5", "Only used when preset=Custom", "modem_preset", "Custom"),
        num("spread_factor", "Spreading factor", 7, 12, "11", "Only used when preset=Custom", "modem_preset", "Custom"),
        num("freq_slot", "Frequency slot", 0, 255, "0", "Channel slot (0 = auto)"),
        num("hop_limit", "Number of hops", 1, 7, "3", "Max mesh routing hops (1-7)"),
        num("tx_power", "TX power dBm", -9, 22, "22", "Transmit power (-9 to 22)"),
        bl("rx_boost", "RX boost", false, "Enable SX126x RX boosted gain"),
        bl("duty_ovr", "Duty cycle override", false, "Override regional duty cycle limit"),
    };
    groups_.push_back(lora);

    Group node; node.name = "Owner / Node"; node.ns = "node";
    node.items = {
        str("long_name", "Long name", "CardputerCliff", "Up to 39 chars"),
        str("short_name", "Short name", "Clif", "Up to 4 chars"),
        en("role", "Device role",
           "CLIENT;CLIENT_MUTE;CLIENT_HIDDEN;ROUTER;ROUTER_CLIENT;REPEATER;TRACKER;SENSOR;TAK;LOST_AND_FOUND",
           "CLIENT", "Behavior on the mesh"),
        bl("unmessagable", "Unmessagable", false, "Node does not accept DMs"),
    };
    groups_.push_back(node);

    Group sec; sec.name = "Security"; sec.ns = "security";
    sec.items = {
        bl("has_key", "PKC key present", true, "X25519 keypair generated"),
        bl("admin_local", "Local admin only", true, "Restrict admin to this device"),
    };
    groups_.push_back(sec);

    Group pos; pos.name = "Position"; pos.ns = "position";
    pos.items = {
        en("mode", "Position source", "OFF;FIXED;GPS", "OFF", "Off, fixed coords, or GPS"),
        num("fixed_lat", "Fixed lat (1e7)", -900000000, 900000000, "0", "degrees * 1e7", "mode", "FIXED"),
        num("fixed_lon", "Fixed lon (1e7)", -1800000000, 1800000000, "0", "degrees * 1e7", "mode", "FIXED"),
        num("bcast_secs", "Broadcast interval s", 0, 86400, "900", "0 = disabled"),
    };
    groups_.push_back(pos);

    Group dev; dev.name = "Device metrics"; dev.ns = "devmetrics";
    dev.items = {
        num("nodeinfo_secs", "NodeInfo interval s", 0, 86400, "10800", "0 = disabled"),
        num("telemetry_secs", "Telemetry interval s", 0, 86400, "1800", "0 = disabled"),
        bl("t_battery", "Telemetry: battery", true, ""),
        bl("t_chutil", "Telemetry: channel util", true, ""),
        bl("t_airutil", "Telemetry: air util", true, ""),
    };
    groups_.push_back(dev);

    Group sys; sys.name = "System"; sys.ns = "system";
    sys.items = {
        en("tz", "Timezone", "UTC;GMT-5;GMT-4;GMT+0;GMT+1;GMT+2;GMT+8", "GMT-5", "Local timezone"),
        bl("sound", "Notification sound", true, "Play sound on new notifications"),
        num("brightness", "CYD brightness", 10, 100, "80", "Backlight percent"),
    };
    groups_.push_back(sys);
}

void Settings::load(persist::Store& s) {
    for (auto& g : groups_)
        for (auto& it : g.items) {
            std::string k = "cfg." + g.ns + "." + it.key;
            std::string v = s.get(k, "\x01"); // sentinel = absent
            if (v != "\x01") it.value = v;
        }
}
void Settings::save(persist::Store& s) {
    for (auto& g : groups_)
        for (auto& it : g.items)
            s.set("cfg." + g.ns + "." + it.key, it.value);
    s.flush();
}

std::string Settings::get(const std::string& ns, const std::string& key) const {
    for (auto& g : groups_) if (g.ns == ns) { const Item* i = g.find(key); if (i) return i->value; }
    return "";
}
long Settings::get_num(const std::string& ns, const std::string& key) const {
    return std::strtol(get(ns, key).c_str(), nullptr, 10);
}
bool Settings::get_bool(const std::string& ns, const std::string& key) const {
    return get(ns, key) == "1";
}
void Settings::set_value(const std::string& ns, const std::string& key, const std::string& val) {
    for (auto& g : groups_) if (g.ns == ns) { Item* i = g.find(key); if (i) i->value = val; }
}

std::string Settings::serialize() const {
    std::string s;
    for (auto& g : groups_)
        for (auto& it : g.items)
            s += g.ns + "." + it.key + "=" + it.value + "\n";
    return s;
}
void Settings::apply_serialized(const std::string& blob) {
    size_t i = 0;
    while (i < blob.size()) {
        size_t nl = blob.find('\n', i);
        std::string ln = blob.substr(i, nl == std::string::npos ? std::string::npos : nl - i);
        size_t eq = ln.find('=');
        size_t dot = ln.find('.');
        if (eq != std::string::npos && dot != std::string::npos && dot < eq)
            set_value(ln.substr(0, dot), ln.substr(dot + 1, eq - dot - 1), ln.substr(eq + 1));
        if (nl == std::string::npos) break;
        i = nl + 1;
    }
}

} // namespace cfg
