#pragma once

#ifndef CLASSFLOWSENSORS_H
#define CLASSFLOWSENSORS_H

#include "ClassFlow.h"
#include <memory>
#include <map>

class SensorManager;
class ClassFlowControll;

// Configuration structure for sensors
struct SensorConfig {
    bool enable = false;
    int interval = -1;  // -1 = follow flow (default)
    bool mqttEnable = true;
    std::string mqttTopic;
    bool influxEnable = false;
    std::string influxMeasurement;
    
    // SHT3x specific
    uint8_t sht3xAddress = 0x44;
    uint32_t i2cFreq = 100000;
};

class ClassFlowSensors : public ClassFlow
{
public:
    ClassFlowSensors();
    ClassFlowSensors(std::vector<ClassFlow*>* lfc);
    ClassFlowSensors(std::vector<ClassFlow*>* lfc, ClassFlow *_prev);
    virtual ~ClassFlowSensors();
    
    bool ReadParameter(FILE* pfile, std::string& aktparamgraph) override;
    bool doFlow(std::string time) override;
    std::string name() override { return "ClassFlowSensors"; }
    
    /**
     * @brief Get sensor manager instance for accessing sensor data
     * @return Pointer to sensor manager (nullptr if not initialized)
     */
    SensorManager* getSensorManager() { return _sensorManager.get(); }
    
    /**
     * @brief Set flow controller reference for accessing flow interval
     * @param controller Pointer to the flow controller
     */
    void setFlowControll(ClassFlowControll* controller) { _flowController = controller; }
    
protected:
    void SetInitialParameter(void) override;
    
private:
    std::unique_ptr<SensorManager> _sensorManager;
    ClassFlowControll* _flowController;
    bool _initialized;
    
    // Store configuration for all sensor types
    std::map<std::string, SensorConfig> _sensorConfigs;
    bool _configParsed;
};

#endif // CLASSFLOWSENSORS_H
