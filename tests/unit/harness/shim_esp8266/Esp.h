/// Esp.h — host replacement of the ESP8266 Arduino core <Esp.h> (EspClass ESP, scheduler hooks)
/// Role: declarations only; the definitions live in platform/stubs_esp8266.cpp.
/// Deps: <cstdint>
#pragma once
#include <cstdint>

class EspClass {
 public:
  uint32_t getFreeHeap();
  uint32_t getMaxFreeBlockSize();
  uint32_t getChipId();
  void restart();
  uint8_t getHeapFragmentation();
};
extern EspClass ESP;

extern "C" {
void esp_schedule(void);
}
void esp_delay(unsigned long timeout_ms, bool (*blocked)(void));
