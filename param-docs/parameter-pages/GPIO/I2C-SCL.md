# GPIO I²C SCL Mode

Configure a GPIO pin as I²C SCL (clock line) for SHT3x temperature & humidity sensor.

> **📍 Configuration Context**: This GPIO setting is part of the **advanced/expert settings** in the web UI. You need to configure **both SDA and SCL** pins **before** enabling the SHT3x sensor. See [SHT3x Enable](../SHT3x/Enable.md) for complete setup instructions.

## Value
Select `i2c-scl` from the GPIO mode dropdown.

## When to Use This Setting

Configure a GPIO pin as `i2c-scl` when you want to:
- Connect an SHT3x sensor to monitor temperature and humidity
- Enable environmental monitoring for outdoor installations
- Track enclosure conditions to prevent device failures

**Important**: You must also configure a second GPIO pin as `i2c-sda` (data line). See [GPIO I²C SDA](I2C-SDA.md).

**Next Step**: After configuring both SDA and SCL GPIO pins, go to the [SHT3x] configuration section and set `Enable = true`.

## Compatible GPIO Pins

| GPIO | Recommended | Notes |
|------|-------------|-------|
| **IO13** | ✅ **Yes** | Best choice - has built-in pull-up resistor |
| **IO12** | ⚠️ Possible | Better used for SDA |
| **IO3** | ⚠️ Caution | Requires disabling USB logging |
| **IO1** | ⚠️ Caution | UART TX - requires disabling USB logging |

**Recommended Configuration:**
- SCL: IO13 (built-in pull-up makes it ideal for clock)
- SDA: IO12 (use for data line)

## Wiring

```
SHT3x Sensor      ESP32-CAM
------------      ---------
VDD       -----> 3.3V (NOT 5V!)
GND       -----> GND
SDA       -----> GPIO12 (with 4.7kΩ pull-up to 3.3V)
SCL       -----> GPIO13 (with 4.7kΩ pull-up to 3.3V)
```

**Critical**: Always add external 4.7kΩ pull-up resistors on both SDA and SCL lines, even though IO13 has an internal pull-up.

## Safety Considerations

⚠️ **Do NOT use these pins**:
- IO0 (boot mode selection - device won't boot)
- IO4 (used for SD card or flash LED)
- IO14, IO15 (SD card - will corrupt data)

Using these restricted pins can prevent the device from booting or cause SD card corruption.

## I²C Protocol Basics

### What is I²C?
I²C (Inter-Integrated Circuit) is a two-wire serial communication protocol:
- **SCL (Clock Line)**: Master device (ESP32) generates clock pulses
- **SDA (Data Line)**: Bidirectional data transmission

### Why Two Wires?
- **SCL**: Synchronizes data transmission (clock signal)
- **SDA**: Carries actual data between ESP32 and sensor
- Both lines need pull-up resistors to work properly

### Multiple Devices
I²C supports multiple sensors on the same bus (each with unique address):
- Same SDA/SCL pins can connect to multiple I²C devices
- Each device identified by 7-bit address (e.g., SHT3x at 0x44 or 0x45)

## Related Parameters

- [I²C SDA](I2C-SDA.md) - Data line configuration (required pair)
- [SHT3x Enable](../SHT3x/Enable.md) - Enable the sensor
- [SHT3x Address](../SHT3x/Address.md) - I²C address selection
- [SHT3x I2C Frequency](../SHT3x/I2C_Frequency.md) - Bus speed configuration

## Example Configuration

```ini
[GPIO]
IO12 = i2c-sda    # Data line
IO13 = i2c-scl    # Clock line (this setting)

[SHT3x]
Enable = true
Address = 0x44
I2C_Frequency = 100000    # 100 kHz (standard mode)
Interval = -1             # Follow main flow cycle
MQTT_Enable = true
MQTT_Topic = enclosure/climate
InfluxDB_Enable = true
InfluxDB_Measurement = environment
```

## Troubleshooting

**Sensor not detected:**
1. ✅ Verify both `i2c-sda` AND `i2c-scl` are configured
2. ✅ Check wiring connections (swap SDA/SCL if unsure)
3. ✅ Confirm pull-up resistors (4.7kΩ) are installed on both lines
4. ✅ Check power (3.3V, not 5V!)
5. ✅ Try scanning for I²C address in device logs
6. ✅ Lower I²C frequency to 100kHz in SHT3x settings

**Communication errors:**
- Swap SDA and SCL wires (easy to reverse them)
- Check for loose connections or solder joints
- Verify no short circuits between pins
- Ensure proper grounding

**Intermittent readings:**
- Electromagnetic interference (move away from power wires)
- Cable too long (keep under 30cm for reliability)
- Pull-up resistors too weak (use 4.7kΩ, not 10kΩ)
- Poor power supply quality

## Advanced: I²C Frequency Selection

Default: 100 kHz (standard mode) - Most reliable

Available speeds:
- **100 kHz**: Standard mode, maximum compatibility
- **400 kHz**: Fast mode, if wires are short (<15cm)
- **1 MHz**: Fast-mode plus (experimental, not recommended)

**Recommendation**: Keep at 100 kHz unless you have a specific reason to increase speed. Higher frequencies are more susceptible to noise and wiring issues.
