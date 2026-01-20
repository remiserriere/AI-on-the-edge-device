# GPIO I²C SDA Mode

Configure a GPIO pin as I²C SDA (data line) for SHT3x temperature & humidity sensor.

## Value
Select `i2c-sda` from the GPIO mode dropdown.

## Compatible GPIO Pins

| GPIO | Recommended | Notes |
|------|-------------|-------|
| **IO12** | ✅ **Yes** | Fully available (SD D2 unused in 1-bit mode) |
| **IO13** | ⚠️ Possible | Already has pull-up enabled (better for SCL) |
| **IO3** | ⚠️ Caution | Requires disabling USB logging |
| **IO1** | ⚠️ Caution | UART TX - requires disabling USB logging |

**Recommended Configuration:**
- SDA: IO12
- SCL: IO13 (pull-up already active)

## Wiring

```
SHT3x Sensor      ESP32-CAM
------------      ---------
VDD       -----> 3.3V
GND       -----> GND
SDA       -----> GPIO12 (with 4.7kΩ pull-up to 3.3V)
SCL       -----> GPIO13 (pull-up already enabled)
```

**Important**: External 4.7kΩ pull-up resistors are required for both SDA and SCL lines.

## Safety Considerations

⚠️ **Do NOT use**:
- IO0 (boot mode selection)
- IO4 (used for SD card or flash)
- IO14, IO15 (SD card)

Using these pins can prevent the device from booting or corrupt the SD card.

## Related Parameters

- [I²C SCL](GPIO-I2C-SCL.md) - Clock line configuration
- [SHT3x Enable](../SHT3x/Enable.md) - Enable the sensor
- [SHT3x I2C Frequency](../SHT3x/I2C_Frequency.md) - Bus speed configuration

## Example Configuration

```ini
[GPIO]
IO12 = i2c-sda
IO13 = i2c-scl

[SHT3x]
Enable = true
Address = 0x44
I2C_Frequency = 100000
```

## Troubleshooting

**Sensor not detected:**
1. Check wiring connections
2. Verify pull-up resistors are installed
3. Try lowering I2C frequency to 100kHz
4. Use I²C scanner tool to detect address

**Bus errors:**
- Check for short circuits on SDA/SCL lines
- Verify sensor is powered (3.3V, not 5V!)
- Ensure proper grounding
- Check for electromagnetic interference
