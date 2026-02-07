#include "onewire_rmt.h"
#include "esp_log.h"
#include "rom/ets_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

// Check ESP-IDF version to use appropriate RMT API
#if defined(ESP_IDF_VERSION)
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
#define ONEWIRE_NEW_RMT_DRIVER 1
#include "driver/rmt_tx.h"
#include "driver/rmt_rx.h"
#else
#define ONEWIRE_NEW_RMT_DRIVER 0
#include "driver/rmt.h"
#endif
#else
#define ONEWIRE_NEW_RMT_DRIVER 0
#include "driver/rmt.h"
#endif

static const char *TAG = "ONEWIRE_RMT";

// 1-Wire timing parameters (in microseconds)
// Based on DS18B20 datasheet specifications
#define OW_RESET_PULSE_TIME         480   // Reset pulse duration
#define OW_RESET_WAIT_TIME          70    // Wait before sampling presence pulse
#define OW_RESET_RELEASE_TIME       410   // Wait for presence pulse to complete
#define OW_WRITE_1_LOW_TIME         6     // Write 1: pull low time
#define OW_WRITE_1_RELEASE_TIME     64    // Write 1: release time
#define OW_WRITE_0_LOW_TIME         60    // Write 0: pull low time
#define OW_WRITE_0_RELEASE_TIME     10    // Write 0: release time
#define OW_READ_INIT_TIME           3     // Read: initial pull low
#define OW_READ_WAIT_TIME           10    // Read: wait before sampling
#define OW_READ_RELEASE_TIME        53    // Read: remaining time slot

// RMT configuration
#define OW_RMT_CLK_DIV              80    // 80MHz / 80 = 1MHz = 1μs resolution
#define OW_RMT_MEM_BLOCK_NUM        1     // Memory blocks per channel

#if ONEWIRE_NEW_RMT_DRIVER

// IDF v5.x implementation using new RMT TX/RX API

typedef struct {
    rmt_channel_handle_t tx_channel;
    rmt_channel_handle_t rx_channel;
    rmt_encoder_handle_t copy_encoder;
    rmt_receive_config_t rx_config;
} onewire_rmt_handle_v5_t;

esp_err_t onewire_rmt_init(onewire_rmt_t* ow, gpio_num_t gpio)
{
    if (ow == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(ow, 0, sizeof(onewire_rmt_t));
    ow->gpio = gpio;
    ow->initialized = false;

    // Allocate handle structure
    onewire_rmt_handle_v5_t* handle = (onewire_rmt_handle_v5_t*)malloc(sizeof(onewire_rmt_handle_v5_t));
    if (handle == NULL) {
        ESP_LOGE(TAG, "Failed to allocate RMT handle");
        return ESP_ERR_NO_MEM;
    }
    memset(handle, 0, sizeof(onewire_rmt_handle_v5_t));
    ow->rmt_handle = handle;

    // Configure TX channel for writing
    rmt_tx_channel_config_t tx_config = {
        .gpio_num = gpio,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 1000000,  // 1MHz = 1μs resolution
        .mem_block_symbols = 64,
        .trans_queue_depth = 1,
        .flags = {
            .invert_out = false,
            .with_dma = false,
            .io_loop_back = false,
            .io_od_mode = true,  // Open-drain mode for 1-Wire
        }
    };

    esp_err_t ret = rmt_new_tx_channel(&tx_config, &handle->tx_channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create RMT TX channel: %s", esp_err_to_name(ret));
        free(handle);
        return ret;
    }

    // Configure RX channel for reading
    rmt_rx_channel_config_t rx_config = {
        .gpio_num = gpio,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 1000000,  // 1MHz = 1μs resolution
        .mem_block_symbols = 64,
        .flags = {
            .invert_in = false,
            .with_dma = false,
            .io_loop_back = false,
        }
    };

    ret = rmt_new_rx_channel(&rx_config, &handle->rx_channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create RMT RX channel: %s", esp_err_to_name(ret));
        rmt_del_channel(handle->tx_channel);
        free(handle);
        return ret;
    }

    // Create copy encoder for simple data transmission
    rmt_copy_encoder_config_t copy_encoder_config = {};
    ret = rmt_new_copy_encoder(&copy_encoder_config, &handle->copy_encoder);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create copy encoder: %s", esp_err_to_name(ret));
        rmt_del_channel(handle->tx_channel);
        rmt_del_channel(handle->rx_channel);
        free(handle);
        return ret;
    }

    // Enable channels
    ret = rmt_enable(handle->tx_channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable TX channel: %s", esp_err_to_name(ret));
        rmt_del_encoder(handle->copy_encoder);
        rmt_del_channel(handle->tx_channel);
        rmt_del_channel(handle->rx_channel);
        free(handle);
        return ret;
    }

    ret = rmt_enable(handle->rx_channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable RX channel: %s", esp_err_to_name(ret));
        rmt_disable(handle->tx_channel);
        rmt_del_encoder(handle->copy_encoder);
        rmt_del_channel(handle->tx_channel);
        rmt_del_channel(handle->rx_channel);
        free(handle);
        return ret;
    }

    // Configure RX receive config
    handle->rx_config.signal_range_min_ns = 1000;  // 1μs minimum
    handle->rx_config.signal_range_max_ns = 1000000;  // 1000μs maximum

    ow->initialized = true;
    ESP_LOGI(TAG, "1-Wire RMT initialized on GPIO%d (IDF v5.x)", gpio);
    
    return ESP_OK;
}

void onewire_rmt_deinit(onewire_rmt_t* ow)
{
    if (ow == NULL || !ow->initialized || ow->rmt_handle == NULL) {
        return;
    }

    onewire_rmt_handle_v5_t* handle = (onewire_rmt_handle_v5_t*)ow->rmt_handle;

    rmt_disable(handle->tx_channel);
    rmt_disable(handle->rx_channel);
    rmt_del_encoder(handle->copy_encoder);
    rmt_del_channel(handle->tx_channel);
    rmt_del_channel(handle->rx_channel);
    
    free(handle);
    ow->rmt_handle = NULL;
    ow->initialized = false;
}

bool onewire_rmt_reset(onewire_rmt_t* ow)
{
    if (ow == NULL || !ow->initialized || ow->rmt_handle == NULL) {
        return false;
    }

    onewire_rmt_handle_v5_t* handle = (onewire_rmt_handle_v5_t*)ow->rmt_handle;

    // Create reset pulse: pull low for 480μs
    rmt_symbol_word_t reset_pulse = {
        .level0 = 0,
        .duration0 = OW_RESET_PULSE_TIME,
        .level1 = 1,
        .duration1 = OW_RESET_WAIT_TIME
    };

    rmt_transmit_config_t tx_config = {
        .loop_count = 0,
        .flags = {
            .eot_level = 1,
        }
    };

    esp_err_t ret = rmt_transmit(handle->tx_channel, handle->copy_encoder, 
                                 &reset_pulse, sizeof(reset_pulse), &tx_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to transmit reset pulse: %s", esp_err_to_name(ret));
        return false;
    }

    // Wait for transmission to complete
    ret = rmt_tx_wait_all_done(handle->tx_channel, 1000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Reset pulse timeout: %s", esp_err_to_name(ret));
        return false;
    }

    // Small delay for presence pulse to stabilize
    ets_delay_us(5);

    // Read the bus to check for presence pulse
    // If device is present, it pulls line low
    gpio_set_direction(ow->gpio, GPIO_MODE_INPUT);
    int presence = !gpio_get_level(ow->gpio);

    // Wait for presence pulse to complete
    ets_delay_us(OW_RESET_RELEASE_TIME);

    return presence;
}

void onewire_rmt_write_bit(onewire_rmt_t* ow, uint8_t bit)
{
    if (ow == NULL || !ow->initialized || ow->rmt_handle == NULL) {
        return;
    }

    onewire_rmt_handle_v5_t* handle = (onewire_rmt_handle_v5_t*)ow->rmt_handle;

    rmt_symbol_word_t bit_pulse;
    
    if (bit) {
        // Write 1: pull low for 6μs, then release for 64μs
        bit_pulse.level0 = 0;
        bit_pulse.duration0 = OW_WRITE_1_LOW_TIME;
        bit_pulse.level1 = 1;
        bit_pulse.duration1 = OW_WRITE_1_RELEASE_TIME;
    } else {
        // Write 0: pull low for 60μs, then release for 10μs
        bit_pulse.level0 = 0;
        bit_pulse.duration0 = OW_WRITE_0_LOW_TIME;
        bit_pulse.level1 = 1;
        bit_pulse.duration1 = OW_WRITE_0_RELEASE_TIME;
    }

    rmt_transmit_config_t tx_config = {
        .loop_count = 0,
        .flags = {
            .eot_level = 1,
        }
    };

    esp_err_t ret = rmt_transmit(handle->tx_channel, handle->copy_encoder,
                                 &bit_pulse, sizeof(bit_pulse), &tx_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write bit: %s", esp_err_to_name(ret));
        return;
    }

    // Wait for transmission to complete
    rmt_tx_wait_all_done(handle->tx_channel, 1000);
}

uint8_t onewire_rmt_read_bit(onewire_rmt_t* ow)
{
    if (ow == NULL || !ow->initialized || ow->rmt_handle == NULL) {
        return 0;
    }

    onewire_rmt_handle_v5_t* handle = (onewire_rmt_handle_v5_t*)ow->rmt_handle;

    // Initiate read slot: pull low for 3μs
    rmt_symbol_word_t read_init = {
        .level0 = 0,
        .duration0 = OW_READ_INIT_TIME,
        .level1 = 1,
        .duration1 = OW_READ_WAIT_TIME
    };

    rmt_transmit_config_t tx_config = {
        .loop_count = 0,
        .flags = {
            .eot_level = 1,
        }
    };

    esp_err_t ret = rmt_transmit(handle->tx_channel, handle->copy_encoder,
                                 &read_init, sizeof(read_init), &tx_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initiate read: %s", esp_err_to_name(ret));
        return 0;
    }

    // Wait for transmission to complete
    rmt_tx_wait_all_done(handle->tx_channel, 1000);

    // Read the bus
    gpio_set_direction(ow->gpio, GPIO_MODE_INPUT);
    uint8_t bit = gpio_get_level(ow->gpio);

    // Wait for rest of time slot
    ets_delay_us(OW_READ_RELEASE_TIME);

    return bit;
}

#else

// IDF v4.x implementation using legacy RMT API

esp_err_t onewire_rmt_init(onewire_rmt_t* ow, gpio_num_t gpio)
{
    if (ow == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(ow, 0, sizeof(onewire_rmt_t));
    ow->gpio = gpio;
    ow->initialized = false;

    // Find an available RMT channel
    // Try to find a free channel, preferring higher-numbered channels
    // to avoid conflicts with LED control (typically uses lower channels)
    // Note: RMT_CHANNEL_MAX varies by ESP32 variant (4-8 channels)
    rmt_channel_t channel = RMT_CHANNEL_MAX;
    int start_channel = (RMT_CHANNEL_MAX > 4) ? 4 : (RMT_CHANNEL_MAX / 2);
    
    // First try higher channels
    for (int i = start_channel; i < RMT_CHANNEL_MAX; i++) {
        rmt_config_t config = {
            .rmt_mode = RMT_MODE_TX,
            .channel = (rmt_channel_t)i,
            .gpio_num = gpio,
            .clk_div = OW_RMT_CLK_DIV,
            .mem_block_num = OW_RMT_MEM_BLOCK_NUM,
            .flags = 0,
            .tx_config = {
                .carrier_en = false,
                .loop_en = false,
                .idle_output_en = true,
                .idle_level = RMT_IDLE_LEVEL_HIGH,
            }
        };

        esp_err_t ret = rmt_config(&config);
        if (ret == ESP_OK) {
            ret = rmt_driver_install(config.channel, 0, 0);
            if (ret == ESP_OK) {
                channel = (rmt_channel_t)i;
                break;
            }
        }
    }
    
    // If no higher channel found, try lower channels as fallback
    if (channel == RMT_CHANNEL_MAX) {
        for (int i = 0; i < start_channel; i++) {
            rmt_config_t config = {
                .rmt_mode = RMT_MODE_TX,
                .channel = (rmt_channel_t)i,
                .gpio_num = gpio,
                .clk_div = OW_RMT_CLK_DIV,
                .mem_block_num = OW_RMT_MEM_BLOCK_NUM,
                .flags = 0,
                .tx_config = {
                    .carrier_en = false,
                    .loop_en = false,
                    .idle_output_en = true,
                    .idle_level = RMT_IDLE_LEVEL_HIGH,
                }
            };

            esp_err_t ret = rmt_config(&config);
            if (ret == ESP_OK) {
                ret = rmt_driver_install(config.channel, 0, 0);
                if (ret == ESP_OK) {
                    channel = (rmt_channel_t)i;
                    break;
                }
            }
        }
    }

    if (channel == RMT_CHANNEL_MAX) {
        ESP_LOGE(TAG, "No available RMT channel for 1-Wire");
        return ESP_ERR_NOT_FOUND;
    }

    ow->rmt_channel = channel;
    ow->initialized = true;

    // Configure GPIO as open-drain
    gpio_set_direction(gpio, GPIO_MODE_INPUT_OUTPUT_OD);
    gpio_set_pull_mode(gpio, GPIO_PULLUP_ONLY);

    ESP_LOGI(TAG, "1-Wire RMT initialized on GPIO%d using RMT channel %d (IDF v4.x)", 
             gpio, channel);
    
    return ESP_OK;
}

void onewire_rmt_deinit(onewire_rmt_t* ow)
{
    if (ow == NULL || !ow->initialized) {
        return;
    }

    rmt_driver_uninstall((rmt_channel_t)ow->rmt_channel);
    ow->initialized = false;
}

bool onewire_rmt_reset(onewire_rmt_t* ow)
{
    if (ow == NULL || !ow->initialized) {
        return false;
    }

    // Create reset pulse using RMT
    rmt_item32_t reset_item;
    reset_item.level0 = 0;  // Pull low
    reset_item.duration0 = OW_RESET_PULSE_TIME;
    reset_item.level1 = 1;  // Release (pulled high by resistor)
    reset_item.duration1 = OW_RESET_WAIT_TIME;

    esp_err_t ret = rmt_write_items((rmt_channel_t)ow->rmt_channel, &reset_item, 1, true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send reset pulse: %s", esp_err_to_name(ret));
        return false;
    }

    // Small delay for signal to stabilize
    ets_delay_us(5);

    // Read presence pulse
    gpio_set_direction(ow->gpio, GPIO_MODE_INPUT);
    int presence = !gpio_get_level(ow->gpio);

    // Wait for presence pulse to complete
    ets_delay_us(OW_RESET_RELEASE_TIME);

    return presence;
}

void onewire_rmt_write_bit(onewire_rmt_t* ow, uint8_t bit)
{
    if (ow == NULL || !ow->initialized) {
        return;
    }

    rmt_item32_t bit_item;
    
    if (bit) {
        // Write 1
        bit_item.level0 = 0;
        bit_item.duration0 = OW_WRITE_1_LOW_TIME;
        bit_item.level1 = 1;
        bit_item.duration1 = OW_WRITE_1_RELEASE_TIME;
    } else {
        // Write 0
        bit_item.level0 = 0;
        bit_item.duration0 = OW_WRITE_0_LOW_TIME;
        bit_item.level1 = 1;
        bit_item.duration1 = OW_WRITE_0_RELEASE_TIME;
    }

    rmt_write_items((rmt_channel_t)ow->rmt_channel, &bit_item, 1, true);
}

uint8_t onewire_rmt_read_bit(onewire_rmt_t* ow)
{
    if (ow == NULL || !ow->initialized) {
        return 0;
    }

    // Initiate read slot
    rmt_item32_t read_item;
    read_item.level0 = 0;  // Pull low briefly
    read_item.duration0 = OW_READ_INIT_TIME;
    read_item.level1 = 1;  // Release
    read_item.duration1 = OW_READ_WAIT_TIME;

    rmt_write_items((rmt_channel_t)ow->rmt_channel, &read_item, 1, true);

    // Read the bit
    gpio_set_direction(ow->gpio, GPIO_MODE_INPUT);
    uint8_t bit = gpio_get_level(ow->gpio);

    // Wait for rest of time slot
    ets_delay_us(OW_READ_RELEASE_TIME);

    return bit;
}

#endif

// Common functions for both IDF versions

void onewire_rmt_write_byte(onewire_rmt_t* ow, uint8_t byte)
{
    for (int i = 0; i < 8; i++) {
        onewire_rmt_write_bit(ow, (byte >> i) & 0x01);
    }
}

uint8_t onewire_rmt_read_byte(onewire_rmt_t* ow)
{
    uint8_t byte = 0;
    for (int i = 0; i < 8; i++) {
        byte |= (onewire_rmt_read_bit(ow) << i);
    }
    return byte;
}

void onewire_rmt_write_bytes(onewire_rmt_t* ow, const uint8_t* buf, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        onewire_rmt_write_byte(ow, buf[i]);
    }
}

void onewire_rmt_read_bytes(onewire_rmt_t* ow, uint8_t* buf, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        buf[i] = onewire_rmt_read_byte(ow);
    }
}
