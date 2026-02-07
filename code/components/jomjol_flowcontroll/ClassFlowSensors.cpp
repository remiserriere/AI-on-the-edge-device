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
    if (upperGraph.compare("[SHT3X]") != 0 && upperGraph.compare("[DS18B20]") != 0) {
        // Not a sensor section
        return false;
    }
    
    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Found sensor section: " + aktparamgraph);
    
    // Create sensor manager on first sensor section found
    if (!_sensorManager) {
        _sensorManager = std::make_unique<SensorManager>();
        
        // Read configuration from file
        if (!_sensorManager->readConfig(CONFIG_FILE)) {
            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to read sensor configuration");
            return false;
        }
    }
    
    // Skip to next paragraph
    while (this->getNextLine(pfile, &aktparamgraph) && !this->isNewParagraph(aktparamgraph)) {
        // Just consume lines until next paragraph
    }
    
    return true;
}

bool ClassFlowSensors::doFlow(std::string time)
{
    if (disabled) {
        return true;
    }
    
    if (!_sensorManager || !_sensorManager->isEnabled()) {
        return true;
    }
    
    // Initialize on first run
    if (!_initialized) {
        // Always returns true now, even with errors, to allow device to boot
        _sensorManager->init();
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
