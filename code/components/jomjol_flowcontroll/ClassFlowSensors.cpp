#include "ClassFlowSensors.h"
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
    _initialized = false;
}

bool ClassFlowSensors::ReadParameter(FILE* pfile, std::string& aktparamgraph)
{
    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "ReadParameter called");
    
    // Check if this is the [Sensors] section
    aktparamgraph = trim(aktparamgraph);
    
    if (aktparamgraph.size() == 0) {
        if (!this->GetNextParagraph(pfile, aktparamgraph)) {
            return false;
        }
    }
    
    std::string upperGraph = toUpper(aktparamgraph);
    if (upperGraph.compare("[SENSORS]") != 0) {
        // Not the Sensors section
        return false;
    }
    
    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Found [Sensors] section in config");
    
    // Create sensor manager
    _sensorManager = std::make_unique<SensorManager>();
    
    // Read configuration from file
    if (!_sensorManager->readConfig(CONFIG_FILE)) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to read sensor configuration");
        return false;
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
        if (!_sensorManager->init()) {
            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to initialize sensors");
            return false;
        }
        _initialized = true;
        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Sensors initialized successfully");
    }
    
    // Update sensors (read and publish if interval elapsed)
    _sensorManager->update();
    
    return true;
}
