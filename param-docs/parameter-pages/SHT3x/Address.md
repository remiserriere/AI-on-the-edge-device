# SHT3x Address

Set the I²C address for the SHT3x temperature and humidity sensor.

## Value
- `0x44` - Default I²C address (most common)
- `0x45` - Alternative I²C address

## Description

The SHT3x sensor communicates over the I²C bus and can have one of two addresses, selectable by a hardware pin on the sensor module.

### Address Selection

Most SHT3x breakout boards use **0x44** as the default address. Some boards allow changing the address to **0x45** by:
- Cutting a trace
- Moving a jumper
- Bridging solder pads

Consult your sensor's documentation for address configuration.

### Why Two Addresses?

The dual-address capability allows you to connect **two SHT3x sensors** to the same I²C bus:
- One sensor configured at 0x44
- Another sensor configured at 0x45

This is useful for:
- Comparing inside vs. outside enclosure conditions
- Monitoring multiple locations with one device
- Redundant sensing for critical applications

## ⚠️ Prerequisites - GPIO Configuration Required!

**Before the SHT3x sensor can be detected at any address, you MUST configure GPIO pins for I²C communication.**

### Quick GPIO Setup

1. Navigate to the **Configuration** page
2. Scroll to the **GPIO** section
3. Click **"Show Expert Options"** or **"Advanced Settings"** to reveal GPIO configuration
4. Configure two pins:
   - One pin set to **`i2c-sda`** (e.g., IO12)
   - One pin set to **`i2c-scl`** (e.g., IO13)
5. Save configuration and restart

See detailed guides:
- [GPIO I²C SDA Configuration](../GPIO/I2C-SDA.md)
- [GPIO I²C SCL Configuration](../GPIO/I2C-SCL.md)
- [SHT3x Enable](Enable.md) - Complete setup walkthrough

### Physical Wiring

After GPIO configuration, wire the sensor:
- VDD → 3.3V (**NOT 5V!**)
- GND → GND
- SDA → Configured SDA GPIO (e.g., IO12)
- SCL → Configured SCL GPIO (e.g., IO13)
- Add 4.7kΩ pull-up resistors on both SDA and SCL lines

## Configuration Example

```ini
[GPIO]
IO12 = i2c-sda
IO13 = i2c-scl

[SHT3x]
Enable = true
Address = 0x44           ; Default address
Interval = -1
I2C_Frequency = 100000
```

## Troubleshooting

**Sensor not detected:**
1. Verify GPIO pins are configured as `i2c-sda` and `i2c-scl`
2. Check physical wiring (SDA, SCL, VDD, GND)
3. Confirm pull-up resistors (4.7kΩ) are installed
4. Try the alternative address (0x45 if using 0x44, or vice versa)
5. Use an I²C scanner tool to verify the sensor is responding
6. Check device logs for I²C initialization errors

**Wrong readings or erratic behavior:**
- EMI interference: Add shorter wires, better shielding
- Loose connections: Secure all pins
- Incorrect address: Verify hardware address matches config

## Related Parameters

- [SHT3x Enable](Enable.md) - Enable the sensor
- [SHT3x I2C_Frequency](I2C_Frequency.md) - I²C bus speed
- [SHT3x Interval](Interval.md) - Reading frequency
- [GPIO I²C SDA](../GPIO/I2C-SDA.md) - Data line configuration
- [GPIO I²C SCL](../GPIO/I2C-SCL.md) - Clock line configuration
