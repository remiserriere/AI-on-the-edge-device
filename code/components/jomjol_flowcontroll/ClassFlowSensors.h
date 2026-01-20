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
    
protected:
    void SetInitialParameter(void) override;
    
private:
    std::unique_ptr<SensorManager> _sensorManager;
    bool _initialized;
};

#endif // CLASSFLOWSENSORS_H
