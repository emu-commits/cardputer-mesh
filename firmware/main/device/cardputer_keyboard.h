// Cardputer ADV keyboard -> ui::KeyEvent.
//
// The ADV keyboard is a TCA8418 I2C matrix scanner (addr 0x34 on I2C1, SDA=8
// SCL=9). We talk to it directly (no Plai HAL dependency) and poll its event
// FIFO, which reports discrete press/release events — so we emit exactly one
// KeyEvent per physical key-down. The raw (row,col) is remapped to Plai's 4x14
// logical keymap grid (matching the ADV's physical wiring), then to our model.
//
// The ; . , / and ` keys double as Up Down Left Right and Esc (the same physical
// keys Plai uses for nav: KEY_NUM_UP is the ';' position, KEY_NUM_ESC the '`').
// Bare press = navigation; Fn + key = the literal character. (Ctrl + a nav key
// still passes the modifier through, e.g. Ctrl+';' = Ctrl+Up for wiki paging.)
// Ctrl/Shift/Alt are physical modifier keys.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "driver/i2c_master.h"
#include "core/input.h"

namespace device {

class Keyboard {
public:
    bool begin() {
        i2c_master_bus_config_t bc = {};
        bc.i2c_port = I2C_NUM_1;
        bc.sda_io_num = GPIO_NUM_8;
        bc.scl_io_num = GPIO_NUM_9;
        bc.clk_source = I2C_CLK_SRC_DEFAULT;
        bc.glitch_ignore_cnt = 7;
        bc.flags.enable_internal_pullup = true;
        if (i2c_new_master_bus(&bc, &bus_) != ESP_OK) return false;

        i2c_device_config_t dc = {};
        dc.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        dc.device_address = 0x34;          // TCA8418
        dc.scl_speed_hz = 400000;
        if (i2c_master_bus_add_device(bus_, &dc, &dev_) != ESP_OK) return false;

        // Init sequence (mirrors Plai's working TCA8418 config).
        static const uint8_t init[][2] = {
            {0x01, 0x00},                                  // CFG: reset
            {0x23, 0x00}, {0x24, 0x00}, {0x25, 0x00},      // GPIO_DIR 1..3 = input
            {0x20, 0xFF}, {0x21, 0xFF}, {0x22, 0xFF},      // GPI_EM 1..3 = event mode
            {0x26, 0x00}, {0x27, 0x00}, {0x28, 0x00},      // INT_LVL 1..3 = falling
            {0x1A, 0xFF}, {0x1B, 0xFF}, {0x1C, 0xFF},      // GPIO_INT_EN 1..3
        };
        for (auto& e : init) if (!wr(e[0], e[1])) return false;

        // set_matrix(7 rows, 8 cols): KP_GPIO1=rows, KP_GPIO2=cols, KP_GPIO3=0.
        wr(0x1D, 0x7F); wr(0x1E, 0xFF); wr(0x1F, 0x00);
        // enable key-event + overflow interrupts in CFG (FIFO fills regardless;
        // we poll, but this matches the known-good config).
        uint8_t cfg = 0; rd(0x01, &cfg); wr(0x01, cfg | 0x09);
        flush();
        ok_ = true;
        return true;
    }

    // Drain the FIFO and return the KeyEvents produced this tick.
    std::vector<ui::KeyEvent> poll() {
        std::vector<ui::KeyEvent> out;
        if (!ok_) return out;
        for (int guard = 0; guard < 32; ++guard) {
            uint8_t raw = get_event();
            if (raw == 0) break;
            bool press = raw & 0x80;
            int code = (raw & 0x7F) - 1;
            int row = code / 10, col = code % 10;
            int x = row * 2 + (col > 3 ? 1 : 0);  // remap to 4x14 logical grid
            int y = (col + 4) % 4;
            if (x < 0 || x >= 14 || y < 0 || y >= 4) continue;
            const KV& kv = MAP[y][x];

            switch (kv.type) {
                case FN:    fn_ = press;    continue;
                case SHIFT: shift_ = press; continue;
                case CTRL:  ctrl_ = press;  continue;
                case ALT:   alt_ = press;   continue;
                case OPT:   opt_ = press;   continue;
                default: break;
            }
            if (!press) continue;  // emit only on key-down for non-modifiers

            ui::KeyEvent k;
            k.ctrl = ctrl_; k.alt = alt_; k.shift = shift_;
            if (kv.type == TAB)        k.key = ui::Key::Tab;
            else if (kv.type == ENTER) k.key = ui::Key::Enter;
            else if (kv.type == DEL)   k.key = ui::Key::Backspace;
            else if (kv.type == SPACE) { k.key = ui::Key::Char; k.ch = ' '; }
            else {  // REGULAR
                char base = kv.a[0];
                bool nav = (base == '`' || base == ';' || base == '.' ||
                            base == ',' || base == '/');
                if (nav && !fn_) {  // bare press = navigation
                    switch (base) {
                        case '`': k.key = ui::Key::Esc;   break;
                        case ';': k.key = ui::Key::Up;    break;
                        case '.': k.key = ui::Key::Down;  break;
                        case ',': k.key = ui::Key::Left;  break;
                        case '/': k.key = ui::Key::Right; break;
                    }
                } else {            // Fn + key = literal char (and all other keys)
                    k.key = ui::Key::Char;
                    char c = ctrl_ ? base : (shift_ ? kv.b[0] : kv.a[0]);
                    k.ch = (char32_t)(unsigned char)c;
                }
            }
            out.push_back(k);
        }
        return out;
    }

private:
    enum { REGULAR = 0, TAB, FN, SHIFT, CTRL, OPT, ALT, DEL, ENTER, SPACE };
    struct KV { const char* a; const char* b; uint8_t type; };

    bool wr(uint8_t reg, uint8_t val) {
        uint8_t b[2] = {reg, val};
        return i2c_master_transmit(dev_, b, 2, 50) == ESP_OK;
    }
    bool rd(uint8_t reg, uint8_t* val) {
        return i2c_master_transmit_receive(dev_, &reg, 1, val, 1, 50) == ESP_OK;
    }
    uint8_t get_event() { uint8_t e = 0; rd(0x04, &e); return e; }   // KEY_EVENT_A
    void flush() { while (get_event() != 0) {} wr(0x02, 0x1F); }      // drain FIFO + clear INT

    i2c_master_bus_handle_t bus_ = nullptr;
    i2c_master_dev_handle_t dev_ = nullptr;
    bool ok_ = false;
    bool fn_ = false, shift_ = false, ctrl_ = false, alt_ = false, opt_ = false;

    // Plai's 4x14 logical keymap: {unshifted, shifted, type}.
    static constexpr KV MAP[4][14] = {
        {{"`","~",REGULAR},{"1","!",REGULAR},{"2","@",REGULAR},{"3","#",REGULAR},{"4","$",REGULAR},
         {"5","%",REGULAR},{"6","^",REGULAR},{"7","&",REGULAR},{"8","*",REGULAR},{"9","(",REGULAR},
         {"0",")",REGULAR},{"-","_",REGULAR},{"=","+",REGULAR},{"del","del",DEL}},
        {{"tab","tab",TAB},{"q","Q",REGULAR},{"w","W",REGULAR},{"e","E",REGULAR},{"r","R",REGULAR},
         {"t","T",REGULAR},{"y","Y",REGULAR},{"u","U",REGULAR},{"i","I",REGULAR},{"o","O",REGULAR},
         {"p","P",REGULAR},{"[","{",REGULAR},{"]","}",REGULAR},{"\\","|",REGULAR}},
        {{"fn","fn",FN},{"shift","shift",SHIFT},{"a","A",REGULAR},{"s","S",REGULAR},{"d","D",REGULAR},
         {"f","F",REGULAR},{"g","G",REGULAR},{"h","H",REGULAR},{"j","J",REGULAR},{"k","K",REGULAR},
         {"l","L",REGULAR},{";",":",REGULAR},{"'","\"",REGULAR},{"enter","enter",ENTER}},
        {{"ctrl","ctrl",CTRL},{"opt","opt",OPT},{"alt","alt",ALT},{"z","Z",REGULAR},{"x","X",REGULAR},
         {"c","C",REGULAR},{"v","V",REGULAR},{"b","B",REGULAR},{"n","N",REGULAR},{"m","M",REGULAR},
         {",","<",REGULAR},{".",">",REGULAR},{"/","?",REGULAR},{"space","space",SPACE}},
    };
};

} // namespace device
