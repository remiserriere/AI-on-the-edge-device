# DS18B20 RMT Implementation - Troubleshooting & Fix

## Issue Reported
**Problem:** DS18B20 sensors not detected at all after RMT implementation was enabled.

**Log Output:** `[0d00h00m51s] 2026-02-07T21:42:40` (incomplete log, but sensor detection failed)

## Root Cause Analysis

### Issue Identified
The RMT implementation for ESP-IDF v5.x was missing critical GPIO configuration:

**Problem:** While the RMT TX channel was configured with `io_od_mode = true` for open-drain operation, the GPIO itself was not explicitly configured with a pull-up resistor.

**Location:** `onewire_rmt.c` line ~148-155 (IDF v5 init function)

**Missing Code:**
```c
gpio_set_pull_mode(gpio, GPIO_PULLUP_ONLY);
```

### Why This Caused Failure
1. 1-Wire protocol requires the bus to be pulled high by a resistor (typically 4.7kΩ external)
2. The ESP32 can also enable an internal pull-up (~45kΩ) which helps ensure the bus is high
3. Without the pull-up configuration, the bus may float or not reliably return to high state
4. This causes the presence pulse detection to fail → "no sensor detected"

### Comparison with IDF v4
The IDF v4.x implementation correctly includes:
```c
gpio_set_direction(gpio, GPIO_MODE_INPUT_OUTPUT_OD);
gpio_set_pull_mode(gpio, GPIO_PULLUP_ONLY);
```

The IDF v5.x implementation was missing the `gpio_set_pull_mode()` call.

## Immediate Fix Applied

### 1. Default to Software Mode (Immediate Stability)
**File:** `sensor_config.h`
**Change:** `USE_ONEWIRE_RMT` changed from `1` to `0`
**Result:** Sensors will use proven software bit-banging by default

```cpp
#ifndef USE_ONEWIRE_RMT
#define USE_ONEWIRE_RMT 0  // Default to software mode for stability
#endif
```

### 2. Fixed RMT Implementation (For Testing)
**File:** `onewire_rmt.c`
**Change:** Added GPIO pull-up configuration to IDF v5 init function

```c
// CRITICAL: Configure GPIO with pull-up for 1-Wire
// The io_od_mode flag enables open-drain in RMT, but we still need pull-up
gpio_set_pull_mode(gpio, GPIO_PULLUP_ONLY);
```

## Testing Instructions

### Option 1: Use Software Mode (Recommended - Safe)
**No action needed.** The default configuration now uses software mode which is proven and reliable.

Your sensors should be detected normally after rebuilding.

### Option 2: Test RMT Mode (Advanced)
If you want to help test the fixed RMT implementation:

1. **Edit** `code/components/jomjol_sensors/sensor_config.h`
2. **Change** line 18 from:
   ```cpp
   #define USE_ONEWIRE_RMT 0  // Software mode
   ```
   to:
   ```cpp
   #define USE_ONEWIRE_RMT 1  // RMT mode (testing)
   ```
3. **Rebuild** the firmware
4. **Flash** to your device
5. **Check logs** for:
   - `[INFO] ONEWIRE_RMT: 1-Wire RMT initialized on GPIO12 (IDF v5.x)`
   - `[INFO] DS18B20: ROM search complete: Found X DS18B20 sensor(s)`
6. **Report** results (sensors detected? CRC errors? temperature readings OK?)

## Expected Behavior After Fix

### With Software Mode (Default)
```
[INFO] DS18B20: Initializing DS18B20 sensor on GPIO12
[INFO] DS18B20: === DS18B20 ROM Search (startup only) ===
[INFO] DS18B20: Scanning 1-Wire bus for DS18B20 devices...
[INFO] DS18B20: ROM search complete: Found 3 DS18B20 sensor(s)
[INFO] DS18B20: Discovered ROM IDs:
[INFO] DS18B20:   Sensor #1: 28-0123456789AB
[INFO] DS18B20:   Sensor #2: 28-FEDCBA987654
[INFO] DS18B20:   Sensor #3: 28-AABBCCDD1122
```

### With RMT Mode (After Fix)
```
[INFO] ONEWIRE_RMT: 1-Wire RMT initialized on GPIO12 (IDF v5.x)
[INFO] DS18B20: Initializing DS18B20 sensor on GPIO12
[INFO] DS18B20: === DS18B20 ROM Search (startup only) ===
[INFO] DS18B20: Scanning 1-Wire bus for DS18B20 devices...
[INFO] DS18B20: ROM search complete: Found 3 DS18B20 sensor(s)
[INFO] DS18B20: Discovered ROM IDs:
[INFO] DS18B20:   Sensor #1: 28-0123456789AB
[INFO] DS18B20:   Sensor #2: 28-FEDCBA987654
[INFO] DS18B20:   Sensor #3: 28-AABBCCDD1122
```

## Additional Considerations

### Hardware Requirements (All Modes)
- **External pull-up resistor:** 4.7kΩ to 3.3V on data line is **required**
- Even with internal pull-up enabled, external resistor is needed for reliable operation
- Long cables or multiple sensors may need stronger pull-up (lower resistance)

### Other Potential Issues
If sensors still not detected after fix:

1. **Check ESP-IDF version:**
   - The code detects IDF v4 vs v5 automatically
   - Ensure your IDF installation is complete
   - Check compile logs for RMT-related errors

2. **Check hardware:**
   - Verify 4.7kΩ pull-up resistor is connected
   - Check sensor wiring (VDD, GND, DATA)
   - Test sensors individually before chaining

3. **Check GPIO availability:**
   - Ensure GPIO is not used by camera/SD card
   - Try different GPIO (12 or 13 recommended)

4. **Check RMT channel availability:**
   - If using many LED strips, RMT channels may be exhausted
   - RMT implementation prefers channels 4-7 to avoid conflicts

## Summary

**Root Cause:** Missing GPIO pull-up configuration in IDF v5 RMT implementation

**Fix Applied:**
1. ✅ Added `gpio_set_pull_mode()` to IDF v5 init function
2. ✅ Defaulted to software mode for stability
3. ✅ Updated documentation

**Current Status:**
- Software mode: ✅ Enabled by default (safe, reliable)
- RMT mode: ⚠️ Fixed and available for testing

**Next Steps:**
- Users should rebuild and verify sensors are detected
- Adventurous users can test RMT mode and report results
- After successful testing, RMT can be enabled by default again

## Feedback Requested

If you test the fixed RMT implementation, please report:
1. ✅ Sensors detected at boot? (yes/no)
2. ✅ Number of sensors found
3. ✅ Any CRC errors in logs?
4. ✅ Temperature readings accurate?
5. ✅ ESP-IDF version used

This will help validate the fix before re-enabling RMT by default.
