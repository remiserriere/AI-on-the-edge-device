#include "ClassFlowSensors.h"
#include "ClassFlowControll.h"
#include "sensor_manager.h"
#include "ClassLogFile.h"
#include "../../include/defines.h"

#include <sstream>

static const char *TAG = "FLOW_SENSORS";

ClassFlowSensors::ClassFlowSensors() : _initialized(false)
{
    SetInitialParameter();
}

ClassFlowSensors::ClassFlowSensors(std::vector<ClassFlow*>* lfc) : _initialized(false)
{
    SetInitialParameter();
    ListFlowControll = lfc;
}

ClassFlowSensors::ClassFlowSensors(std::vector<ClassFlow*>* lfc, ClassFlow *_prev) : _initialized(false)
{
    SetInitialParameter();
    previousElement = _prev;
    ListFlowControll = lfc;
}

ClassFlowSensors::~ClassFlowSensors()
{
    if (_sensorManager) {
        _sensorManager->deinit();
    }
}

void ClassFlowSensors::SetInitialParameter(void)
{
    disabled = false;
    _sensorManager = nullptr;
    _flowController = nullptr;
    _initialized = false;
    _configParsed = false;
}

bool ClassFlowSensors::ReadParameter(FILE* pfile, std::string& aktparamgraph)
{
    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "ReadParameter called");
    
    // Check if this is a sensor section ([SHT3x] or [DS18B20])
    aktparamgraph = trim(aktparamgraph);
    
    if (aktparamgraph.size() == 0) {
        if (!this->GetNextParagraph(pfile, aktparamgraph)) {
            return false;
        }
    }
    
    std::string upperGraph = toUpper(aktparamgraph);
    std::string sensorType;
    
    if (upperGraph.compare("[SHT3X]") == 0) {
        sensorType = "SHT3x";
    } else if (upperGraph.compare("[DS18B20]") == 0) {
        sensorType = "DS18B20";
    } else {
        // Not a sensor section
        return false;
    }
    
    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Found sensor section: " + aktparamgraph);
    
    // Get or create configuration for this sensor type
    SensorConfig& config = _sensorConfigs[sensorType];
    
    // Set default topics if not already set
    if (config.mqttTopic.empty()) {
        config.mqttTopic = (sensorType == "SHT3x") ? "sensors/sht3x" : "sensors/temperature";
    }
    if (config.influxMeasurement.empty()) {
        config.influxMeasurement = "environment";
    }
    
    // Parse parameters from the file
    while (this->getNextLine(pfile, &aktparamgraph) && !this->isNewParagraph(aktparamgraph)) {
        std::vector<std::string> splitted = ZerlegeZeile(aktparamgraph);
        if (splitted.size() < 2) {
            continue;
        }
        
        std::string param = toUpper(splitted[0]);
        std::string value = splitted[1];
        
        if (param == "ENABLE") {
            config.enable = (toUpper(value) == "TRUE" || value == "1");
        } else if (param == "INTERVAL") {
            try {
                config.interval = std::stoi(value);
            } catch (...) {
                LogFile.WriteToFile(ESP_LOG_WARN, TAG, sensorType + ": Invalid interval value: " + value);
            }
        } else if (param == "MQTT_ENABLE") {
            config.mqttEnable = (toUpper(value) == "TRUE" || value == "1");
        } else if (param == "MQTT_TOPIC") {
            config.mqttTopic = value;
        } else if (param == "INFLUXDB_ENABLE") {
            config.influxEnable = (toUpper(value) == "TRUE" || value == "1");
        } else if (param == "INFLUXDB_MEASUREMENT") {
            config.influxMeasurement = value;
        } else if (sensorType == "SHT3x") {
            // SHT3x-specific parameters
            if (param == "ADDRESS") {
                try {
                    unsigned long tempAddress;
                    // Support both hex (0x44) and decimal (68) formats
                    if (value.find("0x") == 0 || value.find("0X") == 0) {
                        tempAddress = std::stoul(value, nullptr, 16);
                    } else {
                        tempAddress = std::stoul(value, nullptr, 0);
                    }
                    if (tempAddress <= 0xFF) {
                        config.sht3xAddress = static_cast<uint8_t>(tempAddress);
                    }
                } catch (...) {
                    LogFile.WriteToFile(ESP_LOG_WARN, TAG, "SHT3x: Invalid address value: " + value);
                }
            } else if (param == "I2C_FREQUENCY") {
                try {
                    config.i2cFreq = std::stoul(value);
                } catch (...) {
                    LogFile.WriteToFile(ESP_LOG_WARN, TAG, "SHT3x: Invalid I2C frequency value: " + value);
                }
            }
        }
    }
    
    _configParsed = true;
    
    return true;
}

bool ClassFlowSensors::doFlow(std::string time)
{
    if (disabled) {
        return true;
    }
    
    // Initialize on first run if we have configuration
    if (!_initialized && _configParsed) {
        // Create sensor manager
        _sensorManager = std::make_unique<SensorManager>();
        
        // Pass the parsed configuration to the sensor manager
        if (!_sensorManager->initFromConfig(CONFIG_FILE, _sensorConfigs)) {
            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to initialize sensors");
            // Still return true to allow device to continue booting
        }
        
        _initialized = true;
        
        // Log initialization summary
        if (_sensorManager->hasSensorErrors()) {
            LogFile.WriteToFile(ESP_LOG_WARN, TAG, "Sensors initialized with errors - check logs for details");
        } else if (_sensorManager->getSensors().empty()) {
            LogFile.WriteToFile(ESP_LOG_INFO, TAG, "No sensors configured");
        } else {
            LogFile.WriteToFile(ESP_LOG_INFO, TAG, "All sensors initialized successfully");
        }
    }
    
    if (!_sensorManager || !_sensorManager->isEnabled()) {
        return true;
    }
    
    // Get the flow interval from the controller for "follow flow" mode
    // The AutoInterval is in minutes, we need to convert to seconds
    int flowIntervalSeconds = 0;
    
    if (_flowController) {
        float intervalMinutes = _flowController->getAutoInterval();
        flowIntervalSeconds = (int)(intervalMinutes * 60);  // Convert minutes to seconds
        LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Using flow interval: " + 
                            std::to_string(intervalMinutes) + " min (" + 
                            std::to_string(flowIntervalSeconds) + " sec)");
    } else {
        LogFile.WriteToFile(ESP_LOG_WARN, TAG, "Flow controller not set, using default interval");
    }
    
    // Update sensors that are in "follow flow" mode (interval = -1)
    // Sensors with custom intervals are handled by their own periodic tasks
    _sensorManager->update(flowIntervalSeconds);
    
    return true;
}
