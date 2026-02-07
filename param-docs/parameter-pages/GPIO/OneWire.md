# GPIO 1-Wire Mode

Configure a GPIO pin for 1-Wire protocol used by DS18B20 temperature sensors.

> **📍 Configuration Context**: This GPIO setting is part of the **advanced/expert settings** in the web UI. You need to configure this **before** enabling DS18B20 sensors. See [DS18B20 Enable](../DS18B20/Enable.md) for complete setup instructions.

## Value
Select `onewire` from the GPIO mode dropdown.

## When to Use This Setting

Configure a GPIO pin as `onewire` when you want to:
- Connect DS18B20 temperature sensor(s) to monitor device temperature
- Enable environmental monitoring for outdoor installations
- Set up multi-sensor temperature chains for thermal analysis

**Next Step**: After configuring this GPIO pin, go to the [DS18B20] configuration section and set `Enable = true`.

## Compatible GPIO Pins

**All of these pins work with the RMT-based 1-Wire driver:**

| GPIO | Recommended | Notes |
|------|-------------|-------|
| **IO12** | ✅ **Yes** | Fully available (SD D2 unused in 1-bit mode), no conflicts |
| **IO13** | ✅ **Yes** | Available with built-in pull-up, no conflicts |
| **IO3** | ⚠️ Works but... | UART RX - requires disabling USB logging |
| **IO1** | ⚠️ Works but... | UART TX - requires disabling USB logging |

**Technical Note:** The ESP32 RMT peripheral can use **any GPIO pin** without hardware restrictions. GPIO1 and GPIO3 work perfectly with RMT, but they're used for USB serial communication by default.

**Recommended**: IO12 or IO13 for hassle-free operation

### When to Use GPIO1 or GPIO3

✅ **Good for:**
- Production deployments without USB debugging
- Remote installations where serial logging isn't needed
- When GPIO12/13 are already in use

❌ **Avoid if:**
- You need USB serial console for debugging
- You rely on log output via USB
- You're still in development/testing phase

## Wiring

### Single DS18B20
```
DS18B20 Sensor    ESP32-CAM
--------------    ---------
VDD       -----> 3.3V
GND       -----> GND
DATA      -----> GPIO12 (with 4.7kΩ pull-up to 3.3V)
```

### Chained DS18B20 (Multiple Sensors)
```
            4.7kΩ Pull-up
3.3V -------|-----|
            |     |
          Data    |
ESP32 ---|--+     |
         |  |     |
    DS18B20_1     |
         |  |     |
    DS18B20_2     |
         |  |     |
    DS18B20_3     |
         |        |
GND  ---|--------|
```

All sensors share the same 3 wires. Each is identified by its unique 64-bit ROM code.

## Multiple Sensor Use Case

**Indoor vs. Outdoor Temperature Monitoring:**

Chain 3 DS18B20 sensors on one GPIO:
1. **Inside enclosure** - Monitor electronics temperature
2. **Outside enclosure** - Ambient air temperature
3. **Water pipe** - Actual measured object temperature

Each publishes to MQTT with ROM ID for identification:
- `enclosure/temperature/28-01234567890A` (inside)
- `enclosure/temperature/28-ABCDEF123456` (outside)
- `enclosure/temperature/28-FEDCBA987654` (pipe)

## Safety Considerations

⚠️ **Do NOT use**:
- IO0 (boot mode selection)
- IO4 (used for SD card or flash)
- IO14, IO15 (SD card)

## Related Parameters

- [DS18B20 Enable](../DS18B20/Enable.md) - Enable the sensor
- [DS18B20 Interval](../DS18B20/Interval.md) - Reading frequency

## Example Configuration

```ini
[GPIO]
IO12 = onewire

[DS18B20]
Enable = true
Interval = 300           ; Read every 5 minutes
MQTT_Enable = true
MQTT_Topic = enclosure/temperature
```

## Sensor Chaining Benefits

### Multi-Point Monitoring
- **Temperature gradient analysis**: Detect hot spots
- **Thermal management**: Track cooling effectiveness
- **Failure prediction**: Spot abnormal temperature rises
- **Environment vs. Device**: Compare ambient to internal temps

### Example Thermal Monitoring
```
Outdoor Meter Reader Installation:
├─ DS18B20 #1: Outside air (-15°C to +45°C range)
├─ DS18B20 #2: Inside enclosure (monitor for overheating)
└─ DS18B20 #3: ESP32-CAM module (direct component temp)

Alert if:
- Outside < -20°C → Activate heater
- Enclosure > 60°C → Cooling issue
- Camera module > 70°C → Thermal shutdown imminent
```

## Troubleshooting

**No sensor detected:**
1. Check wiring and 4.7kΩ pull-up resistor
2. Verify sensor is DS18B20 (not DHT22 or other)
3. Test sensor individually before chaining
4. Check ROM IDs in logs

**Intermittent readings:**
- Cable too long (max ~100m for reliable operation)
- Electromagnetic interference
- Weak pull-up resistor (use 4.7kΩ, not 10kΩ)
- Poor connections

**Wrong temperature values:**
- Check sensor is in correct environment
- Verify ROM ID matches physical sensor
- Sensor may be damaged (replace if readings impossible)
