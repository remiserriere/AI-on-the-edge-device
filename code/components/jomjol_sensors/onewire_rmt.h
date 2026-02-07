#pragma once

#ifndef ONEWIRE_RMT_H
#define ONEWIRE_RMT_H

#include "driver/gpio.h"
#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Hardware-based 1-Wire driver using ESP32 RMT peripheral
 * 
 * This implementation uses the RMT (Remote Control Transceiver) peripheral
 * for precise hardware-based timing, eliminating timing issues caused by
 * software bit-banging and CPU interrupts.
 * 
 * Benefits over software bit-banging:
 * - Hardware-based precise timing (not affected by interrupts)
 * - Reduced CRC errors
 * - Better sensor detection reliability
 * - Lower CPU overhead
 */

typedef struct {
    gpio_num_t gpio;
    int rmt_channel;
    void* rmt_handle;  // RMT channel handle (implementation-specific)
    bool initialized;
} onewire_rmt_t;

/**
 * @brief Initialize 1-Wire bus using RMT
 * @param ow Pointer to onewire_rmt_t structure
 * @param gpio GPIO pin for 1-Wire bus
 * @return ESP_OK on success
 */
esp_err_t onewire_rmt_init(onewire_rmt_t* ow, gpio_num_t gpio);

/**
 * @brief Deinitialize 1-Wire bus
 * @param ow Pointer to onewire_rmt_t structure
 */
void onewire_rmt_deinit(onewire_rmt_t* ow);

/**
 * @brief Perform 1-Wire reset and check for presence pulse
 * @param ow Pointer to onewire_rmt_t structure
 * @return true if device present, false otherwise
 */
bool onewire_rmt_reset(onewire_rmt_t* ow);

/**
 * @brief Write a single bit to the 1-Wire bus
 * @param ow Pointer to onewire_rmt_t structure
 * @param bit Bit value (0 or 1)
 */
void onewire_rmt_write_bit(onewire_rmt_t* ow, uint8_t bit);

/**
 * @brief Read a single bit from the 1-Wire bus
 * @param ow Pointer to onewire_rmt_t structure
 * @return Bit value (0 or 1)
 */
uint8_t onewire_rmt_read_bit(onewire_rmt_t* ow);

/**
 * @brief Write a byte to the 1-Wire bus
 * @param ow Pointer to onewire_rmt_t structure
 * @param byte Byte to write
 */
void onewire_rmt_write_byte(onewire_rmt_t* ow, uint8_t byte);

/**
 * @brief Read a byte from the 1-Wire bus
 * @param ow Pointer to onewire_rmt_t structure
 * @return Byte read from bus
 */
uint8_t onewire_rmt_read_byte(onewire_rmt_t* ow);

/**
 * @brief Write multiple bytes to the 1-Wire bus
 * @param ow Pointer to onewire_rmt_t structure
 * @param buf Buffer containing bytes to write
 * @param len Number of bytes to write
 */
void onewire_rmt_write_bytes(onewire_rmt_t* ow, const uint8_t* buf, size_t len);

/**
 * @brief Read multiple bytes from the 1-Wire bus
 * @param ow Pointer to onewire_rmt_t structure
 * @param buf Buffer to store read bytes
 * @param len Number of bytes to read
 */
void onewire_rmt_read_bytes(onewire_rmt_t* ow, uint8_t* buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif // ONEWIRE_RMT_H
