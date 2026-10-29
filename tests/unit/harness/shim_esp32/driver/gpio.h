#pragma once
#include "esp_err.h"
typedef int gpio_num_t;
typedef enum { GPIO_PULLUP_ONLY, GPIO_PULLDOWN_ONLY, GPIO_FLOATING } gpio_pull_mode_t;
extern "C" { esp_err_t gpio_reset_pin(gpio_num_t); esp_err_t gpio_set_pull_mode(gpio_num_t, gpio_pull_mode_t); }
