// UART Terminal sink — the device side of the ui::Terminal seam. The brain
// renders into a TextCanvas, AnsiRenderer diffs it to ANSI bytes, and this sink
// pushes those bytes out Port-A to the CYD (the dumb VT100 terminal).
//
// Wiring: Cardputer Port-A Grove G1 (GPIO1) = TX -> CYD GPIO35 (RX). RX-only
// device, so we never use a return line. UART1 (UART0=USB console, UART2=GPS).
#pragma once
#include <string>
#include "driver/uart.h"
#include "core/ansi.h"

namespace device {

class UartTerminal : public ui::Terminal {
public:
    explicit UartTerminal(uart_port_t port = UART_NUM_1) : port_(port) {}

    void begin(int tx_pin, int baud) {
        uart_config_t cfg = {};
        cfg.baud_rate = baud;
        cfg.data_bits = UART_DATA_8_BITS;
        cfg.parity = UART_PARITY_DISABLE;
        cfg.stop_bits = UART_STOP_BITS_1;
        cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
        cfg.source_clk = UART_SCLK_DEFAULT;
        // rx buffer is unused (TX-only) but must exceed the 128-byte HW FIFO;
        // a generous TX ring lets full-frame writes return without blocking.
        uart_driver_install(port_, 256, 8192, 0, nullptr, 0);
        uart_param_config(port_, &cfg);
        uart_set_pin(port_, tx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    }

    void write(const std::string& bytes) override {
        uart_write_bytes(port_, bytes.data(), bytes.size());
    }

private:
    uart_port_t port_;
};

} // namespace device
