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
#include "core/text_canvas.h"
#include "device/font6x12.h"

namespace device {

class BuiltinDisplay {
public:
    static constexpr int PIN_DC = 34, PIN_CS = 37, PIN_SCLK = 36, PIN_MOSI = 35;
    static constexpr int PIN_RST = 33, PIN_BL = 38;
    static constexpr int W = 240, H = 135;            // landscape visible area
    static constexpr int CW = 6, CH = 12;             // Spleen 6x12 cell
    static constexpr int COLS = W / CW;               // 40
    static constexpr int ROWS = H / CH;               // 11
    // Tunables (flip on glass if needed):
    static constexpr bool MIRROR_X = false, MIRROR_Y = false, SWAP_XY = true;
    static constexpr int  GAP_X = 40, GAP_Y = 53;
    static constexpr bool INVERT_COLOR = true;

    bool begin() {
        init_palette();
        // PWM backlight (start off; ramp up after init to avoid a white flash).
        ledc_timer_config_t lt = {};
        lt.speed_mode = LEDC_LOW_SPEED_MODE; lt.duty_resolution = LEDC_TIMER_8_BIT;
        lt.timer_num = LEDC_TIMER_0; lt.freq_hz = 5000; lt.clk_cfg = LEDC_AUTO_CLK;
        ledc_timer_config(&lt);
        ledc_channel_config_t lc = {};
        lc.gpio_num = PIN_BL; lc.speed_mode = LEDC_LOW_SPEED_MODE; lc.channel = LEDC_CHANNEL_0;
        lc.timer_sel = LEDC_TIMER_0; lc.duty = 0; lc.hpoint = 0;
        ledc_channel_config(&lc);

        spi_bus_config_t bus = {};
        bus.sclk_io_num = PIN_SCLK;
        bus.mosi_io_num = PIN_MOSI;
        bus.miso_io_num = -1;
        bus.quadwp_io_num = -1;
        bus.quadhd_io_num = -1;
        bus.max_transfer_sz = W * CH * 2 + 64;        // one text row-band
        if (spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) return false;

        esp_lcd_panel_io_spi_config_t io = {};
        io.cs_gpio_num = PIN_CS;
        io.dc_gpio_num = PIN_DC;
        io.spi_mode = 0;
        io.pclk_hz = 40 * 1000 * 1000;
        io.trans_queue_depth = 10;
        io.lcd_cmd_bits = 8;
        io.lcd_param_bits = 8;
        if (esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI3_HOST, &io, &io_) != ESP_OK)
            return false;

        esp_lcd_panel_dev_config_t pc = {};
        pc.reset_gpio_num = PIN_RST;
        pc.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;
        pc.bits_per_pixel = 16;
        if (esp_lcd_new_panel_st7789(io_, &pc, &panel_) != ESP_OK) return false;

        esp_lcd_panel_reset(panel_);
        esp_lcd_panel_init(panel_);
        esp_lcd_panel_invert_color(panel_, INVERT_COLOR);
        esp_lcd_panel_swap_xy(panel_, SWAP_XY);
        esp_lcd_panel_mirror(panel_, MIRROR_X, MIRROR_Y);
        esp_lcd_panel_set_gap(panel_, GAP_X, GAP_Y);
        esp_lcd_panel_disp_on_off(panel_, true);
        ok_ = true;
        return true;
    }

    void backlight(uint8_t duty) {       // 0..255
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    }

    void fill(uint16_t color) {
        if (!ok_) return;
        for (int x = 0; x < W * CH; ++x) rowbuf_[x] = color;
        for (int y = 0; y < H; y += CH) {
            int h = (y + CH <= H) ? CH : (H - y);
            esp_lcd_panel_draw_bitmap(panel_, 0, y, W, y + h, rowbuf_);
        }
    }

    // Boot self-test: banner + 16 colour bars, so the panel/orientation/colours
    // can be eyeballed before any live data (same idea as the CYD self-test).
    void self_test() {
        if (!ok_) return;
        fill(pal_[0]);
        ui::TextCanvas tc(COLS, ROWS);
        tc.clear(ui::White, ui::Black);
        tc.text(0, 0, "Cardputer Deck", ui::BrightWhite, ui::Black, ui::ATTR_BOLD);
        tc.text(1, 0, "built-in screen ok", ui::BrightGreen, ui::Black);
        render(tc);
        // colour bars on the last row band
        for (int i = 0; i < 16; ++i) {
            int x0 = i * (W / 16);
            for (int y = 0; y < CH; ++y)
                for (int x = 0; x < (W / 16); ++x) rowbuf_[y * W + x0 + x] = pal_[i];
        }
        esp_lcd_panel_draw_bitmap(panel_, 0, (ROWS - 1) * CH, W, (ROWS - 1) * CH + CH, rowbuf_);
        backlight(200);
    }

    // Render a COLS x ROWS TextCanvas to the panel, one 12px row-band at a time.
    void render(const ui::TextCanvas& tc) {
        if (!ok_) return;
        int rows = tc.height() < ROWS ? tc.height() : ROWS;
        int cols = tc.width() < COLS ? tc.width() : COLS;
        for (int r = 0; r < rows; ++r) {
            std::memset(rowbuf_, 0, sizeof(uint16_t) * W * CH);
            for (int c = 0; c < cols; ++c) draw_cell(tc.at(r, c), c);
            esp_lcd_panel_draw_bitmap(panel_, 0, r * CH, W, r * CH + CH, rowbuf_);
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

    // Rasterise one cell (col c) into the 12-row band buffer (W wide).
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

        int x0 = c * CW;
        for (int y = 0; y < CH; ++y) {
            uint8_t bits = g ? g[y] : 0;
            uint16_t* dst = &rowbuf_[y * W + x0];
            for (int x = 0; x < CW; ++x) {
                bool on;
                if (block) on = true;
                else if (vbar) on = (x == 2 || x == 3);
                else on = (bits >> (7 - x)) & 1;
                if (underline && y == CH - 2) on = true;
                dst[x] = on ? cf : cb;
            }
        }
    }

    esp_lcd_panel_io_handle_t io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    bool ok_ = false;
    uint16_t pal_[16];
    uint16_t rowbuf_[W * CH];                       // one 240x12 row-band (~5.6 KB)
};

} // namespace device
