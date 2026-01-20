#pragma once

#ifndef SENSOR_MANAGER_H
#define SENSOR_MANAGER_H

#include <string>
#include <vector>
#include <memory>
#include <time.h>

/**
 * @brief Base class for all sensors
 */
class SensorBase {
public:
    virtual ~SensorBase() = default;
    
    /**
     * @brief Initialize the sensor hardware
     * @return true if successful, false otherwise
     */
    virtual bool init() = 0;
    
    /**
     * @brief Read data from the sensor
     * @return true if successful, false otherwise
     */
    virtual bool readData() = 0;
    
    /**
     * @brief Publish sensor data to MQTT
     */
    virtual void publishMQTT() = 0;
    
    /**
     * @brief Publish sensor data to InfluxDB
     */
    virtual void publishInfluxDB() = 0;
    
    /**
     * @brief Get sensor name/identifier
     */
    virtual std::string getName() = 0;

protected:
    std::string _mqttTopic;
    std::string _influxMeasurement;
    int _readInterval;
    bool _mqttEnabled;
    bool _influxEnabled;
    time_t _lastRead = 0;
    
    /**
     * @brief Check if it's time to read the sensor
     * @return true if should read now
     */
    bool shouldRead() {
        return (time(nullptr) - _lastRead) >= _readInterval;
    }
};

/**
 * @brief Manager class for all sensors
 */
class SensorManager {
public:
    SensorManager();
    ~SensorManager();
    
    /**
     * @brief Initialize sensor manager and all configured sensors
     * @return true if successful, false otherwise
     */
    bool init();
    
    /**
     * @brief Update all sensors (read if interval elapsed, publish if needed)
     */
    void update();
    
    /**
     * @brief Clean up and deinitialize all sensors
     */
    void deinit();
    
    /**
     * @brief Read configuration and create sensor instances
     * @param configFile Path to config.ini file
     * @return true if successful, false otherwise
     */
    bool readConfig(const std::string& configFile);
    
    /**
     * @brief Check if sensor manager is enabled
     */
    bool isEnabled() { return _enabled; }
    
private:
    std::vector<std::unique_ptr<SensorBase>> _sensors;
    bool _enabled;
    bool _i2cInitialized;
    
    /**
     * @brief Initialize I2C bus
     * @param sda SDA GPIO pin
     * @param scl SCL GPIO pin
     * @param freq I2C frequency in Hz
     * @return true if successful, false otherwise
     */
    bool initI2C(int sda, int scl, uint32_t freq);
};

#endif // SENSOR_MANAGER_H
