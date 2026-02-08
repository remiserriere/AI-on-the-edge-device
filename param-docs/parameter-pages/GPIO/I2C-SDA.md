# GPIO I²C SDA Mode

Configure a GPIO pin as I²C SDA (data line) for SHT3x temperature & humidity sensor.

> **📍 Configuration Context**: This GPIO setting is part of the **advanced/expert settings** in the web UI. You need to configure **both SDA and SCL** pins **before** enabling the SHT3x sensor. See [SHT3x Enable](../SHT3x/Enable.md) for complete setup instructions.

## Value
Select `i2c-sda` from the GPIO mode dropdown.

## When to Use This Setting

Configure a GPIO pin as `i2c-sda` when you want to:
- Connect an SHT3x sensor to monitor temperature and humidity
- Enable environmental monitoring for outdoor installations
- Track enclosure conditions to prevent device failures

**Important**: You must also configure a second GPIO pin as `i2c-scl` (clock line). See [GPIO I²C SCL](I2C-SCL.md).

**Next Step**: After configuring both SDA and SCL GPIO pins, go to the [SHT3x] configuration section and set `Enable = true`.

## ⚠️ CRITICAL: GPIO12 Boot Strapping Pin Conflict

**DO NOT use GPIO12 for I²C SDA!** GPIO12 is a critical strapping pin that determines flash voltage at boot:
- GPIO12 LOW at boot = 3.3V flash voltage (standard)
- GPIO12 HIGH at boot = 1.8V flash voltage (rare)

When using GPIO12 with I²C sensors requiring pull-up resistors, the pull-up holds GPIO12 HIGH during boot, causing the ESP32 to switch to 1.8V flash mode and **preventing the device from booting completely** on standard 3.3V flash modules.

**Symptom:** ESP32 does not boot when sensor is connected, boots fine when disconnected.

## Compatible GPIO Pins

| GPIO | Recommended | Notes |
|------|-------------|-------|
| **IO1** | ✅ **YES** | UART TX - requires disabling USB logging |
| **IO3** | ✅ **YES** | UART RX - requires disabling USB logging |
| **IO13** | ⚠️ CONDITIONAL | Only safe if 1-line SD card mode enabled (`__SD_USE_ONE_LINE_MODE__`) |
| **IO12** | ❌ **NEVER USE** | **STRAPPING PIN** - pull-up will prevent boot! |
| **IO0** | ❌ **NEVER USE** | Boot mode selection strapping pin |

**Recommended Configuration:**
- SDA: IO3 (with 4.7kΩ pull-up to 3.3V) *requires disabling USB logging*
- SCL: IO1 (with 4.7kΩ pull-up to 3.3V) *requires disabling USB logging*

Alternative if 1-line SD mode enabled:
- SDA: IO13
- SCL: IO1 or IO3

## Wiring

```
SHT3x Sensor      ESP32-CAM
------------      ---------
VDD       -----> 3.3V
GND       -----> GND
SDA       -----> GPIO3 (with 4.7kΩ pull-up to 3.3V) *
SCL       -----> GPIO1 (with 4.7kΩ pull-up to 3.3V) *

* Requires disabling USB serial logging in configuration
```

**Important**: External 4.7kΩ pull-up resistors are required for both SDA and SCL lines.

## Safety Considerations

⚠️ **Do NOT use these pins**:
- **IO12** - **CRITICAL: Strapping pin!** Pull-up resistor will prevent boot entirely
- **IO0** - Boot mode selection strapping pin
- **IO2** - SD card D0
- **IO4** - SD card D1 / Flash LED
- **IO14** - SD card CLK
- **IO15** - SD card CMD

Using these pins can prevent the device from booting or corrupt the SD card.

## Related Parameters

- [I²C SCL](GPIO-I2C-SCL.md) - Clock line configuration
- [SHT3x Enable](../SHT3x/Enable.md) - Enable the sensor
- [SHT3x I2C Frequency](../SHT3x/I2C_Frequency.md) - Bus speed configuration

## Example Configuration

```ini
[GPIO]
IO3 = i2c-sda
IO1 = i2c-scl
# NOTE: USB serial logging must be disabled when using GPIO1/GPIO3

[SHT3x]
Enable = true
Address = 0x44
I2C_Frequency = 100000
```

## Troubleshooting

**Device won't boot when sensor connected:**
- **CAUSE**: GPIO12 strapping pin conflict with pull-up resistor
- **SOLUTION**: Use GPIO1/GPIO3 instead of GPIO12
- **Boot error logs**: May show `invalid header: 0xffffffff` or `ets_main.c` errors

**Sensor not detected:**
1. Check wiring connections
2. Verify pull-up resistors are installed (4.7kΩ to 3.3V)
3. Confirm USB logging is disabled if using GPIO1/GPIO3
4. Try lowering I2C frequency to 100kHz
5. Use I²C scanner tool to detect address

**Bus errors:**
- Check for short circuits on SDA/SCL lines
- Verify sensor is powered (3.3V, not 5V!)
- Ensure proper grounding
- Check for electromagnetic interference
