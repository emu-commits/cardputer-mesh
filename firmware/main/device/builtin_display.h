// Built-in 1.14" ST7789V2 — the status strip + notification center surface.
//
// This is the Cardputer ADV's on-board screen (NOT the CYD). It's small and only
// shows the NotificationCenter output, so we drive it with ESP-IDF's native
// esp_lcd ST7789 panel driver (no Arduino / LovyanGFX dependency) and render a
// ui::TextCanvas cell grid into it with the same 6x12 font + 16-colour palette
// the CYD uses. Wiring (from Plai's known-good config): a SEPARATE bus, SPI3,
// DC=34, CS=37, SCLK=36, MOSI=35 (write-only, no MISO), RST=33, backlight=38.
// Landscape (rotation 1) => 240x135 visible, panel offsets colstart=40 rowstart=53.
//
// The orientation/offset/inversion constants below are the usual things that need
// a one-line tweak once seen on glass (cf. the CYD's setSwapBytes/INVON dance):
//   MIRROR_X / MIRROR_Y / SWAP_XY  -> rotation & flips
//   GAP_X / GAP_Y                  -> panel offsets (shifted image)
//   INVERT_COLOR                   -> ST7789 IPS panels usually need this true
#pragma once
#include <cstdint>
#include <cstring>
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "core/text_canvas.h"
#include "device/font6x12.h"

namespace device {

class BuiltinDisplay {
public:
    static constexpr int PIN_DC = 34, PIN_CS = 37, PIN_SCLK = 36, PIN_MOSI = 35;
    static constexpr int PIN_RST = 33, PIN_BL = 38;
    static constexpr int W = 240, H = 135;            // landscape visible area
    static constexpr int FCW = 6, FCH = 12;           // Spleen glyph (source font)
    static constexpr int SCALE = 2;                   // 2x = big, readable on the 1.14"
    static constexpr int CW = FCW * SCALE;            // 12px cell
    static constexpr int CH = FCH * SCALE;            // 24px cell
    static constexpr int COLS = 19;                   // 19*12=228; leaves 12px for side margins
    static constexpr int ROWS = 5;                    // 5*24=120; leaves 15px for top/bottom margins
    static constexpr int X_OFF = (W - COLS * CW) / 2; // 6px each side
    static constexpr int Y_OFF = (H - ROWS * CH) / 2; // 7px top (8px bottom) — centred
    // Tunables (flip on glass if needed):
    static constexpr bool MIRROR_X = true, MIRROR_Y = false, SWAP_XY = true;
    static constexpr int  GAP_X = 40, GAP_Y = 53;
    static constexpr bool INVERT_COLOR = true;
    static constexpr const char* TAG = "disp";

    bool begin() {
        init_palette();
        // Backlight: drive the pin as a plain GPIO HIGH (full on). PWM dimming can
        // come later — a plain high rules out any LEDC misconfig as the cause of a
        // dark panel during bring-up.
        gpio_config_t bl = {};
        bl.pin_bit_mask = 1ULL << PIN_BL;
        bl.mode = GPIO_MODE_OUTPUT;
        gpio_config(&bl);
        gpio_set_level((gpio_num_t)PIN_BL, 1);

        spi_bus_config_t bus = {};
        bus.sclk_io_num = PIN_SCLK;
        bus.mosi_io_num = PIN_MOSI;
        bus.miso_io_num = -1;
        bus.quadwp_io_num = -1;
        bus.quadhd_io_num = -1;
        bus.max_transfer_sz = W * CH * 2 + 64;        // one text row-band
        esp_err_t e = spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO);
        ESP_LOGI(TAG, "spi_bus_initialize(SPI3) -> %s", esp_err_to_name(e));
        if (e != ESP_OK) return false;

        esp_lcd_panel_io_spi_config_t io = {};
        io.cs_gpio_num = PIN_CS;
        io.dc_gpio_num = PIN_DC;
        io.spi_mode = 0;
        io.pclk_hz = 40 * 1000 * 1000;
        io.trans_queue_depth = 10;
        io.lcd_cmd_bits = 8;
        io.lcd_param_bits = 8;
        e = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI3_HOST, &io, &io_);
        ESP_LOGI(TAG, "new_panel_io_spi -> %s", esp_err_to_name(e));
        if (e != ESP_OK) return false;

        esp_lcd_panel_dev_config_t pc = {};
        pc.reset_gpio_num = PIN_RST;
        pc.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;
        pc.bits_per_pixel = 16;
        e = esp_lcd_new_panel_st7789(io_, &pc, &panel_);
        ESP_LOGI(TAG, "new_panel_st7789 -> %s", esp_err_to_name(e));
        if (e != ESP_OK) return false;

        ESP_LOGI(TAG, "reset=%s init=%s invert=%s",
                 esp_err_to_name(esp_lcd_panel_reset(panel_)),
                 esp_err_to_name(esp_lcd_panel_init(panel_)),
                 esp_err_to_name(esp_lcd_panel_invert_color(panel_, INVERT_COLOR)));
        esp_lcd_panel_swap_xy(panel_, SWAP_XY);
        esp_lcd_panel_mirror(panel_, MIRROR_X, MIRROR_Y);
        esp_lcd_panel_set_gap(panel_, GAP_X, GAP_Y);
        ESP_LOGI(TAG, "disp_on -> %s", esp_err_to_name(esp_lcd_panel_disp_on_off(panel_, true)));
        ok_ = true;
        // The text grid covers ROWS*CH = 132px of the 135px-tall panel; clear the
        // whole panel to black once so the undrawn bottom band (and any slivers)
        // don't show power-on garbage.
        fill(pal_[0]);
        ESP_LOGI(TAG, "begin ok, backlight pin=%d", PIN_BL);
        return true;
    }

    void backlight(uint8_t duty) {       // on/off for now (plain GPIO)
        gpio_set_level((gpio_num_t)PIN_BL, duty ? 1 : 0);
    }

    // Clear well beyond the 135px visible area: the LCD's RAM keeps whatever was
    // last drawn across MCU resets/reflashes, so stale content from earlier
    // layouts can sit just below the visible region. Wipe a generous band.
    static constexpr int CLEAR_H = 200;
    void fill(uint16_t color) {
        if (!ok_) return;
        for (int x = 0; x < W * CH; ++x) rowbuf_[x] = color;
        for (int y = 0; y < CLEAR_H; y += CH) {
            int h = (y + CH <= CLEAR_H) ? CH : (CLEAR_H - y);
            esp_lcd_panel_draw_bitmap(panel_, 0, y, W, y + h, rowbuf_);
        }
    }
    void clear() { fill(pal_[0]); }

    // Boot: wipe the panel (incl. stale LCD RAM from prior flashes) to black and
    // light the backlight. The live status layout takes over once the boot
    // "ready" notification fires — no banner, so nothing stale lingers.
    void self_test() {
        if (!ok_) return;
        clear();
        backlight(1);
        ESP_LOGI(TAG, "panel cleared (font %dx%d, grid %dx%d)", CW, CH, COLS, ROWS);
    }

    // Diagnostic: draw a 1px white border at the exact panel edges. A black margin
    // before a border line means that edge's gap is too big; a missing/cut line
    // means it's too small. Lets us dial GAP_X/GAP_Y exactly.
    void frame() {
        if (!ok_) return;
        uint16_t wht = pal_[15];
        for (int i = 0; i < W; ++i) rowbuf_[i] = wht;
        esp_lcd_panel_draw_bitmap(panel_, 0, 0, W, 1, rowbuf_);        // top
        esp_lcd_panel_draw_bitmap(panel_, 0, H - 1, W, H, rowbuf_);    // bottom
        for (int i = 0; i < H; ++i) rowbuf_[i] = wht;
        esp_lcd_panel_draw_bitmap(panel_, 0, 0, 1, H, rowbuf_);        // left
        esp_lcd_panel_draw_bitmap(panel_, W - 1, 0, W, H, rowbuf_);    // right
    }

    // Render a COLS x ROWS TextCanvas to the panel, one row-band at a time, with
    // the grid centred via X_OFF/Y_OFF (margins stay black from the begin() fill).
    void render(const ui::TextCanvas& tc) {
        if (!ok_) return;
        int rows = tc.height() < ROWS ? tc.height() : ROWS;
        int cols = tc.width() < COLS ? tc.width() : COLS;
        for (int r = 0; r < rows; ++r) {
            std::memset(rowbuf_, 0, sizeof(uint16_t) * W * CH);   // full-width band, margins black
            for (int c = 0; c < cols; ++c) draw_cell(tc.at(r, c), c);
            int y = Y_OFF + r * CH;
            esp_lcd_panel_draw_bitmap(panel_, 0, y, W, y + CH, rowbuf_);
        }
    }

    bool ok() const { return ok_; }

private:
    static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
        return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    }
    void init_palette() {
        static const uint8_t rgb[16][3] = {
            {0,0,0},{170,0,0},{0,170,0},{170,85,0},{0,0,170},{170,0,170},{0,170,170},{170,170,170},
            {85,85,85},{255,85,85},{85,255,85},{255,255,85},{85,85,255},{255,85,255},{85,255,255},{255,255,255}};
        for (int i = 0; i < 16; ++i) pal_[i] = rgb565(rgb[i][0], rgb[i][1], rgb[i][2]);
    }
    static uint16_t dim565(uint16_t c) { return (uint16_t)((c >> 1) & 0x7BEF); }

    // Rasterise one cell (col c) into the CH-tall row-band buffer (W wide), with
    // each source glyph pixel expanded into a SCALE x SCALE block (2x = 12x24).
    void draw_cell(const ui::Cell& cell, int c) {
        uint8_t f = cell.fg, b = cell.bg, a = cell.attr;
        if (a & ui::ATTR_BOLD) { if (f < 8) f += 8; }
        if (a & ui::ATTR_INVERSE) { uint8_t t = f; f = b; b = t; }
        uint16_t cf = pal_[f & 15], cb = pal_[b & 15];
        if (a & ui::ATTR_DIM) cf = dim565(cf);
        bool underline = (a & ui::ATTR_UNDERLINE);

        char32_t cp = cell.cp;
        const uint8_t* g = nullptr;
        bool block = (cp == 0x2588);              // █ full cell
        bool vbar  = (cp == 0x2502);              // │ centre vline
        if (!block && !vbar && cp >= 0x20 && cp <= 0x7E) g = FONT6x12[cp - 0x20];

        int x0 = X_OFF + c * CW;                  // cell origin in scaled px (+ left margin)
        for (int gy = 0; gy < FCH; ++gy) {        // 12 source glyph rows
            uint8_t bits = g ? g[gy] : 0;
            for (int gx = 0; gx < FCW; ++gx) {    // 6 source glyph cols
                bool on;
                if (block) on = true;
                else if (vbar) on = (gx == 2 || gx == 3);
                else on = (bits >> (7 - gx)) & 1;
                if (underline && gy == FCH - 2) on = true;
                uint16_t col = on ? cf : cb;
                for (int sy = 0; sy < SCALE; ++sy) {       // expand to SCALE x SCALE
                    uint16_t* dst = &rowbuf_[(gy * SCALE + sy) * W + x0 + gx * SCALE];
                    for (int sx = 0; sx < SCALE; ++sx) dst[sx] = col;
                }
            }
        }
    }

    esp_lcd_panel_io_handle_t io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    bool ok_ = false;
    uint16_t pal_[16];
    uint16_t rowbuf_[W * CH];                       // one 240x24 row-band (~11.5 KB)
};

} // namespace device
