#include "sensor_sht3x.h"
#include "ClassLogFile.h"

#ifdef ENABLE_MQTT
#include "interface_mqtt.h"
#endif

#ifdef ENABLE_INFLUXDB
#include "interface_influxdb.h"
extern InfluxDB influxDB;
#endif

#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "SHT3x";

// SHT3x I2C commands
#define SHT3X_CMD_MEASURE_HIGH_REP 0x2400  // High repeatability measurement
#define SHT3X_CMD_SOFT_RESET       0x30A2  // Soft reset command

SensorSHT3x::SensorSHT3x(uint8_t address,
                         const std::string& mqttTopic,
                         const std::string& influxMeasurement,
                         int interval,
                         bool mqttEnabled,
                         bool influxEnabled)
    : _temperature(0.0f), _humidity(0.0f), _i2cAddress(address),
      _i2cPort(I2C_NUM_0), _initialized(false)
{
    _mqttTopic = mqttTopic;
    _influxMeasurement = influxMeasurement;
    _readInterval = interval;
    _mqttEnabled = mqttEnabled;
    _influxEnabled = influxEnabled;
    _lastRead = 0;
}

SensorSHT3x::~SensorSHT3x()
{
}

bool SensorSHT3x::init()
{
    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Initializing SHT3x sensor at address 0x" + 
                        std::to_string(_i2cAddress));
    
    // Try to communicate with the sensor - send soft reset
    uint8_t cmd[2];
    cmd[0] = (SHT3X_CMD_SOFT_RESET >> 8) & 0xFF;
    cmd[1] = SHT3X_CMD_SOFT_RESET & 0xFF;
    
    i2c_cmd_handle_t cmdHandle = i2c_cmd_link_create();
    i2c_master_start(cmdHandle);
    i2c_master_write_byte(cmdHandle, (_i2cAddress << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmdHandle, cmd, 2, true);
    i2c_master_stop(cmdHandle);
    
    esp_err_t ret = i2c_master_cmd_begin(_i2cPort, cmdHandle, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmdHandle);
    
    if (ret != ESP_OK) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to communicate with SHT3x: " + std::to_string(ret));
        return false;
    }
    
    vTaskDelay(pdMS_TO_TICKS(10));  // Wait for reset
    
    _initialized = true;
    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "SHT3x sensor initialized successfully");
    return true;
}

uint8_t SensorSHT3x::calculateCRC(const uint8_t* data, size_t len)
{
    // CRC-8 polynomial: 0x31 (x^8 + x^5 + x^4 + 1)
    uint8_t crc = 0xFF;
    
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x31;
            } else {
                crc = crc << 1;
            }
        }
    }
    
    return crc;
}

bool SensorSHT3x::measureAndRead(float& temp, float& hum)
{
    // Send measurement command (high repeatability)
    uint8_t cmd[2];
    cmd[0] = (SHT3X_CMD_MEASURE_HIGH_REP >> 8) & 0xFF;
    cmd[1] = SHT3X_CMD_MEASURE_HIGH_REP & 0xFF;
    
    i2c_cmd_handle_t cmdHandle = i2c_cmd_link_create();
    i2c_master_start(cmdHandle);
    i2c_master_write_byte(cmdHandle, (_i2cAddress << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmdHandle, cmd, 2, true);
    i2c_master_stop(cmdHandle);
    
    esp_err_t ret = i2c_master_cmd_begin(_i2cPort, cmdHandle, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmdHandle);
    
    if (ret != ESP_OK) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to send measurement command: " + std::to_string(ret));
        return false;
    }
    
    // Wait for measurement (datasheet: max 15ms for high repeatability)
    vTaskDelay(pdMS_TO_TICKS(20));
    
    // Read 6 bytes: temp MSB, temp LSB, temp CRC, hum MSB, hum LSB, hum CRC
    uint8_t data[6];
    cmdHandle = i2c_cmd_link_create();
    i2c_master_start(cmdHandle);
    i2c_master_write_byte(cmdHandle, (_i2cAddress << 1) | I2C_MASTER_READ, true);
    i2c_master_read(cmdHandle, data, 5, I2C_MASTER_ACK);
    i2c_master_read_byte(cmdHandle, &data[5], I2C_MASTER_NACK);
    i2c_master_stop(cmdHandle);
    
    ret = i2c_master_cmd_begin(_i2cPort, cmdHandle, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmdHandle);
    
    if (ret != ESP_OK) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to read measurement data: " + std::to_string(ret));
        return false;
    }
    
    // Verify CRC
    if (calculateCRC(&data[0], 2) != data[2]) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Temperature CRC mismatch");
        return false;
    }
    
    if (calculateCRC(&data[3], 2) != data[5]) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Humidity CRC mismatch");
        return false;
    }
    
    // Convert raw values to temperature and humidity
    uint16_t rawTemp = (data[0] << 8) | data[1];
    uint16_t rawHum = (data[3] << 8) | data[4];
    
    temp = -45.0f + 175.0f * (float)rawTemp / 65535.0f;
    hum = 100.0f * (float)rawHum / 65535.0f;
    
    return true;
}

bool SensorSHT3x::readData()
{
    if (!_initialized) {
        return false;
    }
    
    if (!shouldRead()) {
        return false;
    }
    
    float temp, hum;
    if (!measureAndRead(temp, hum)) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to read sensor data");
        return false;
    }
    
    _temperature = temp;
    _humidity = hum;
    _lastRead = time(nullptr);
    
    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Read: Temp=" + std::to_string(_temperature) + 
                        "°C, Humidity=" + std::to_string(_humidity) + "%");
    
    return true;
}

void SensorSHT3x::publishMQTT()
{
#ifdef ENABLE_MQTT
    if (!_mqttEnabled || !getMQTTisConnected()) {
        return;
    }
    
    // Publish temperature
    std::string tempTopic = _mqttTopic + "/temperature";
    std::string tempValue = std::to_string(_temperature);
    MQTTPublish(tempTopic, tempValue, 1, true);
    
    // Publish humidity
    std::string humTopic = _mqttTopic + "/humidity";
    std::string humValue = std::to_string(_humidity);
    MQTTPublish(humTopic, humValue, 1, true);
    
    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Published to MQTT");
#endif
}

void SensorSHT3x::publishInfluxDB()
{
#ifdef ENABLE_INFLUXDB
    if (!_influxEnabled) {
        return;
    }
    
    time_t now = time(nullptr);
    
    // Publish temperature
    influxDB.InfluxDBPublish(_influxMeasurement, 
                             "temperature", 
                             std::to_string(_temperature), 
                             now);
    
    // Publish humidity
    influxDB.InfluxDBPublish(_influxMeasurement, 
                             "humidity", 
                             std::to_string(_humidity), 
                             now);
    
    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Published to InfluxDB");
#endif
}
