/* 模拟器桩：芯片信息 / IDF 版本 / MAC */
#include <string.h>

#include "esp_chip_info.h"
#include "esp_idf_version.h"
#include "esp_mac.h"

const char *esp_get_idf_version(void)
{
    return "5.5.1 (sim)";
}

void esp_chip_info(esp_chip_info_t *out_info)
{
    if (!out_info) return;
    out_info->model = CHIP_ESP32S3;
    out_info->features = 0;
    out_info->cores = 2;
    out_info->revision = 0;
    out_info->rom_version = "sim";
}

void esp_read_mac(uint8_t *mac, int type)
{
    static const uint8_t kMac[6] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01 };
    (void)type;
    if (mac) {
        memcpy(mac, kMac, 6);
    }
}
