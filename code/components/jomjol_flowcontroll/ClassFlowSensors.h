#pragma once

#ifndef CLASSFLOWSENSORS_H
#define CLASSFLOWSENSORS_H

#include "ClassFlow.h"
#include <memory>

class SensorManager;

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
    
protected:
    void SetInitialParameter(void) override;
    
private:
    std::unique_ptr<SensorManager> _sensorManager;
    bool _initialized;
};

#endif // CLASSFLOWSENSORS_H
