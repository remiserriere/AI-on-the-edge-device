# SHT3x Enable

Enable or disable the SHT3x temperature and humidity sensor.

## Value
- `true` - Enable sensor
- `false` - Disable sensor (default)

## Description

The SHT3x is a high-precision temperature and humidity sensor connected via I²C. When enabled, it provides:
- **Temperature**: -40°C to +125°C (±0.2°C accuracy)
- **Relative Humidity**: 0-100% RH (±2% accuracy)

## ⚠️ Prerequisites - GPIO Configuration Required!

**Before enabling the SHT3x sensor, you MUST configure TWO GPIO pins for I²C communication (SDA and SCL).**

### Step 1: Access Advanced GPIO Settings

The GPIO configuration section is **hidden by default** in the web interface. To access it:

1. Navigate to the **Configuration** page in the web UI
2. Scroll to the **GPIO** section
3. Click **"Show Expert Options"** or **"Advanced Settings"** button (if available)
4. The GPIO pin configuration options will appear

> **💡 Tip**: GPIO settings are in an "advanced" or "expert" section to prevent accidental misconfiguration that could affect device operation.

### Step 2: Configure GPIO Pins for I²C

Once the GPIO section is visible, configure **two pins** for I²C:

#### I²C SDA (Data Line)
1. Select a GPIO pin (recommended: **IO12**)
2. Set the mode to **`i2c-sda`** from the dropdown
3. This pin carries bidirectional data

#### I²C SCL (Clock Line)
1. Select a different GPIO pin (recommended: **IO13**)
2. Set the mode to **`i2c-scl`** from the dropdown
3. This pin provides the clock signal

**Recommended Configuration:**
- **SDA**: IO12 ✅ (Fully available, good for data)
- **SCL**: IO13 ✅ (Has built-in pull-up, ideal for clock)

See the detailed guides:
- [GPIO I²C SDA Configuration](../GPIO/I2C-SDA.md)
- [GPIO I²C SCL Configuration](../GPIO/I2C-SCL.md)

### Step 3: Physical Wiring

After configuring both GPIO pins:

1. **Connect SHT3x sensor** with proper wiring:
   - VDD → 3.3V (**NOT 5V!**)
   - GND → GND
   - SDA → Selected SDA GPIO (e.g., IO12)
   - SCL → Selected SCL GPIO (e.g., IO13)
2. **Add 4.7kΩ pull-up resistors**:
   - One between SDA and 3.3V
   - One between SCL and 3.3V
3. **Verify power**: Ensure sensor receives 3.3V (5V will damage it!)

### Step 4: Enable the Sensor

After GPIO configuration and wiring are complete:

1. Return to the **SHT3x** configuration section
2. Set **Enable = true**
3. Configure other settings (Address, Interval, MQTT, InfluxDB)
4. Save and restart if required

## Device Health Monitoring Use Case

### Why Monitor Enclosure Climate?

**Outdoor installations** are exposed to harsh conditions. The SHT3x acts as an early warning system:

**Temperature Monitoring:**
- **> 60°C**: Electronics stress, reduced lifespan
- **> 70°C**: Risk of thermal shutdown
- **< 0°C**: LCD issues, potential condensation on warming
- **Rapid changes**: Thermal stress on components

**Humidity Monitoring:**
- **> 80% RH**: High risk of condensation → short circuits
- **> 60% RH sustained**: Gradual corrosion of contacts
- **< 20% RH**: Potential static electricity issues
- **Sudden spikes**: Water ingress, seal failure

### Example Scenarios

**Scenario 1: Outdoor Water Meter**
```
Environment: Exposed enclosure, temperature -20°C to +40°C
Problem: Device fails during hot summer days
Solution: SHT3x detects enclosure reaching 65°C
Action: Add ventilation holes, relocate to shaded area
Result: Device stable, no more thermal failures
```

**Scenario 2: Basement Installation**
```
Environment: High humidity basement, minimal ventilation
Problem: Corrosion on circuit boards after 6 months
Solution: SHT3x detects sustained 85% humidity
Action: Add desiccant pack, improve enclosure seal
Result: Humidity drops to 50%, device lifespan extended
```

**Scenario 3: Roof-Mounted**
```
Environment: Direct sun exposure
Problem: Camera stops working during afternoon
Solution: SHT3x shows 75°C internal temperature
Action: Added heat sink, improved airflow
Result: Temperature stays below 55°C
```

## Configuration Example

```ini
[GPIO]
IO12 = i2c-sda
IO13 = i2c-scl

[SHT3x]
Enable = true
Address = 0x44
Interval = -1            ; Follow flow cycle
I2C_Frequency = 100000
MQTT_Enable = true
MQTT_Topic = enclosure/climate
InfluxDB_Enable = true
InfluxDB_Measurement = environment
```

## MQTT Output

When enabled and MQTT is configured, publishes:
```
enclosure/climate/temperature → 24.5
enclosure/climate/humidity → 45.2
```

Use in Home Assistant for:
- Automation triggers (e.g., turn on heater if temp < 5°C)
- Alert notifications (humidity > 80%)
- Long-term graphs and trending

## InfluxDB Output

When enabled and InfluxDB is configured, stores:
```
environment,sensor=sht3x temperature=24.5
environment,sensor=sht3x humidity=45.2
```

Use in Grafana for:
- Historical trending
- Correlation with device reboots/errors
- Predictive maintenance

## Recommended Alert Thresholds

Configure alerts in Home Assistant or Grafana:

| Condition | Severity | Action |
|-----------|----------|--------|
| Temp > 65°C | ⚠️ Warning | Check ventilation |
| Temp > 70°C | 🔴 Critical | Device may shut down |
| Humidity > 80% | ⚠️ Warning | Check seals |
| Humidity > 90% | 🔴 Critical | Water ingress risk |
| Temp < -10°C | ⚠️ Warning | Cold weather mode |

## Related Parameters

- [GPIO I²C SDA](../GPIO/I2C-SDA.md) - Data line configuration
- [GPIO I²C SCL](../GPIO/I2C-SCL.md) - Clock line configuration
- [SHT3x Address](Address.md) - I²C address selection
- [SHT3x Interval](Interval.md) - Reading frequency
- [SHT3x MQTT Enable](MQTT_Enable.md) - MQTT publishing
- [SHT3x InfluxDB Enable](InfluxDB_Enable.md) - InfluxDB logging

## Troubleshooting

**Sensor not detected:**
1. Check GPIO pins are configured as i2c-sda and i2c-scl
2. Verify wiring: SDA→IO12, SCL→IO13, VDD→3.3V, GND→GND
3. Confirm pull-up resistors (4.7kΩ) are installed
4. Check I²C address (try 0x44 and 0x45)
5. Look in device logs for I²C errors

**Readings seem wrong:**
- Temperature off by constant: Sensor self-heating (improve airflow)
- Humidity stuck at 99%: Sensor saturated (needs drying)
- Erratic values: EMI interference or loose connection

**No MQTT messages:**
1. Verify MQTT is enabled globally ([MQTT] section)
2. Check MQTT_Enable = true for SHT3x
3. Confirm MQTT broker is accessible
4. Check logs for publish errors
