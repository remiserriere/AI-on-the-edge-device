# External Sensors Overview

## Device Health & Safety Monitoring

External sensors can be connected to monitor the environmental conditions around your AI-on-the-edge-device. This is **especially critical for outdoor installations** where temperature and humidity can indicate imminent device failure.

## Supported Sensors

### SHT3x (Temperature & Humidity Sensor)
- **Interface**: I²C
- **Measures**: Temperature (-40°C to +125°C) and Relative Humidity (0-100%)
- **Use Cases**:
  - Monitor enclosure humidity to detect condensation risks
  - Track temperature to prevent overheating
  - Early warning system for adverse weather conditions
  - Validate proper enclosure ventilation

### DS18B20 (Temperature Sensor)
- **Interface**: 1-Wire (Dallas/Maxim)
- **Measures**: Temperature (-55°C to +125°C)
- **Chainable**: Multiple sensors on one wire, each identified by unique ROM ID
- **Use Cases**:
  - Compare inside vs. outside enclosure temperature
  - Monitor multiple temperature points (camera, power supply, ambient)
  - Detect thermal issues before component failure
  - Track diurnal temperature variations

## Why Monitor Your Device?

### 🔴 Failure Prevention
- **Humidity > 80%**: Risk of condensation on electronics → short circuits
- **Temperature > 70°C**: Component stress → reduced lifespan
- **Temperature < -10°C**: LCD display issues, battery problems
- **Rapid temperature changes**: Thermal stress on solder joints

### 🟡 Early Warning Signs
- Gradual temperature increase → cooling fan failure or dust buildup
- Increasing humidity trends → seal degradation
- Temperature spikes during operation → inadequate power supply

### 🟢 Proactive Maintenance
- Schedule enclosure cleaning based on temperature trends
- Replace desiccant packs when humidity rises
- Verify heater/cooling systems are working
- Optimize placement based on environmental data

## Example Use Cases

### Outdoor Water Meter Enclosure
**Challenge**: Device exposed to rain, snow, extreme temperatures  
**Solution**: SHT3x inside enclosure + DS18B20 outside  
**Benefit**: Get alerts when conditions approach failure thresholds

### Unheated Garage Installation  
**Challenge**: Wide temperature swings, potential freezing  
**Solution**: DS18B20 chain monitoring ambient, enclosure, and camera temps  
**Benefit**: Know when to activate heater, detect component issues

### Roof-Mounted Installation
**Challenge**: Direct sun exposure causing overheating  
**Solution**: SHT3x monitoring enclosure climate  
**Benefit**: Data to optimize ventilation, prevent thermal shutdowns

## Configuration

Sensors are configured in two parts:

1. **GPIO Assignment**: Set GPIO pins to sensor modes in `[GPIO]` section
2. **Sensor Parameters**: Configure sensor-specific settings in `[SHT3x]` or `[DS18B20]` sections

See individual parameter documentation for details.

## Data Publishing

Sensor data is automatically published to:
- **MQTT**: For Home Assistant, Node-RED, or other automation systems
- **InfluxDB**: For long-term trending and analysis
- Both support configurable topics/measurements for easy integration

## Recommended Thresholds

Based on typical ESP32-CAM operating conditions:

| Metric | Good | Warning | Critical |
|--------|------|---------|----------|
| **Enclosure Temp** | 0-50°C | 50-65°C | >65°C |
| **Enclosure Humidity** | 20-60% | 60-80% | >80% |
| **Ambient Temp** | -20 to +40°C | Outside range | <-30°C or >50°C |

Set up alerts in Home Assistant or Grafana based on these thresholds.
