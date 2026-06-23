// settings — a portable, data-driven config model mirroring Plai's
// settings.{h,cpp}: groups of typed items (BOOL/NUMBER/ENUM/STRING) with
// options, ranges, hints, and a visible_when condition. This is the single
// source of truth the Settings app edits, the config wizard writes, and the
// mesh-status view reads. Values persist via persist::Store under "cfg.<ns>.<key>".
//
// Field set + option lists mirror Plai (LoRa region/preset/bandwidth/coding/SF/
// slot/hop/tx_power, Node info name/role, Security, Position, Device metrics).
#pragma once
#include <string>
#include <vector>
#include "core/persist.h"

namespace cfg {

enum Type { BOOL, NUMBER, ENUM, STRING };

struct Item {
    std::string key;
    std::string label;
    Type type;
    std::string value;                 // current value, string form
    std::vector<std::string> options;  // for ENUM
    long min = 0, max = 0;             // for NUMBER
    std::string hint;
    std::string when_key, when_val;    // show only if sibling when_key == when_val
    bool visible(const struct Group& g) const;
};

struct Group {
    std::string name;
    std::string ns;
    std::vector<Item> items;
    const Item* find(const std::string& key) const;
    Item* find(const std::string& key);
};

class Settings {
public:
    void build_default();                 // construct the Plai-mirrored metadata
    void load(persist::Store& s);         // read persisted values over the defaults
    void save(persist::Store& s);         // write all values + flush

    std::vector<Group>& groups() { return groups_; }

    // typed accessors by namespace+key (return default-ish if missing)
    std::string get(const std::string& ns, const std::string& key) const;
    long get_num(const std::string& ns, const std::string& key) const;
    bool get_bool(const std::string& ns, const std::string& key) const;

    static std::vector<std::string> split(const std::string& csv, char sep = ';');

private:
    std::vector<Group> groups_;
};

} // namespace cfg
