#pragma once
#include <cstdint>
typedef struct { uint16_t revision; uint8_t cores; } esp_chip_info_t;
extern "C" { void esp_chip_info(esp_chip_info_t*); }
