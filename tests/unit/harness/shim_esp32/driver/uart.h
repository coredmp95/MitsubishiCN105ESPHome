#pragma once
#include "esp_err.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
typedef int uart_port_t;
#define UART_NUM_0 0
#define UART_NUM_1 1
#define UART_NUM_2 2
#define UART_PIN_NO_CHANGE (-1)
typedef enum { UART_DATA_8_BITS = 3 } uart_word_length_t;
typedef enum { UART_PARITY_EVEN = 2 } uart_parity_t;
typedef enum { UART_STOP_BITS_1 = 1 } uart_stop_bits_t;
typedef enum { UART_HW_FLOWCTRL_DISABLE = 0 } uart_hw_flowcontrol_t;
typedef enum { UART_MODE_UART = 0 } uart_mode_t;
typedef enum { UART_SIGNAL_INV_DISABLE = 0 } uart_signal_inv_t;
#define UART_SCLK_APB 1
typedef int uart_sclk_t;
typedef struct { int baud_rate; uart_word_length_t data_bits; uart_parity_t parity; uart_stop_bits_t stop_bits; uart_hw_flowcontrol_t flow_ctrl; uint8_t rx_flow_ctrl_thresh; } uart_config_t;
extern "C" {
esp_err_t uart_param_config(uart_port_t, const uart_config_t*);
esp_err_t uart_set_pin(uart_port_t, int, int, int, int);
esp_err_t uart_set_mode(uart_port_t, uart_mode_t);
esp_err_t uart_wait_tx_done(uart_port_t, TickType_t);
esp_err_t uart_set_sclk(uart_port_t, uart_sclk_t);
esp_err_t uart_set_baudrate(uart_port_t, uint32_t);
esp_err_t uart_set_line_inverse(uart_port_t, uint32_t);
esp_err_t uart_set_hw_flow_ctrl(uart_port_t, uart_hw_flowcontrol_t, uint8_t);
esp_err_t uart_set_rx_timeout(uart_port_t, uint8_t);
esp_err_t uart_flush_input(uart_port_t);
esp_err_t uart_get_baudrate(uart_port_t, uint32_t*);
}
