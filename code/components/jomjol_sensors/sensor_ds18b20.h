#pragma once

#ifndef SENSOR_DS18B20_H
#define SENSOR_DS18B20_H

#include "sensor_manager.h"
#include "driver/gpio.h"

/**
 * @brief DS18B20 Temperature Sensor (1-Wire)
 */
class SensorDS18B20 : public SensorBase {
public:
    /**
     * @brief Construct a new SensorDS18B20 object
     * @param gpio GPIO pin for 1-Wire bus
     * @param mqttTopic MQTT topic for publishing
     * @param influxMeasurement InfluxDB measurement name
     * @param interval Read interval in seconds
     * @param mqttEnabled Enable MQTT publishing
     * @param influxEnabled Enable InfluxDB publishing
     */
    SensorDS18B20(gpio_num_t gpio,
                  const std::string& mqttTopic,
                  const std::string& influxMeasurement,
                  int interval,
                  bool mqttEnabled,
                  bool influxEnabled);
    
    virtual ~SensorDS18B20();
    
    bool init() override;
    bool readData() override;
    void publishMQTT() override;
    void publishInfluxDB() override;
    std::string getName() override { return "DS18B20"; }
    
    /**
     * @brief Get number of detected sensors on the bus
     */
    int getSensorCount() const { return _temperatures.size(); }
    
    /**
     * @brief Get temperature reading for specific sensor
     * @param index Sensor index
     * @return Temperature in Celsius
     */
    float getTemperature(int index = 0) const;
    
private:
    std::vector<float> _temperatures;
    gpio_num_t _gpio;
    bool _initialized;
    
    /**
     * @brief Scan the 1-Wire bus for DS18B20 devices
     * @return Number of devices found
     */
    int scanDevices();
    
    /**
     * @brief Read temperature from a single DS18B20 sensor
     * @param temp Output temperature value
     * @return true if successful
     */
    bool readOneSensor(float& temp);
};
};

#endif // SENSOR_DS18B20_H
