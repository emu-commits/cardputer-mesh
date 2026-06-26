// Battery gauge — the Cardputer ADV reads its 1S LiPo through a 2:1 divider on
// ADC1 channel 9 (GPIO10). There is no PMIC / fuel-gauge IC on this board, so:
//   * voltage + percent come from a calibrated ADC read + a LiPo discharge curve
//   * "charge state" can only be INFERRED (no charge/VBUS pin is broken out):
//     the pack pinned high (>~4.18 V) or rising between samples == on the charger.
// Ported from Plai's HAL::Battery (Apache-2.0, d4rkmen) — same divider + curve,
// trimmed to a self-contained header with no Plai HAL dependency.
#pragma once
#include <cstdint>
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"

namespace device {

class Battery {
public:
    static constexpr adc_unit_t    ADC_UNIT    = ADC_UNIT_1;
    static constexpr adc_channel_t ADC_CHANNEL = ADC_CHANNEL_9;   // GPIO10 on the S3
    static constexpr adc_atten_t   ADC_ATTEN   = ADC_ATTEN_DB_12;
    static constexpr float         DIVIDER     = 1.5f;            // board divider (Plai's MEASUREMENT_OFFSET)

    void begin() {
        adc_oneshot_unit_init_cfg_t init = {};
        init.unit_id = ADC_UNIT;
        if (adc_oneshot_new_unit(&init, &adc_) != ESP_OK) { adc_ = nullptr; return; }
        adc_oneshot_chan_cfg_t ch = {};
        ch.atten = ADC_ATTEN;
        ch.bitwidth = ADC_BITWIDTH_DEFAULT;
        adc_oneshot_config_channel(adc_, ADC_CHANNEL, &ch);
        calibrated_ = init_calibration();
        ESP_LOGI("battery", "ADC ready (calibrated=%d)", calibrated_);
    }

    bool present() const { return adc_ != nullptr; }

    // Pack voltage in volts (0 if the ADC isn't up or the read failed).
    float voltage() {
        if (!adc_) return 0.0f;
        int raw = 0;
        if (adc_oneshot_read(adc_, ADC_CHANNEL, &raw) != ESP_OK) return 0.0f;
        int mv = 0;
        if (calibrated_ && adc_cali_raw_to_voltage(cali_, raw, &mv) == ESP_OK)
            return (float)mv * 3.0f / 1000.0f / DIVIDER;
        // Uncalibrated fallback: 12-bit, 12 dB atten ≈ 0..3.1 V full-scale.
        return (float)raw / 4095.0f * 3.1f * 3.0f / DIVIDER;
    }

    // 0..100 % from a 1S LiPo discharge curve (interpolated).
    static uint8_t level(float v) {
        static const struct { float v; uint8_t p; } curve[] = {
            {4.20f,100},{4.15f,95},{4.11f,90},{4.08f,85},{4.02f,80},{3.98f,75},
            {3.95f,70},{3.91f,65},{3.87f,60},{3.85f,55},{3.84f,50},{3.82f,45},
            {3.80f,40},{3.79f,35},{3.77f,30},{3.75f,25},{3.73f,20},{3.71f,15},
            {3.69f,10},{3.61f,5},{3.27f,0}};
        const int n = sizeof(curve) / sizeof(curve[0]);
        if (v >= curve[0].v) return 100;
        if (v <= curve[n-1].v) return 0;
        for (int i = 0; i < n - 1; ++i)
            if (v >= curve[i+1].v) {
                float t = (v - curve[i+1].v) / (curve[i].v - curve[i+1].v);
                return (uint8_t)(curve[i+1].p + t * (curve[i].p - curve[i+1].p) + 0.5f);
            }
        return 0;
    }

private:
    bool init_calibration() {
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
        adc_cali_curve_fitting_config_t c = {};
        c.unit_id = ADC_UNIT; c.chan = ADC_CHANNEL; c.atten = ADC_ATTEN; c.bitwidth = ADC_BITWIDTH_DEFAULT;
        if (adc_cali_create_scheme_curve_fitting(&c, &cali_) == ESP_OK) return true;
#endif
#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
        adc_cali_line_fitting_config_t l = {};
        l.unit_id = ADC_UNIT; l.atten = ADC_ATTEN; l.bitwidth = ADC_BITWIDTH_DEFAULT;
        if (adc_cali_create_scheme_line_fitting(&l, &cali_) == ESP_OK) return true;
#endif
        return false;  // eFuse not burnt -> uncalibrated fallback in voltage()
    }

    adc_oneshot_unit_handle_t adc_ = nullptr;
    adc_cali_handle_t         cali_ = nullptr;
    bool                      calibrated_ = false;
};

} // namespace device
