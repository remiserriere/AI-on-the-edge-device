#include "ClassFlowSensors.h"
#include "ClassFlowControll.h"
#include "sensor_manager.h"
#include "ClassLogFile.h"
#include "../../include/defines.h"

#include <sstream>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "FLOW_SENSORS";

ClassFlowSensors::ClassFlowSensors() : _initialized(false), _sensorTaskHandle(nullptr)
{
    SetInitialParameter();
}

ClassFlowSensors::ClassFlowSensors(std::vector<ClassFlow*>* lfc) : _initialized(false), _sensorTaskHandle(nullptr)
{
    SetInitialParameter();
    ListFlowControll = lfc;
}

ClassFlowSensors::ClassFlowSensors(std::vector<ClassFlow*>* lfc, ClassFlow *_prev) : _initialized(false), _sensorTaskHandle(nullptr)
{
    SetInitialParameter();
    previousElement = _prev;
    ListFlowControll = lfc;
}

ClassFlowSensors::~ClassFlowSensors()
{
    stopSensorUpdateTask();
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
    _sensorTaskHandle = nullptr;
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
        if (!_sensorManager->init()) {
            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to initialize sensors");
            return false;
        }
        _initialized = true;
        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Sensors initialized successfully");
        
        // Start background task for periodic sensor updates (for custom intervals)
        startSensorUpdateTask();
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
    
    // Update sensors (read and publish if interval elapsed)
    // Pass the actual flow interval for "follow flow" mode sensors
    _sensorManager->update(flowIntervalSeconds);
    
    return true;
}

// Background task for periodic sensor updates
// This allows sensors with custom intervals shorter than flow interval to be read properly
void ClassFlowSensors::sensorUpdateTask(void* pvParameters)
{
    ClassFlowSensors* self = static_cast<ClassFlowSensors*>(pvParameters);
    
    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Sensor update task started");
    
    // Check sensors every 1 second
    const TickType_t xDelay = 1000 / portTICK_PERIOD_MS;
    
    while (true) {
        if (self->_sensorManager && self->_sensorManager->isEnabled()) {
            // Pass 0 as flow interval - sensors will use their own custom intervals
            self->_sensorManager->update(0);
        }
        
        vTaskDelay(xDelay);
    }
}

void ClassFlowSensors::startSensorUpdateTask()
{
    if (_sensorTaskHandle != nullptr) {
        LogFile.WriteToFile(ESP_LOG_WARN, TAG, "Sensor update task already running");
        return;
    }
    
    BaseType_t xReturned = xTaskCreatePinnedToCore(
        &ClassFlowSensors::sensorUpdateTask,
        "sensor_update",
        4096,  // Stack size
        this,  // Parameter passed to task
        tskIDLE_PRIORITY + 1,  // Priority (lower than main flow)
        &_sensorTaskHandle,
        0  // Core 0
    );
    
    if (xReturned != pdPASS) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to create sensor update task");
        _sensorTaskHandle = nullptr;
    } else {
        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Sensor update task created successfully");
    }
}

void ClassFlowSensors::stopSensorUpdateTask()
{
    if (_sensorTaskHandle != nullptr) {
        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Stopping sensor update task");
        vTaskDelete(_sensorTaskHandle);
        _sensorTaskHandle = nullptr;
    }
}
