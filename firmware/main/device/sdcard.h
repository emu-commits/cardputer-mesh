// SD card bring-up: mount FATFS at /sdcard over the rear SPI2 bus.
//
// Self-contained (no Plai HAL dependency) but uses the SAME wiring Plai does:
// the microSD shares SPI2 with the LoRa radio — SCK=40, MOSI=14, MISO=39, and
// the card's own CS=12 (the radio's CS is 5, brought up later in step 4). We
// init the bus here; once the radio is added, spi_bus_initialize() returns
// ESP_ERR_INVALID_STATE (already up) which we treat as "shared", matching Plai.
#pragma once
#include <cstdint>
#include "driver/sdspi_host.h"
#include "driver/sdmmc_host.h"
#include "driver/spi_common.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

namespace device {

class SdCard {
public:
    static constexpr int PIN_MISO = 39, PIN_MOSI = 14, PIN_CLK = 40, PIN_CS = 12;
    static constexpr const char* MOUNT = "/sdcard";

    bool mount() {
        if (mounted_) return true;

        spi_bus_config_t bus = {};
        bus.mosi_io_num = PIN_MOSI;
        bus.miso_io_num = PIN_MISO;
        bus.sclk_io_num = PIN_CLK;
        bus.quadwp_io_num = -1;
        bus.quadhd_io_num = -1;
        bus.max_transfer_sz = 4092;
        esp_err_t e = spi_bus_initialize((spi_host_device_t)host_.slot, &bus, SDSPI_DEFAULT_DMA);
        if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return false;  // INVALID_STATE = shared w/ radio
        bus_shared_ = (e == ESP_ERR_INVALID_STATE);

        sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
        slot.gpio_cs = (gpio_num_t)PIN_CS;
        slot.host_id = (spi_host_device_t)host_.slot;

        esp_vfs_fat_sdmmc_mount_config_t mcfg = {};
        mcfg.format_if_mount_failed = false;   // never reformat the user's card
        mcfg.max_files = 5;
        mcfg.allocation_unit_size = 16 * 1024;

        e = esp_vfs_fat_sdspi_mount(MOUNT, &host_, &slot, &mcfg, &card_);
        if (e != ESP_OK) {
            card_ = nullptr;
            if (!bus_shared_) spi_bus_free((spi_host_device_t)host_.slot);
            return false;
        }
        mounted_ = true;
        return true;
    }

    bool mounted() const { return mounted_; }
    uint64_t capacity() const {
        return card_ ? (uint64_t)card_->csd.capacity * card_->csd.sector_size : 0;
    }

private:
    sdmmc_host_t host_ = SDSPI_HOST_DEFAULT();
    sdmmc_card_t* card_ = nullptr;
    bool mounted_ = false;
    bool bus_shared_ = false;
};

} // namespace device
