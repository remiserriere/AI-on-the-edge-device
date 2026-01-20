#include "sensor_ds18b20.h"
#include "ClassLogFile.h"

#ifdef ENABLE_MQTT
#include "interface_mqtt.h"
#endif

#ifdef ENABLE_INFLUXDB
#include "interface_influxdb.h"
extern InfluxDB influxDB;
#endif

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "rom/ets_sys.h"

static const char *TAG = "DS18B20";

// DS18B20 commands
#define DS18B20_CMD_SKIP_ROM        0xCC
#define DS18B20_CMD_CONVERT_T       0x44
#define DS18B20_CMD_READ_SCRATCHPAD 0xBE
#define DS18B20_CMD_SEARCH_ROM      0xF0

SensorDS18B20::SensorDS18B20(gpio_num_t gpio,
                             const std::string& mqttTopic,
                             const std::string& influxMeasurement,
                             int interval,
                             bool mqttEnabled,
                             bool influxEnabled)
    : _gpio(gpio), _busHandle(nullptr), _initialized(false)
{
    _mqttTopic = mqttTopic;
    _influxMeasurement = influxMeasurement;
    _readInterval = interval;
    _mqttEnabled = mqttEnabled;
    _influxEnabled = influxEnabled;
    _lastRead = 0;
}

SensorDS18B20::~SensorDS18B20()
{
    if (_initialized) {
        gpio_reset_pin(_gpio);
    }
}

// Simple 1-Wire bit-banging functions
static inline void ow_set_high(gpio_num_t gpio)
{
    gpio_set_direction(gpio, GPIO_MODE_INPUT);  // High-Z (pulled up externally)
}

static inline void ow_set_low(gpio_num_t gpio)
{
    gpio_set_direction(gpio, GPIO_MODE_OUTPUT);
    gpio_set_level(gpio, 0);
}

static inline int ow_read(gpio_num_t gpio)
{
    gpio_set_direction(gpio, GPIO_MODE_INPUT);
    return gpio_get_level(gpio);
}

static bool ow_reset(gpio_num_t gpio)
{
    // Pull bus low for 480μs
    ow_set_low(gpio);
    ets_delay_us(480);
    
    // Release bus and wait for presence pulse
    ow_set_high(gpio);
    ets_delay_us(70);
    
    // Read presence pulse (sensor should pull low)
    int presence = !ow_read(gpio);
    
    // Wait for presence pulse to complete
    ets_delay_us(410);
    
    return presence;
}

static void ow_write_bit(gpio_num_t gpio, int bit)
{
    if (bit) {
        // Write '1': Pull low for 6μs, then release
        ow_set_low(gpio);
        ets_delay_us(6);
        ow_set_high(gpio);
        ets_delay_us(64);
    } else {
        // Write '0': Pull low for 60μs, then release
        ow_set_low(gpio);
        ets_delay_us(60);
        ow_set_high(gpio);
        ets_delay_us(10);
    }
}

static int ow_read_bit(gpio_num_t gpio)
{
    // Pull low for 3μs to initiate read
    ow_set_low(gpio);
    ets_delay_us(3);
    
    // Release and wait 10μs
    ow_set_high(gpio);
    ets_delay_us(10);
    
    // Read bit
    int bit = ow_read(gpio);
    
    // Wait for rest of time slot
    ets_delay_us(53);
    
    return bit;
}

static void ow_write_byte(gpio_num_t gpio, uint8_t byte)
{
    for (int i = 0; i < 8; i++) {
        ow_write_bit(gpio, (byte >> i) & 0x01);
    }
}

static uint8_t ow_read_byte(gpio_num_t gpio)
{
    uint8_t byte = 0;
    for (int i = 0; i < 8; i++) {
        byte |= (ow_read_bit(gpio) << i);
    }
    return byte;
}

bool SensorDS18B20::init()
{
    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Initializing DS18B20 sensor on GPIO" + 
                        std::to_string(_gpio));
    
    // Configure GPIO
    gpio_reset_pin(_gpio);
    gpio_set_pull_mode(_gpio, GPIO_PULLUP_ONLY);  // Enable pull-up
    ow_set_high(_gpio);
    
    // Test communication
    if (!ow_reset(_gpio)) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "No DS18B20 device found on GPIO" + 
                            std::to_string(_gpio));
        return false;
    }
    
    _initialized = true;
    
    // Try to read one temperature to verify sensor works
    float temp;
    if (readOneSensor(temp)) {
        _temperatures.clear();
        _temperatures.push_back(temp);
        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "DS18B20 sensor initialized successfully. Temp: " + 
                            std::to_string(temp) + "°C");
    } else {
        LogFile.WriteToFile(ESP_LOG_WARN, TAG, "DS18B20 initialized but initial read failed");
    }
    
    return true;
}

bool SensorDS18B20::readOneSensor(float& temp)
{
    // Reset and check presence
    if (!ow_reset(_gpio)) {
        return false;
    }
    
    // Skip ROM (single device mode)
    ow_write_byte(_gpio, DS18B20_CMD_SKIP_ROM);
    
    // Start temperature conversion
    ow_write_byte(_gpio, DS18B20_CMD_CONVERT_T);
    
    // Wait for conversion (750ms for 12-bit resolution)
    vTaskDelay(pdMS_TO_TICKS(750));
    
    // Reset and check presence
    if (!ow_reset(_gpio)) {
        return false;
    }
    
    // Skip ROM again
    ow_write_byte(_gpio, DS18B20_CMD_SKIP_ROM);
    
    // Read scratchpad
    ow_write_byte(_gpio, DS18B20_CMD_READ_SCRATCHPAD);
    
    // Read 9 bytes
    uint8_t data[9];
    for (int i = 0; i < 9; i++) {
        data[i] = ow_read_byte(_gpio);
    }
    
    // Calculate CRC
    uint8_t crc = 0;
    for (int i = 0; i < 8; i++) {
        uint8_t inByte = data[i];
        for (int j = 0; j < 8; j++) {
            uint8_t mix = (crc ^ inByte) & 0x01;
            crc >>= 1;
            if (mix) {
                crc ^= 0x8C;
            }
            inByte >>= 1;
        }
    }
    
    if (crc != data[8]) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "CRC mismatch in DS18B20 data");
        return false;
    }
    
    // Convert temperature
    int16_t rawTemp = (data[1] << 8) | data[0];
    temp = (float)rawTemp / 16.0f;
    
    return true;
}

bool SensorDS18B20::readData()
{
    if (!_initialized) {
        return false;
    }
    
    if (!shouldRead()) {
        return false;
    }
    
    float temp;
    if (!readOneSensor(temp)) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to read DS18B20 sensor");
        return false;
    }
    
    _temperatures.clear();
    _temperatures.push_back(temp);
    _lastRead = time(nullptr);
    
    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Read: Temp=" + std::to_string(temp) + "°C");
    
    return true;
}

int SensorDS18B20::getSensorCount() const
{
    return _temperatures.size();
}

float SensorDS18B20::getTemperature(int index) const
{
    if (index >= 0 && index < (int)_temperatures.size()) {
        return _temperatures[index];
    }
    return 0.0f;
}

int SensorDS18B20::scanDevices()
{
    // For simplicity, we'll use single-device mode
    // Multiple device support would require implementing ROM search algorithm
    return _temperatures.size();
}

void SensorDS18B20::publishMQTT()
{
#ifdef ENABLE_MQTT
    if (!_mqttEnabled || !getMQTTisConnected()) {
        return;
    }
    
    for (size_t i = 0; i < _temperatures.size(); i++) {
        std::string topic = _mqttTopic;
        if (_temperatures.size() > 1) {
            topic += "/" + std::to_string(i);
        }
        
        std::string value = std::to_string(_temperatures[i]);
        MQTTPublish(topic, value, 1, true);
    }
    
    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Published to MQTT");
#endif
}

void SensorDS18B20::publishInfluxDB()
{
#ifdef ENABLE_INFLUXDB
    if (!_influxEnabled) {
        return;
    }
    
    time_t now = time(nullptr);
    
    for (size_t i = 0; i < _temperatures.size(); i++) {
        std::string field = "temperature";
        if (_temperatures.size() > 1) {
            field += "_" + std::to_string(i);
        }
        
        influxDB.InfluxDBPublish(_influxMeasurement, 
                                 field, 
                                 std::to_string(_temperatures[i]), 
                                 now);
    }
    
    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Published to InfluxDB");
#endif
}
