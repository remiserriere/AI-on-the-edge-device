#pragma once

#include <string>
#include <cstdint>

/**
 * @brief DS18B20 1-Wire driver mode
 * 
 * USE_ONEWIRE_RMT: Use hardware-based RMT peripheral for precise timing
 *   - Benefits: Hardware timing, reduced CRC errors, better reliability, lower CPU overhead
 *   - Compatible with ESP32CAM
 *   - Enabled by default for testing
 * 
 * Software bit-banging (fallback): Software-based timing
 *   - Benefits: Proven reliability, no RMT channel usage
 *   - Drawbacks: Timing affected by interrupts, more CRC errors (mitigated by retries)
 *   - Set to 0 to use software mode
 */
#ifndef USE_ONEWIRE_RMT
#define USE_ONEWIRE_RMT 1  // Enable RMT for testing (set to 0 for software mode)
#endif

/**
 * @brief Configuration structure for sensors
 * 
 * This structure holds all configuration parameters for a sensor,
 * parsed from the config.ini file during ReadParameter phase.
 */
struct SensorConfig {
    bool enable = false;
    int interval = -1;  // -1 = follow flow (default), >0 = custom interval in seconds
    bool mqttEnable = true;
    std::string mqttTopic;
    bool influxEnable = false;
    std::string influxMeasurement;
    
    // SHT3x specific parameters
    uint8_t sht3xAddress = 0x44;  // Default I2C address
    uint32_t i2cFreq = 100000;    // Default 100kHz
};
