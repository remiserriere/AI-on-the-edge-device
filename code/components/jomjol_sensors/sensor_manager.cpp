#include "sensor_manager.h"
#include "sensor_sht3x.h"
#include "sensor_ds18b20.h"

#include "ClassLogFile.h"
#include "Helper.h"

#include <fstream>
#include <sstream>

#include "driver/i2c.h"

static const char *TAG = "SENSOR_MANAGER";

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

void SensorManager::update()
{
    if (!_enabled) {
        return;
    }
    
    for (auto& sensor : _sensors) {
        if (sensor->readData()) {
            sensor->publishMQTT();
            sensor->publishInfluxDB();
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

bool SensorManager::readConfig(const std::string& configFile)
{
    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Reading sensor configuration from " + configFile);
    
    std::ifstream ifs(configFile);
    if (!ifs.is_open()) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Cannot open config file: " + configFile);
        return false;
    }
    
    std::string line;
    bool inSensorsSection = false;
    
    // Configuration variables
    bool i2cEnable = false;
    int i2cSDA = 12;
    int i2cSCL = 13;
    uint32_t i2cFreq = 100000;
    
    bool sht3xEnable = false;
    uint8_t sht3xAddress = 0x44;
    int sht3xInterval = 60;
    bool sht3xMqttEnable = false;
    std::string sht3xMqttTopic = "sensors/sht3x";
    bool sht3xInfluxEnable = false;
    std::string sht3xInfluxMeasurement = "environment";
    
    bool ds18b20Enable = false;
    int ds18b20GPIO = 12;
    int ds18b20Interval = 60;
    bool ds18b20MqttEnable = false;
    std::string ds18b20MqttTopic = "sensors/temperature";
    bool ds18b20InfluxEnable = false;
    std::string ds18b20InfluxMeasurement = "environment";
    
    while (std::getline(ifs, line)) {
        line = trim(line);
        
        // Check for section headers
        if (line.find("[Sensors]") == 0 || line.find("[SENSORS]") == 0) {
            inSensorsSection = true;
            _enabled = true;
            continue;
        } else if (line[0] == '[') {
            inSensorsSection = false;
            continue;
        }
        
        if (!inSensorsSection || line.empty() || line[0] == ';' || line[0] == '#') {
            continue;
        }
        
        // Parse parameters
        std::vector<std::string> parts = ZerlegeZeile(line);
        if (parts.size() < 2) {
            continue;
        }
        
        std::string param = toUpper(parts[0]);
        std::string value = parts[1];
        
        // I2C parameters
        if (param == "I2C_ENABLE") {
            i2cEnable = (toUpper(value) == "TRUE" || value == "1");
        } else if (param == "I2C_SDA") {
            i2cSDA = std::stoi(value);
        } else if (param == "I2C_SCL") {
            i2cSCL = std::stoi(value);
        } else if (param == "I2C_FREQUENCY") {
            i2cFreq = std::stoul(value);
        }
        // SHT3x parameters
        else if (param == "SHT3X_ENABLE") {
            sht3xEnable = (toUpper(value) == "TRUE" || value == "1");
        } else if (param == "SHT3X_ADDRESS") {
            sht3xAddress = std::stoul(value, nullptr, 0);  // Support hex (0x44) and decimal
        } else if (param == "SHT3X_INTERVAL") {
            sht3xInterval = std::stoi(value);
        } else if (param == "SHT3X_MQTT_ENABLE") {
            sht3xMqttEnable = (toUpper(value) == "TRUE" || value == "1");
        } else if (param == "SHT3X_MQTT_TOPIC") {
            sht3xMqttTopic = value;
        } else if (param == "SHT3X_INFLUXDB_ENABLE") {
            sht3xInfluxEnable = (toUpper(value) == "TRUE" || value == "1");
        } else if (param == "SHT3X_INFLUXDB_MEASUREMENT") {
            sht3xInfluxMeasurement = value;
        }
        // DS18B20 parameters
        else if (param == "DS18B20_ENABLE") {
            ds18b20Enable = (toUpper(value) == "TRUE" || value == "1");
        } else if (param == "DS18B20_GPIO") {
            ds18b20GPIO = std::stoi(value);
        } else if (param == "DS18B20_INTERVAL") {
            ds18b20Interval = std::stoi(value);
        } else if (param == "DS18B20_MQTT_ENABLE") {
            ds18b20MqttEnable = (toUpper(value) == "TRUE" || value == "1");
        } else if (param == "DS18B20_MQTT_TOPIC") {
            ds18b20MqttTopic = value;
        } else if (param == "DS18B20_INFLUXDB_ENABLE") {
            ds18b20InfluxEnable = (toUpper(value) == "TRUE" || value == "1");
        } else if (param == "DS18B20_INFLUXDB_MEASUREMENT") {
            ds18b20InfluxMeasurement = value;
        }
    }
    
    ifs.close();
    
    if (!_enabled) {
        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Sensors section not found in config");
        return true;  // Not an error, just disabled
    }
    
    // Initialize I2C if needed
    if (i2cEnable && (sht3xEnable)) {
        if (!initI2C(i2cSDA, i2cSCL, i2cFreq)) {
            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to initialize I2C bus");
            return false;
        }
    }
    
    // Create sensor instances
    if (sht3xEnable) {
        auto sensor = std::make_unique<SensorSHT3x>(
            sht3xAddress,
            sht3xMqttTopic,
            sht3xInfluxMeasurement,
            sht3xInterval,
            sht3xMqttEnable,
            sht3xInfluxEnable
        );
        _sensors.push_back(std::move(sensor));
        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Created SHT3x sensor (addr:0x" + intToHexString(sht3xAddress) + ")");
    }
    
    if (ds18b20Enable) {
        auto sensor = std::make_unique<SensorDS18B20>(
            (gpio_num_t)ds18b20GPIO,
            ds18b20MqttTopic,
            ds18b20InfluxMeasurement,
            ds18b20Interval,
            ds18b20MqttEnable,
            ds18b20InfluxEnable
        );
        _sensors.push_back(std::move(sensor));
        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Created DS18B20 sensor (GPIO:" + std::to_string(ds18b20GPIO) + ")");
    }
    
    return true;
}
