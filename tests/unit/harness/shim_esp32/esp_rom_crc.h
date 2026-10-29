#pragma once
#include <cstdint>
extern "C" { uint16_t esp_rom_crc16_le(uint16_t, const uint8_t*, uint32_t); uint16_t esp_rom_crc16_be(uint16_t, const uint8_t*, uint32_t); }
