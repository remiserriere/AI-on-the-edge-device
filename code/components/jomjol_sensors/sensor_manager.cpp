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
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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
        // Read sensor data
        // Note: readData() spawns an async task that handles publishing
        // Don't publish here to avoid double-publishing
        readData();
        
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
        tskIDLE_PRIORITY,  // Priority - low for periodic reading (not critical)
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
    
    bool anySuccess = false;
    bool anyFailure = false;
    
    for (auto& sensor : _sensors) {
        // Sensors are already initialized in readConfig, just start periodic tasks
        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Starting periodic task for sensor: " + sensor->getName());
        
        // Start periodic task for sensors with custom intervals (> 0)
        if (sensor->getReadInterval() > 0) {
            if (!sensor->startPeriodicTask()) {
                LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to start periodic task for sensor: " + sensor->getName());
                anyFailure = true;
            } else {
                anySuccess = true;
            }
        } else {
            LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Sensor " + sensor->getName() + " will follow flow interval (no periodic task)");
            anySuccess = true;
        }
    }
    
    // Log results
    if (_sensors.empty()) {
        if (_sensorErrors.empty()) {
            LogFile.WriteToFile(ESP_LOG_INFO, TAG, "No sensors configured");
        } else {
            LogFile.WriteToFile(ESP_LOG_WARN, TAG, "All configured sensors failed to initialize");
        }
    } else if (anyFailure) {
        LogFile.WriteToFile(ESP_LOG_WARN, TAG, "Some sensors failed to start periodic tasks");
    } else {
        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "All sensors started successfully");
    }
    
    // Always return true to allow device to boot even with sensor errors
    return true;
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
        
        // Check if we should start a new read
        if (sensor->shouldRead(flowInterval)) {
            // Start async read (spawns ephemeral background task)
            // readData() returns immediately - conversion happens in background
            // The background task will:
            // 1. Poll hardware with vTaskDelay() yields (power efficient)
            // 2. Update sensor data when complete
            // 3. Publish to MQTT/InfluxDB
            // 4. Self-terminate via vTaskDelete(NULL)
            sensor->readData();
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
    
    // First, try to delete any existing driver (in case of previous failed init)
    // This is safe - if no driver exists, it returns ESP_ERR_INVALID_STATE which we ignore
    esp_err_t err = i2c_driver_delete(I2C_NUM_0);
    if (err == ESP_OK) {
        LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Deleted existing I2C driver before reinit");
        vTaskDelay(pdMS_TO_TICKS(10));  // Small delay after deletion
    }
    
    i2c_config_t conf = {};
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = (gpio_num_t)sda;
    conf.scl_io_num = (gpio_num_t)scl;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = freq;
    conf.clk_flags = 0; // Use default clock configuration
    
    err = i2c_param_config(I2C_NUM_0, &conf);
    if (err != ESP_OK) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "I2C param config failed: " + std::to_string(err));
        return false;
    }
    
    err = i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0);
    if (err != ESP_OK) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "I2C driver install failed: " + std::to_string(err));
        // If already installed, try to continue anyway
        if (err == ESP_ERR_INVALID_STATE) {
            LogFile.WriteToFile(ESP_LOG_WARN, TAG, "I2C driver already installed, continuing...");
            _i2cInitialized = true;
            return true;
        }
        return false;
    }
    
    // Give the I2C bus time to stabilize after initialization
    vTaskDelay(pdMS_TO_TICKS(50));
    
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
    
    // Clear any previous sensor errors
    _sensorErrors.clear();
    
    // Create and initialize SHT3x sensor with retry logic
    if (sht3xEnable && sdaPin >= 0 && sclPin >= 0) {
        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Attempting to initialize SHT3x sensor...");
        
        // Try to initialize I2C bus
        bool i2cSuccess = false;
        for (int retry = 0; retry < SENSOR_INIT_RETRY_COUNT; retry++) {
            if (retry > 0) {
                int delayMs = 100 * retry;  // 100ms, 200ms
                LogFile.WriteToFile(ESP_LOG_WARN, TAG, "I2C init retry " + std::to_string(retry + 1) + 
                                    " after " + std::to_string(delayMs) + "ms");
                vTaskDelay(pdMS_TO_TICKS(delayMs));
            }
            
            if (initI2C(sdaPin, sclPin, i2cFreq)) {
                i2cSuccess = true;
                break;
            }
        }
        
        if (!i2cSuccess) {
            addSensorError("SHT3x", SensorInitStatus::FAILED_BUS_INIT, 
                          "Failed to initialize I2C bus after " + std::to_string(SENSOR_INIT_RETRY_COUNT) + " retries",
                          SENSOR_INIT_RETRY_COUNT);
            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "SHT3x initialization aborted - I2C bus init failed");
            
            // Add delay after I2C failure to allow GPIO states to settle before DS18B20 init
            // This prevents I2C bus issues from affecting 1-Wire GPIO initialization
            LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Waiting for GPIO states to stabilize after I2C failure...");
            vTaskDelay(pdMS_TO_TICKS(200));
        } else {
            // I2C bus is ready, create and initialize sensor with retry
            auto sensor = std::make_unique<SensorSHT3x>(
                sht3xAddress,
                sht3xMqttTopic,
                sht3xInfluxMeasurement,
                sht3xInterval,
                sht3xMqttEnable,
                sht3xInfluxEnable
            );
            
            std::stringstream ss;
            ss << "0x" << std::hex << (int)sht3xAddress;
            LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Created SHT3x sensor (addr:" + ss.str() + 
                                ", interval:" + (sht3xInterval < 0 ? "follow flow" : std::to_string(sht3xInterval) + "s") + ")");
            
            // Try to initialize the sensor with retries
            bool initSuccess = false;
            for (int retry = 0; retry < SENSOR_INIT_RETRY_COUNT; retry++) {
                if (retry > 0) {
                    int delayMs = 100 * retry;  // 100ms, 200ms
                    LogFile.WriteToFile(ESP_LOG_WARN, TAG, "SHT3x sensor init retry " + std::to_string(retry + 1) + 
                                        " after " + std::to_string(delayMs) + "ms");
                    vTaskDelay(pdMS_TO_TICKS(delayMs));
                }
                
                if (sensor->init()) {
                    initSuccess = true;
                    break;
                }
            }
            
            if (initSuccess) {
                _sensors.push_back(std::move(sensor));
                LogFile.WriteToFile(ESP_LOG_INFO, TAG, "SHT3x sensor initialized successfully");
            } else {
                addSensorError("SHT3x", SensorInitStatus::FAILED_NO_DEVICE,
                              "Sensor not responding at address " + ss.str() + " after " + 
                              std::to_string(SENSOR_INIT_RETRY_COUNT) + " retries",
                              SENSOR_INIT_RETRY_COUNT);
                LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "SHT3x sensor initialization failed");
            }
        }
    } else if (sht3xEnable) {
        addSensorError("SHT3x", SensorInitStatus::FAILED_OTHER,
                      "I2C pins not configured in GPIO section",
                      0);
        LogFile.WriteToFile(ESP_LOG_WARN, TAG, "SHT3x enabled but I2C pins not configured in GPIO section");
    }
    
    // Create and initialize DS18B20 sensor with retry logic
    if (ds18b20Enable && onewirePin >= 0) {
        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Attempting to initialize DS18B20 sensor...");
        
        // Add initial delay to allow system boot to stabilize before 1-Wire init
        // This helps GPIO subsystem settle, especially beneficial after I2C init failures
        vTaskDelay(pdMS_TO_TICKS(100));
        
        auto sensor = std::make_unique<SensorDS18B20>(
            (gpio_num_t)onewirePin,
            ds18b20MqttTopic,
            ds18b20InfluxMeasurement,
            ds18b20Interval,
            ds18b20MqttEnable,
            ds18b20InfluxEnable
        );
        
        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Created DS18B20 sensor (GPIO:" + std::to_string(onewirePin) + 
                            ", interval:" + (ds18b20Interval < 0 ? "follow flow" : std::to_string(ds18b20Interval) + "s") + ")");
        
        // Try to initialize the sensor with retries
        // Use longer delays between retries to allow hardware to stabilize
        bool initSuccess = false;
        for (int retry = 0; retry < SENSOR_INIT_RETRY_COUNT; retry++) {
            if (retry > 0) {
                // Longer delays: 200ms, 400ms, 600ms for hardware stabilization
                int delayMs = 200 * retry;
                LogFile.WriteToFile(ESP_LOG_WARN, TAG, "DS18B20 sensor init retry " + std::to_string(retry + 1) + 
                                    " after " + std::to_string(delayMs) + "ms");
                vTaskDelay(pdMS_TO_TICKS(delayMs));
            }
            
            if (sensor->init()) {
                initSuccess = true;
                break;
            }
        }
        
        if (initSuccess) {
            _sensors.push_back(std::move(sensor));
            LogFile.WriteToFile(ESP_LOG_INFO, TAG, "DS18B20 sensor initialized successfully");
        } else {
            addSensorError("DS18B20", SensorInitStatus::FAILED_NO_DEVICE,
                          "No DS18B20 devices found on GPIO" + std::to_string(onewirePin) + 
                          " after " + std::to_string(SENSOR_INIT_RETRY_COUNT) + " retries",
                          SENSOR_INIT_RETRY_COUNT);
            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "DS18B20 sensor initialization failed");
        }
    } else if (ds18b20Enable) {
        addSensorError("DS18B20", SensorInitStatus::FAILED_OTHER,
                      "1-Wire pin not configured in GPIO section",
                      0);
        LogFile.WriteToFile(ESP_LOG_WARN, TAG, "DS18B20 enabled but 1-Wire pin not configured in GPIO section");
    }
    
    // Log summary
    if (_sensors.empty() && _sensorErrors.empty()) {
        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "No sensors configured");
    } else if (_sensors.empty() && !_sensorErrors.empty()) {
        LogFile.WriteToFile(ESP_LOG_WARN, TAG, "All sensors failed to initialize - device will continue to boot");
    } else if (!_sensorErrors.empty()) {
        LogFile.WriteToFile(ESP_LOG_WARN, TAG, "Some sensors failed to initialize - " + 
                            std::to_string(_sensors.size()) + " sensor(s) working, " +
                            std::to_string(_sensorErrors.size()) + " sensor(s) failed");
    } else {
        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "All " + std::to_string(_sensors.size()) + 
                            " sensor(s) initialized successfully");
    }
    
    return true;  // Always return true to allow device to continue booting
}

std::string SensorManager::getJSON()
{
    if (!_enabled || (_sensors.empty() && _sensorErrors.empty())) {
        return "{}";
    }
    
    std::stringstream json;
    json << "{";
    
    // Add sensors data - each physical sensor is its own object
    json << "\"sensors\":[";
    
    bool first = true;
    for (const auto& sensor : _sensors) {
        // Add sensor-specific data
        if (sensor->getName() == "SHT3x") {
            auto* sht3x = static_cast<SensorSHT3x*>(sensor.get());
            if (sht3x) {
                if (!first) {
                    json << ",";
                }
                first = false;
                
                json << "{";
                json << "\"name\":\"SHT3x\"";
                json << ",\"id\":\"SHT3x-0x44\"";  // Could be made dynamic if we support multiple addresses
                json << ",\"status\":\"ok\"";
                json << ",\"temperature\":" << sht3x->getTemperature();
                json << ",\"humidity\":" << sht3x->getHumidity();
                json << ",\"unit_temp\":\"°C\"";
                json << ",\"unit_humidity\":\"%\"";
                json << ",\"last_read\":" << sensor->getLastReadTime();
                json << "}";
            }
        } else if (sensor->getName() == "DS18B20") {
            auto* ds18b20 = static_cast<SensorDS18B20*>(sensor.get());
            if (ds18b20) {
                int count = ds18b20->getSensorCount();
                // Create a separate object for each DS18B20 sensor on the bus
                for (int i = 0; i < count; i++) {
                    if (!first) {
                        json << ",";
                    }
                    first = false;
                    
                    json << "{";
                    json << "\"name\":\"DS18B20\"";
                    json << ",\"id\":\"" << ds18b20->getRomId(i) << "\"";
                    json << ",\"status\":\"ok\"";
                    json << ",\"temperature\":" << ds18b20->getTemperature(i);
                    json << ",\"unit\":\"°C\"";
                    json << ",\"last_read\":" << sensor->getLastReadTime();
                    json << "}";
                }
            }
        }
    }
    
    json << "]";
    
    // Add sensor errors
    if (!_sensorErrors.empty()) {
        json << ",\"errors\":[";
        first = true;
        for (const auto& error : _sensorErrors) {
            if (!first) {
                json << ",";
            }
            first = false;
            
            json << "{";
            json << "\"name\":\"" << error.sensorName << "\"";
            json << ",\"status\":\"";
            switch (error.status) {
                case SensorInitStatus::FAILED_BUS_INIT:
                    json << "bus_init_failed";
                    break;
                case SensorInitStatus::FAILED_NO_DEVICE:
                    json << "no_device";
                    break;
                case SensorInitStatus::FAILED_OTHER:
                    json << "config_error";
                    break;
                default:
                    json << "unknown";
                    break;
            }
            json << "\"";
            json << ",\"message\":\"" << error.errorMessage << "\"";
            json << ",\"retry_count\":" << error.retryCount;
            json << "}";
        }
        json << "]";
    }
    
    json << "}";
    
    return json.str();
}

void SensorManager::addSensorError(const std::string& sensorName, SensorInitStatus status, 
                                   const std::string& errorMessage, int retryCount)
{
    SensorError error;
    error.sensorName = sensorName;
    error.status = status;
    error.errorMessage = errorMessage;
    error.retryCount = retryCount;
    _sensorErrors.push_back(error);
    
    LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Sensor error recorded: " + sensorName + 
                        " - " + errorMessage + " (retries: " + std::to_string(retryCount) + ")");
}
