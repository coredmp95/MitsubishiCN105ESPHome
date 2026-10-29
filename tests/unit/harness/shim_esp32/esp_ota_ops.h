#pragma once
#include "esp_err.h"
#include "esp_bootloader_desc.h"
extern "C" { esp_err_t esp_ota_get_bootloader_description(const void*, esp_bootloader_desc_t*); }
