#include "sensor_manager.h"
#include "sensor_sht3x.h"
#include "sensor_ds18b20.h"

#include "ClassLogFile.h"
#include "Helper.h"

#include <fstream>
#include <sstream>

#include "driver/i2c.h"

static const char *TAG = "SENSOR_MANAGER";

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
        if (sensor->shouldRead(flowInterval)) {
            if (sensor->readData()) {
                sensor->publishMQTT();
                sensor->publishInfluxDB();
            }
        }
    }
}

void SensorManager::deinit()
{
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
            try {
                gpioNum = std::stoi(param.substr(2));
            } catch (...) {
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
                sht3xAddress = std::stoul(value, nullptr, 0);
            } else if (param == "INTERVAL") {
                sht3xInterval = std::stoi(value);
            } else if (param == "I2C_FREQUENCY") {
                i2cFreq = std::stoul(value);
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
                ds18b20Interval = std::stoi(value);
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
