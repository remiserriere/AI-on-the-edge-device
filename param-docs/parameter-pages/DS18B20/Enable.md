# DS18B20 Enable

Enable or disable DS18B20 temperature sensor(s) on 1-Wire bus.

## Value
- `true` - Enable sensor
- `false` - Disable sensor (default)

## Description

The DS18B20 is a digital temperature sensor using 1-Wire protocol. Key features:
- **Temperature Range**: -55°C to +125°C (±0.5°C accuracy)
- **Resolution**: 9 to 12-bit configurable (0.5°C to 0.0625°C)
- **Chainable**: Multiple sensors on same wire, each with unique 64-bit ROM ID
- **Conversion Time**: 750ms for 12-bit resolution

## Prerequisites

Before enabling:
1. **GPIO Configuration**: Set one GPIO to `onewire` mode in the GPIO section
2. **Wiring**: Connect DS18B20 with 4.7kΩ pull-up resistor on DATA line
3. **Power**: Can use parasitic power or external 3.3V

## Multi-Sensor Chaining

**The DS18B20's killer feature**: Chain multiple sensors on ONE wire!

Each sensor has a unique 64-bit ROM code factory-programmed. This allows:
- Identify which physical sensor is which
- Read all sensors on same bus
- Publish each with distinct MQTT topic using ROM ID

### Example 3-Sensor Setup

**Hardware:**
```
                    4.7kΩ
3.3V -------|-------|
            |       |
       1-Wire Bus   |
ESP32 --|---+       |
        |   |       |
   DS18B20_A        |
        |   |       |
   DS18B20_B        |
        |   |       |
   DS18B20_C        |
        |           |
GND  ---|-----------|
```

**Physical Placement:**
- **Sensor A** (ROM: 28-0123456789AB): Inside enclosure, near ESP32
- **Sensor B** (ROM: 28-FEDCBA987654): Outside enclosure wall
- **Sensor C** (ROM: 28-AABBCCDD1122): On water pipe being measured

**MQTT Output:**
```
enclosure/temperature/28-0123456789AB → 28.5   (inside)
enclosure/temperature/28-FEDCBA987654 → 15.2   (outside)
enclosure/temperature/28-AABBCCDD1122 → 12.8   (pipe)
```

## Device Health Monitoring with Multiple Sensors

### Use Case 1: Thermal Gradient Detection

**Problem**: Unknown if heat buildup is from electronics or ambient  
**Solution**: 3-sensor chain

```
Inside temp = 45°C
Enclosure wall = 40°C  
Outside ambient = 35°C

Analysis: 10°C delta → Heat generated internally
Action: Add heat sink to ESP32, improve ventilation
```

### Use Case 2: Freeze Protection

**Problem**: Water meter in unheated location, risk of pipe freeze  
**Solution**: Monitor pipe temperature directly

```
[DS18B20]
Enable = true
Interval = 300      ; Check every 5 minutes

Alert: If pipe temp < 2°C for > 30 min
Action: Activate trace heating, send notification
Result: Prevent costly pipe burst
```

### Use Case 3: Cooling Effectiveness

**Problem**: Added fan but unsure if it's working  
**Solution**: Before/after temperature sensors

```
Sensor A: Air intake = 25°C
Sensor B: Near electronics = 32°C
Sensor C: Air exhaust = 35°C

Analysis: 10°C rise through enclosure → Fan working
If delta increases → Fan failing, dust buildup
```

### Use Case 4: Solar Heating Analysis

**Problem**: Outdoor enclosure, direct sun exposure  
**Solution**: Track diurnal temperature cycle

```
Dawn: Inside 15°C, Outside 10°C (5°C delta)
Noon: Inside 65°C, Outside 35°C (30°C delta)
Dusk: Inside 45°C, Outside 25°C (20°C delta)

Analysis: Excessive solar gain, need shading/ventilation
```

## Configuration Example

```ini
[GPIO]
IO12 = onewire

[DS18B20]
Enable = true
Interval = -1           ; Follow flow cycle for frequent updates
MQTT_Enable = true
MQTT_Topic = enclosure/temperature
InfluxDB_Enable = true
InfluxDB_Measurement = environment
```

## MQTT Output Formats

**Single Sensor:**
```
enclosure/temperature → 24.5
```

**Multiple Sensors (automatically appended with ROM ID):**
```
enclosure/temperature/28-0123456789AB → 28.5
enclosure/temperature/28-FEDCBA987654 → 15.2
enclosure/temperature/28-AABBCCDD1122 → 12.8
```

## InfluxDB Output

```
environment,sensor=ds18b20,rom=28-0123456789AB temperature=28.5
environment,sensor=ds18b20,rom=28-FEDCBA987654 temperature=15.2
environment,sensor=ds18b20,rom=28-AABBCCDD1122 temperature=12.8
```

Perfect for Grafana dashboards showing all sensors on one graph!

## Alert Threshold Examples

**For Outdoor Installation:**

| Sensor Location | Warning | Critical | Action |
|-----------------|---------|----------|--------|
| **Inside Enclosure** | >60°C | >70°C | Cooling issue |
| **Inside Enclosure** | <-5°C | <-10°C | Activate heater |
| **Outside Ambient** | >45°C | >50°C | Extreme heat warning |
| **Pipe/Object** | <2°C | <0°C | Freeze protection |

**Temperature Delta Alerts:**
- Inside-Outside > 25°C → Poor ventilation
- Rapid change > 20°C/hour → Weather event, check seals

## Related Parameters

- [GPIO 1-Wire](../GPIO/OneWire.md) - Pin configuration
- [DS18B20 Interval](Interval.md) - Reading frequency
- [DS18B20 MQTT Enable](MQTT_Enable.md) - MQTT publishing
- [DS18B20 InfluxDB Enable](InfluxDB_Enable.md) - InfluxDB logging

## Troubleshooting

**No sensor found:**
1. Check GPIO configured as `onewire`
2. Verify wiring and 4.7kΩ pull-up resistor
3. Check sensor power (3.3V or parasitic)
4. Try single sensor first before chaining
5. Look for ROM IDs in device logs

**Some sensors missing:**
- Cable connections loose
- One sensor damaged (find and remove)
- Pull-up resistor too weak for cable length
- EMI interference

**Temperature readings incorrect:**
- Verify which ROM ID is which physical sensor
- Sensor may be in different location than expected
- Check for self-heating (sensor too close to hot component)
- Verify good thermal contact if measuring object

**Intermittent readings:**
- Cable too long (shorten or use stronger pull-up)
- Poor solder joints
- Water ingress in sensor
- Temperature outside operating range (-55°C to +125°C)
