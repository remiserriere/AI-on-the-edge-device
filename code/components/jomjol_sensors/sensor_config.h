#pragma once

#ifndef SENSOR_CONFIG_H
#define SENSOR_CONFIG_H

#include <string>
#include <cstdint>

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

#endif // SENSOR_CONFIG_H
