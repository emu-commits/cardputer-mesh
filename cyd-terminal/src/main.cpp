// Cardputer Deck — CYD-side dumb ANSI terminal.
//
// The Cardputer ("the brain") runs every app and renders into a 53x20 logical
// cell grid, which its AnsiRenderer diffs and serialises as a tiny, fixed subset
// of ANSI/VT100 (see emulator/src/core/ansi.cpp). This sketch is the receiving
// end: it parses that stream and paints a 53x20 grid of 6x12 cells onto the
// 320x240 ILI9341, landscape. It NEVER sends anything back — keyboard input
// stays on the Cardputer. Three wires: 5V, GND, RXD/GPIO3<-Cardputer TX @ 921600.
//
// Recognised sequences (exactly what the emitter produces):
//   ESC [ 2 J            clear screen
//   ESC [ <row> ; <col> H   cursor to row;col (1-based)
//   ESC [ 0 ; .. m       SGR: 0 reset, 1 bold, 2 dim, 4 underline, 7 inverse,
//                        30-37/90-97 fg, 40-47/100-107 bg
//   ESC [ 0 m            reset
//   UTF-8 text           printable glyph at cursor (advances one cell)
#include <Arduino.h>
#include <TFT_eSPI.h>
#include "font6x12.h"

// ---- geometry ----
static const int COLS = 53, ROWS = 20;     // logical grid
static const int CW = 6, CH = 12;          // cell pixels (Spleen 6x12)
static const int ROT = 1;                   // 1 or 3 (flip if upside down)
static const uint32_t BAUD = 921600;

// ---- input source ----
#ifdef TERM_FROM_USB
  #define INPORT Serial
#else
  #define INPORT Serial1
  // CYD "RXD" pin = GPIO3 (U0RXD), grouped with 5V on the serial header so one
  // connector carries 5V + GND + data. Read it via UART1 (GPIO matrix) to leave
  // the UART0 USB console free. Cardputer Port-A G1 (TX) -> this pin.
  static const int RX_PIN = 3;
#endif

TFT_eSPI tft = TFT_eSPI();

// ---- backlight dimming ----
// The brain (Cardputer) owns the "CYD brightness" setting and pushes it to us as
// a private CSI: ESC [ <duty> p (0..255, already perceptually scaled by the
// brain). We drive TFT_BL with LEDC PWM. Old firmware without this case simply
// ignores the sequence (full brightness).
static const int BL_PIN = TFT_BL;     // 21 on the common ESP32-2432S028R
static const int BL_CH  = 7;          // LEDC channel (TFT_eSPI uses none here)
static int blDuty = 255;
static void applyBacklight() { ledcWrite(BL_CH, (uint32_t)blDuty); }

// 16-colour palette (VGA-ish), filled at boot with the panel's RGB565.
static uint16_t PAL[16];
static void initPalette() {
    static const uint8_t rgb[16][3] = {
        {0,0,0},{170,0,0},{0,170,0},{170,85,0},{0,0,170},{170,0,170},{0,170,170},{170,170,170},
        {85,85,85},{255,85,85},{85,255,85},{255,255,85},{85,85,255},{255,85,255},{85,255,255},{255,255,255}};
    for (int i = 0; i < 16; ++i) PAL[i] = tft.color565(rgb[i][0], rgb[i][1], rgb[i][2]);
}

// ---- terminal state ----
static const int RW = COLS * CW;             // row pixel width (318)
static int curR = 0, curC = 0;
static uint8_t fg = 7, bg = 0, attr = 0;     // attr bits: 1 bold,2 dim,4 underline,8 inverse

// We render a whole row per pushImage (our emitter always redraws full rows),
// which is ~50x fewer SPI transactions than per-cell and keeps up at 921600.
static uint16_t rowpix[RW * CH];             // one full row, 318x12
static int bufRow = -1;                      // which grid row rowpix holds (-1 = none)

static inline uint16_t dim565(uint16_t c) {  // halve brightness for ATTR_DIM
    return ((c >> 1) & 0x7BEF);
}

// Push the buffered row to the panel.
static void flushRow() {
    if (bufRow >= 0) { tft.pushImage(0, bufRow * CH, RW, CH, rowpix); bufRow = -1; }
}

// Rasterise one glyph cell into the row buffer at the cursor, then advance.
static void drawGlyph(uint32_t cp) {
    if (curR < 0 || curR >= ROWS) { curC++; return; }
    if (curR != bufRow) { flushRow(); bufRow = curR; }
    if (curC < 0 || curC >= COLS) { curC++; return; }
    uint8_t f = fg, b = bg;
    if (attr & 0x01) { if (f < 8) f += 8; }          // bold -> bright
    if (attr & 0x08) { uint8_t t = f; f = b; b = t; } // inverse -> swap
    uint16_t cf = PAL[f & 15], cb = PAL[b & 15];
    if (attr & 0x02) cf = dim565(cf);                 // dim
    bool underline = (attr & 0x04);

    const uint8_t* g = nullptr;
    bool block = (cp == 0x2588);                      // █ full block
    bool vbar  = (cp == 0x2502);                      // │ box-drawing vertical
    if (!block && !vbar && cp >= 0x20 && cp <= 0x7E) g = FONT6x12[cp - 0x20];

    int x0 = curC * CW;
    for (int y = 0; y < CH; ++y) {
        uint8_t bits = g ? g[y] : 0;
        uint16_t* dst = &rowpix[y * RW + x0];
        for (int x = 0; x < CW; ++x) {
            bool on;
            if (block) on = true;
            else if (vbar) on = (x == 2 || x == 3);
            else on = (bits >> (7 - x)) & 1;
            if (underline && y == CH - 2) on = true;
            dst[x] = on ? cf : cb;
        }
    }
    curC++;
}

// ---- ANSI parser ----
enum PState { P_NORMAL, P_ESC, P_CSI };
static PState ps = P_NORMAL;
static char csi[24]; static int csiN = 0;
static uint32_t uc = 0; static int ucLeft = 0;       // UTF-8 accumulator

static void applySGR() {
    // params in csi, ';'-separated; empty == single 0
    int vals[16], n = 0; int v = 0; bool any = false;
    for (int i = 0; i <= csiN; ++i) {
        char c = (i < csiN) ? csi[i] : ';';
        if (c == ';') { if (n < 16) vals[n++] = any ? v : 0; v = 0; any = false; }
        else if (c >= '0' && c <= '9') { v = v * 10 + (c - '0'); any = true; }
    }
    if (n == 0) { vals[n++] = 0; }
    for (int i = 0; i < n; ++i) {
        int p = vals[i];
        if (p == 0) { fg = 7; bg = 0; attr = 0; }
        else if (p == 1) attr |= 0x01;
        else if (p == 2) attr |= 0x02;
        else if (p == 4) attr |= 0x04;
        else if (p == 7) attr |= 0x08;
        else if (p >= 30 && p <= 37) fg = p - 30;
        else if (p >= 90 && p <= 97) fg = p - 90 + 8;
        else if (p >= 40 && p <= 47) bg = p - 40;
        else if (p >= 100 && p <= 107) bg = p - 100 + 8;
    }
}

static void finishCSI(char final) {
    if (final == 'H' || final == 'f') {           // cursor position (1-based)
        int r = 0, c = 0, *t = &r; bool any = false;
        for (int i = 0; i < csiN; ++i) {
            char ch = csi[i];
            if (ch == ';') { t = &c; any = false; }
            else if (ch >= '0' && ch <= '9') { *t = *t * 10 + (ch - '0'); any = true; }
        }
        int nr = (r > 0 ? r - 1 : 0);
        if (nr != bufRow) flushRow();              // finish the row we're leaving
        curR = nr;
        curC = (c > 0 ? c - 1 : 0);
    } else if (final == 'J') {                     // clear: our emitter always
        flushRow();
        curR = curC = 0;                           // follows 2J with a full-grid
        // redraw, so we DON'T fillScreen here — repainting every cell in place
        // avoids a visible black flash on each 1 Hz heartbeat repaint.
    } else if (final == 'm') {
        applySGR();
    } else if (final == 'p') {                     // private: set backlight duty (0-255)
        int v = 0; bool any = false;
        for (int i = 0; i < csiN; ++i) {
            char ch = csi[i];
            if (ch >= '0' && ch <= '9') { v = v * 10 + (ch - '0'); any = true; }
        }
        if (any) { if (v < 0) v = 0; if (v > 255) v = 255; blDuty = v; applyBacklight(); }
    }
    // everything else: ignore
}

static void feed(uint8_t b) {
    switch (ps) {
    case P_NORMAL:
        if (b == 0x1b) { ps = P_ESC; }
        else if (ucLeft) {                          // continuation byte
            uc = (uc << 6) | (b & 0x3F); if (--ucLeft == 0) { drawGlyph(uc); }
        } else if (b < 0x80) {
            if (b >= 0x20) drawGlyph(b);            // printable ASCII
        } else if ((b & 0xE0) == 0xC0) { uc = b & 0x1F; ucLeft = 1; }
        else if ((b & 0xF0) == 0xE0) { uc = b & 0x0F; ucLeft = 2; }
        // else: stray byte, drop
        break;
    case P_ESC:
        if (b == '[') { ps = P_CSI; csiN = 0; }
        else ps = P_NORMAL;
        break;
    case P_CSI:
        if (b >= 0x40 && b <= 0x7E) { finishCSI((char)b); ps = P_NORMAL; }
        else if (csiN < (int)sizeof(csi)) csi[csiN++] = (char)b;
        break;
    }
}

// ---- boot self-test: prove panel, colours, orientation before any serial ----
static void selfTest() {
    tft.fillScreen(TFT_BLACK);
    fg = 15; bg = 0; attr = 0; curR = 0; curC = 0;
    const char* msg = "Cardputer Deck CYD terminal  53x20 @921600";
    for (const char* p = msg; *p; ++p) drawGlyph((uint8_t)*p);
    // a row of the 16 palette colours as solid blocks
    for (int i = 0; i < 16; ++i)
        tft.fillRect(i * (320 / 16), CH + 2, 320 / 16 - 1, 16, PAL[i]);
    curR = 4; curC = 0; fg = 10;
    const char* m2 = "waiting for stream...";
    for (const char* p = m2; *p; ++p) drawGlyph((uint8_t)*p);
    flushRow();
    fg = 7; bg = 0; attr = 0; curR = curC = 0;
}

void setup() {
#ifdef TERM_FROM_USB
    Serial.setRxBufferSize(16384);         // hold a whole frame so 921600 can't overflow
    Serial.begin(BAUD);                    // USB carries the ANSI stream @921600
#else
    Serial.begin(115200);                  // USB: logs only
#endif
    tft.init();
    tft.setRotation(ROT);
    tft.setSwapBytes(true);                 // pushImage() buffers hold color565() order
    tft.fillScreen(TFT_BLACK);
    initPalette();
    // Take over TFT_BL with PWM (tft.init() left it digital-HIGH = full on).
    ledcSetup(BL_CH, 5000, 8);
    ledcAttachPin(BL_PIN, BL_CH);
    applyBacklight();
#ifndef TERM_FROM_USB
    Serial1.setRxBufferSize(16384);                // hold a whole frame (see above)
    Serial1.begin(BAUD, SERIAL_8N1, RX_PIN, -1);   // RX-only on RXD/GPIO3
#endif
    selfTest();
}

void loop() {
    bool any = false;
    while (INPORT.available()) { feed((uint8_t)INPORT.read()); any = true; }
    if (!any) flushRow();                  // stream paused: push the last pending row
}
