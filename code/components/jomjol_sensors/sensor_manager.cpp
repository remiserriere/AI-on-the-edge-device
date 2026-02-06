#include "sensor_manager.h"
#include "sensor_sht3x.h"
#include "sensor_ds18b20.h"

#include "ClassLogFile.h"
#include "Helper.h"

#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cerrno>
#include <climits>
#include <cstdint>

#include "driver/i2c.h"

static const char *TAG = "SENSOR_MANAGER";

// Helper function to safely parse integer without exceptions
static bool safeParseInt(const std::string& str, int& result) {
    if (str.empty()) {
        return false;
    }
    
    char* endptr = nullptr;
    errno = 0;
    long val = strtol(str.c_str(), &endptr, 10);
    
    // Check for errors
    if (errno == ERANGE || val < INT_MIN || val > INT_MAX) {
        return false;
    }
    
    // Check if no conversion was performed
    if (endptr == str.c_str() || *endptr != '\0') {
        return false;
    }
    
    result = static_cast<int>(val);
    return true;
}

// Helper function to safely parse unsigned long without exceptions
static bool safeParseULong(const std::string& str, unsigned long& result, int base = 10) {
    if (str.empty()) {
        return false;
    }
    
    char* endptr = nullptr;
    errno = 0;
    unsigned long val = strtoul(str.c_str(), &endptr, base);
    
    // Check for errors
    if (errno == ERANGE) {
        return false;
    }
    
    // Check if no conversion was performed
    if (endptr == str.c_str() || *endptr != '\0') {
        return false;
    }
    
    result = val;
    return true;
}

bool SensorBase::shouldRead(int flowInterval)
{
    time_t now = time(nullptr);
    int interval = _readInterval;
    
    // If readInterval is -1, use flow interval (follow flow mode)
    if (_readInterval < 0) {
        interval = flowInterval;
    }
    
    // If interval is still 0 or negative, don't read
    if (interval <= 0) {
        return false;
    }
    
    return (now - _lastRead) >= interval;
}

void SensorBase::sensorTaskWrapper(void* pvParameters)
{
    SensorBase* sensor = static_cast<SensorBase*>(pvParameters);
    sensor->sensorTask();
}

void SensorBase::sensorTask()
{
    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Periodic task started for sensor: " + getName() + 
                        " (interval: " + std::to_string(_readInterval) + "s)");
    
    const TickType_t xDelay = (_readInterval * 1000) / portTICK_PERIOD_MS;
    
    while (true) {
        // Read sensor data and publish
        if (readData()) {
            publishMQTT();
            publishInfluxDB();
        }
        
        vTaskDelay(xDelay);
    }
}

bool SensorBase::startPeriodicTask()
{
    // Only create task if we have a custom interval (not follow flow mode)
    if (_readInterval <= 0) {
        return true;  // Not an error, just not applicable
    }
    
    if (_taskHandle != nullptr) {
        LogFile.WriteToFile(ESP_LOG_WARN, TAG, "Task already running for sensor: " + getName());
        return true;
    }
    
    std::string taskName = "sensor_" + getName();
    // Truncate task name if too long (FreeRTOS limit is 16 chars)
    if (taskName.length() > 15) {
        taskName = taskName.substr(0, 15);
    }
    
    BaseType_t xReturned = xTaskCreatePinnedToCore(
        &SensorBase::sensorTaskWrapper,
        taskName.c_str(),
        4096,  // Stack size
        this,  // Parameter passed to task
        tskIDLE_PRIORITY + 1,  // Priority
        &_taskHandle,
        0  // Core 0
    );
    
    if (xReturned != pdPASS) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to create periodic task for sensor: " + getName());
        _taskHandle = nullptr;
        return false;
    }
    
    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Created periodic task for sensor: " + getName() + 
                        " (interval: " + std::to_string(_readInterval) + "s)");
    return true;
}

void SensorBase::stopPeriodicTask()
{
    if (_taskHandle != nullptr) {
        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Stopping periodic task for sensor: " + getName());
        vTaskDelete(_taskHandle);
        _taskHandle = nullptr;
    }
}

SensorManager::SensorManager() : _enabled(false), _i2cInitialized(false)
{
}

SensorManager::~SensorManager()
{
    deinit();
}

bool SensorManager::init()
{
    if (!_enabled) {
        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Sensor manager disabled");
        return true;
    }
    
    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Initializing sensor manager...");
    
    bool allSuccess = true;
    for (auto& sensor : _sensors) {
        if (!sensor->init()) {
            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to initialize sensor: " + sensor->getName());
            allSuccess = false;
        } else {
            LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Initialized sensor: " + sensor->getName());
            
            // Start periodic task for sensors with custom intervals (> 0)
            if (sensor->getReadInterval() > 0) {
                if (!sensor->startPeriodicTask()) {
                    LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to start periodic task for sensor: " + sensor->getName());
                    allSuccess = false;
                }
            } else {
                LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Sensor " + sensor->getName() + " will follow flow interval (no periodic task)");
            }
        }
    }
    
    return allSuccess;
}

void SensorManager::update(int flowInterval)
{
    if (!_enabled) {
        return;
    }
    
    for (auto& sensor : _sensors) {
        // For sensors with custom intervals, their periodic tasks handle reading
        // Only process "follow flow" sensors here (interval = -1)
        if (sensor->getReadInterval() > 0) {
            continue;
        }
        
        if (sensor->shouldRead(flowInterval)) {
            // Call readData() - uses efficient polling with vTaskDelay()
            // This yields to other tasks between checks, maintaining power efficiency
            // The sensor will poll hardware status and return when complete
            if (sensor->readData()) {
                sensor->publishMQTT();
                sensor->publishInfluxDB();
            }
        }
    }
}

void SensorManager::deinit()
{
    // Stop all periodic tasks before clearing sensors
    for (auto& sensor : _sensors) {
        sensor->stopPeriodicTask();
    }
    
    _sensors.clear();
    
    if (_i2cInitialized) {
        i2c_driver_delete(I2C_NUM_0);
        _i2cInitialized = false;
    }
}

bool SensorManager::initI2C(int sda, int scl, uint32_t freq)
{
    if (_i2cInitialized) {
        LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "I2C already initialized");
        return true;
    }
    
    i2c_config_t conf = {};
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = (gpio_num_t)sda;
    conf.scl_io_num = (gpio_num_t)scl;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = freq;
    
    esp_err_t err = i2c_param_config(I2C_NUM_0, &conf);
    if (err != ESP_OK) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "I2C param config failed: " + std::to_string(err));
        return false;
    }
    
    err = i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0);
    if (err != ESP_OK) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "I2C driver install failed: " + std::to_string(err));
        return false;
    }
    
    _i2cInitialized = true;
    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "I2C initialized (SDA:" + std::to_string(sda) + 
                        ", SCL:" + std::to_string(scl) + ", Freq:" + std::to_string(freq) + ")");
    return true;
}

void SensorManager::scanGPIOConfig(const std::string& configFile, int& sdaPin, int& sclPin, int& onewirePin)
{
    sdaPin = -1;
    sclPin = -1;
    onewirePin = -1;
    
    std::ifstream ifs(configFile);
    if (!ifs.is_open()) {
        return;
    }
    
    std::string line;
    bool inGPIOSection = false;
    
    while (std::getline(ifs, line)) {
        line = trim(line);
        
        // Check for GPIO section
        if (line.find("[GPIO]") == 0) {
            inGPIOSection = true;
            continue;
        } else if (line[0] == '[') {
            inGPIOSection = false;
            continue;
        }
        
        if (!inGPIOSection || line.empty() || line[0] == ';' || line[0] == '#') {
            continue;
        }
        
        // Parse GPIO configuration
        std::vector<std::string> parts = ZerlegeZeile(line);
        if (parts.size() < 2) {
            continue;
        }
        
        std::string param = toUpper(parts[0]);
        std::string value = toLower(parts[1]);
        
        // Extract GPIO number from param (e.g., "IO12" -> 12)
        int gpioNum = -1;
        if (param.find("IO") == 0) {
            if (!safeParseInt(param.substr(2), gpioNum)) {
                continue;
            }
        }
        
        // Check GPIO mode
        if (value == "i2c-sda") {
            sdaPin = gpioNum;
            LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Found I2C SDA on GPIO" + std::to_string(gpioNum));
        } else if (value == "i2c-scl") {
            sclPin = gpioNum;
            LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Found I2C SCL on GPIO" + std::to_string(gpioNum));
        } else if (value == "onewire") {
            onewirePin = gpioNum;
            LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Found 1-Wire on GPIO" + std::to_string(gpioNum));
        }
    }
    
    ifs.close();
}

bool SensorManager::readConfig(const std::string& configFile)
{
    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Reading sensor configuration from " + configFile);
    
    // First, scan GPIO configuration to find sensor pins
    int sdaPin, sclPin, onewirePin;
    scanGPIOConfig(configFile, sdaPin, sclPin, onewirePin);
    
    std::ifstream ifs(configFile);
    if (!ifs.is_open()) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Cannot open config file: " + configFile);
        return false;
    }
    
    std::string line;
    bool inSHT3xSection = false;
    bool inDS18B20Section = false;
    
    // SHT3x configuration variables
    bool sht3xEnable = false;
    uint8_t sht3xAddress = 0x44;
    int sht3xInterval = -1;  // -1 = follow flow (default)
    bool sht3xMqttEnable = true;
    std::string sht3xMqttTopic = "sensors/sht3x";
    bool sht3xInfluxEnable = false;
    std::string sht3xInfluxMeasurement = "environment";
    uint32_t i2cFreq = 100000;
    
    // DS18B20 configuration variables
    bool ds18b20Enable = false;
    int ds18b20Interval = -1;  // -1 = follow flow (default)
    bool ds18b20MqttEnable = true;
    std::string ds18b20MqttTopic = "sensors/temperature";
    bool ds18b20InfluxEnable = false;
    std::string ds18b20InfluxMeasurement = "environment";
    
    while (std::getline(ifs, line)) {
        line = trim(line);
        
        // Check for section headers
        if (line.find("[SHT3x]") == 0 || line.find("[SHT3X]") == 0) {
            inSHT3xSection = true;
            inDS18B20Section = false;
            _enabled = true;
            continue;
        } else if (line.find("[DS18B20]") == 0) {
            inSHT3xSection = false;
            inDS18B20Section = true;
            _enabled = true;
            continue;
        } else if (line[0] == '[') {
            inSHT3xSection = false;
            inDS18B20Section = false;
            continue;
        }
        
        if (line.empty() || line[0] == ';' || line[0] == '#') {
            continue;
        }
        
        // Parse parameters
        std::vector<std::string> parts = ZerlegeZeile(line);
        if (parts.size() < 2) {
            continue;
        }
        
        std::string param = toUpper(parts[0]);
        std::string value = parts[1];
        
        // SHT3x parameters
        if (inSHT3xSection) {
            if (param == "ENABLE") {
                sht3xEnable = (toUpper(value) == "TRUE" || value == "1");
            } else if (param == "ADDRESS") {
                unsigned long tempAddress;
                // Use base 0 to auto-detect format (supports both 0x44 hex and 68 decimal)
                if (safeParseULong(value, tempAddress, 0) && tempAddress <= 0xFF) {
                    sht3xAddress = static_cast<uint8_t>(tempAddress);
                } else {
                    LogFile.WriteToFile(ESP_LOG_WARN, TAG, "Invalid SHT3x address value: " + value);
                }
            } else if (param == "INTERVAL") {
                if (!safeParseInt(value, sht3xInterval)) {
                    LogFile.WriteToFile(ESP_LOG_WARN, TAG, "Invalid SHT3x interval value: " + value);
                }
            } else if (param == "I2C_FREQUENCY") {
                unsigned long tempFreq;
                // Range check is meaningful on 64-bit platforms where unsigned long > 32-bit
                if (safeParseULong(value, tempFreq, 10) && tempFreq <= UINT32_MAX) {
                    i2cFreq = static_cast<uint32_t>(tempFreq);
                } else {
                    LogFile.WriteToFile(ESP_LOG_WARN, TAG, "Invalid I2C frequency value: " + value);
                }
            } else if (param == "MQTT_ENABLE") {
                sht3xMqttEnable = (toUpper(value) == "TRUE" || value == "1");
            } else if (param == "MQTT_TOPIC") {
                sht3xMqttTopic = value;
            } else if (param == "INFLUXDB_ENABLE") {
                sht3xInfluxEnable = (toUpper(value) == "TRUE" || value == "1");
            } else if (param == "INFLUXDB_MEASUREMENT") {
                sht3xInfluxMeasurement = value;
            }
        }
        // DS18B20 parameters
        else if (inDS18B20Section) {
            if (param == "ENABLE") {
                ds18b20Enable = (toUpper(value) == "TRUE" || value == "1");
            } else if (param == "INTERVAL") {
                if (!safeParseInt(value, ds18b20Interval)) {
                    LogFile.WriteToFile(ESP_LOG_WARN, TAG, "Invalid DS18B20 interval value: " + value);
                }
            } else if (param == "MQTT_ENABLE") {
                ds18b20MqttEnable = (toUpper(value) == "TRUE" || value == "1");
            } else if (param == "MQTT_TOPIC") {
                ds18b20MqttTopic = value;
            } else if (param == "INFLUXDB_ENABLE") {
                ds18b20InfluxEnable = (toUpper(value) == "TRUE" || value == "1");
            } else if (param == "INFLUXDB_MEASUREMENT") {
                ds18b20InfluxMeasurement = value;
            }
        }
    }
    
    ifs.close();
    
    if (!_enabled) {
        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "No sensor sections found in config");
        return true;  // Not an error, just disabled
    }
    
    // Create sensor instances
    if (sht3xEnable && sdaPin >= 0 && sclPin >= 0) {
        if (!initI2C(sdaPin, sclPin, i2cFreq)) {
            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to initialize I2C bus");
            return false;
        }
        
        auto sensor = std::make_unique<SensorSHT3x>(
            sht3xAddress,
            sht3xMqttTopic,
            sht3xInfluxMeasurement,
            sht3xInterval,
            sht3xMqttEnable,
            sht3xInfluxEnable
        );
        _sensors.push_back(std::move(sensor));
        
        std::stringstream ss;
        ss << "0x" << std::hex << (int)sht3xAddress;
        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Created SHT3x sensor (addr:" + ss.str() + 
                            ", interval:" + (sht3xInterval < 0 ? "follow flow" : std::to_string(sht3xInterval) + "s") + ")");
    } else if (sht3xEnable) {
        LogFile.WriteToFile(ESP_LOG_WARN, TAG, "SHT3x enabled but I2C pins not configured in GPIO section");
    }
    
    // Create DS18B20 sensor if enabled and pin is configured
    if (ds18b20Enable && onewirePin >= 0) {
        auto sensor = std::make_unique<SensorDS18B20>(
            (gpio_num_t)onewirePin,
            ds18b20MqttTopic,
            ds18b20InfluxMeasurement,
            ds18b20Interval,
            ds18b20MqttEnable,
            ds18b20InfluxEnable
        );
        _sensors.push_back(std::move(sensor));
        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Created DS18B20 sensor (GPIO:" + std::to_string(onewirePin) + 
                            ", interval:" + (ds18b20Interval < 0 ? "follow flow" : std::to_string(ds18b20Interval) + "s") + ")");
    } else if (ds18b20Enable) {
        LogFile.WriteToFile(ESP_LOG_WARN, TAG, "DS18B20 enabled but 1-Wire pin not configured in GPIO section");
    }
    
    return true;
}

std::string SensorManager::getJSON()
{
    if (!_enabled || _sensors.empty()) {
        return "{}";
    }
    
    std::stringstream json;
    json << "{";
    json << "\"sensors\":[";
    
    bool first = true;
    for (const auto& sensor : _sensors) {
        if (!first) {
            json << ",";
        }
        first = false;
        
        json << "{";
        json << "\"name\":\"" << sensor->getName() << "\"";
        
        // Add sensor-specific data
        if (sensor->getName() == "SHT3x") {
            auto* sht3x = static_cast<SensorSHT3x*>(sensor.get());
            if (sht3x) {
                json << ",\"temperature\":" << sht3x->getTemperature();
                json << ",\"humidity\":" << sht3x->getHumidity();
                json << ",\"unit_temp\":\"°C\"";
                json << ",\"unit_humidity\":\"%\"";
            }
        } else if (sensor->getName() == "DS18B20") {
            auto* ds18b20 = static_cast<SensorDS18B20*>(sensor.get());
            if (ds18b20) {
                int count = ds18b20->getSensorCount();
                json << ",\"count\":" << count;
                json << ",\"temperatures\":[";
                for (int i = 0; i < count; i++) {
                    if (i > 0) json << ",";
                    json << ds18b20->getTemperature(i);
                }
                json << "]";
                json << ",\"rom_ids\":[";
                for (int i = 0; i < count; i++) {
                    if (i > 0) json << ",";
                    json << "\"" << ds18b20->getRomId(i) << "\"";
                }
                json << "]";
                json << ",\"unit\":\"°C\"";
            }
        }
        
        // Add last read timestamp
        json << ",\"last_read\":" << sensor->getLastReadTime();
        
        json << "}";
    }
    
    json << "]";
    json << "}";
    
    return json.str();
}
