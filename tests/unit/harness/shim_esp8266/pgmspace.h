/// pgmspace.h — host replacement of the ESP8266 Arduino core <pgmspace.h> (flash-string helpers)
/// Role: makes PSTR / F() / pgm_read_* and the *_P libc aliases plain RAM accesses on the host.
///       Force-included FIRST in the ESP8266 variant (before esphome/core/defines.h): otherwise
///       <cstring> would be included for the first time from inside namespace esphome.
/// Deps: libc headers, WString.h (this directory)
#pragma once
#include <cstring>
#include <cstdio>
#include <cstdint>
#include <cstdarg>
#include <strings.h>
#include "WString.h"

#define PGM_P const char *
#define PSTR(s) (s)
#define PROGMEM_STRING_DEF(x)
#define ICACHE_RODATA_ATTR
#define pgm_read_byte(a) (*(const uint8_t *) (a))
#define pgm_read_word(a) (*(const uint16_t *) (a))
#define pgm_read_ptr(a) (*(void *const *) (a))
#define memcpy_P memcpy
#define memcmp_P memcmp
#define strlen_P strlen
#define strnlen_P strnlen
#define strncpy_P strncpy
#define strcmp_P strcmp
#define strncmp_P strncmp
#define strcasecmp_P strcasecmp
#define strncasecmp_P strncasecmp
#define vsnprintf_P vsnprintf
#define snprintf_P snprintf
#define F(s) (reinterpret_cast<const __FlashStringHelper *>(PSTR(s)))
