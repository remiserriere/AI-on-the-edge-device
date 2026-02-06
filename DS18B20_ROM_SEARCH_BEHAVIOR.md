# DS18B20 ROM Search Behavior

## Question: Is it listing the sensors on the bus on each read or on each startup?

**Answer: ROM search happens ONCE at startup only, NOT on every read.**

## How It Works

### At Device Startup (Once)

When the device boots and initializes sensors, the following happens:

1. **ROM Search Algorithm Executes** - Scans the 1-Wire bus for all DS18B20 devices
2. **ROM IDs Are Cached** - All discovered sensor ROM IDs are stored in memory
3. **Initial Temperatures Read** - Each sensor is read once to verify it works
4. **Cached List Used Forever** - This list is used for all future reads until restart

### During Normal Operation (Every Read Cycle)

When the sensor manager calls `readData()`:

1. **Uses Cached ROM IDs** - No bus scanning occurs
2. **Addresses Each Sensor** - Uses MATCH_ROM command with cached ROM ID
3. **Reads Temperature** - Reads each sensor individually
4. **Fast & Efficient** - No expensive ROM search overhead

## Example Log Output

Here's what you'll see in the logs:

```
[INFO] DS18B20: Initializing DS18B20 sensor on GPIO12
[INFO] DS18B20: === DS18B20 ROM Search (startup only) ===
[INFO] DS18B20: Scanning 1-Wire bus for DS18B20 devices...
[INFO] DS18B20: ROM search complete: Found 3 DS18B20 sensor(s)
[INFO] DS18B20: Discovered ROM IDs (will be used for all future reads):
[INFO] DS18B20:   Sensor #1: 28-0123456789AB
[INFO] DS18B20:   Sensor #2: 28-FEDCBA987654
[INFO] DS18B20:   Sensor #3: 28-AABBCCDD1122
[INFO] DS18B20: Reading initial temperatures from discovered sensors...
[INFO] DS18B20:   Sensor #1 initial temp: 24.5°C
[INFO] DS18B20:   Sensor #2 initial temp: 15.2°C
[INFO] DS18B20:   Sensor #3 initial temp: 12.8°C
[INFO] DS18B20: === DS18B20 initialization complete ===
[INFO] DS18B20: Future reads will use these 3 cached sensor(s) without re-scanning
```

Later during operation (every read cycle):

```
[DEBUG] DS18B20: Sensor #1 (28-0123456789AB): 24.7°C
[DEBUG] DS18B20: Sensor #2 (28-FEDCBA987654): 15.3°C
[DEBUG] DS18B20: Sensor #3 (28-AABBCCDD1122): 12.9°C
```

Notice: **No ROM search messages during normal reads!**

## Performance Implications

### ROM Search (Once at Startup)
- **Duration**: ~100-200ms per sensor detected
- **Frequency**: Once per device boot
- **Impact**: Negligible (only at startup)

### Temperature Reads (Every Cycle)
- **Duration**: ~800ms per sensor (750ms conversion + 50ms communication)
- **Frequency**: Based on configured interval (e.g., every 5 minutes)
- **Impact**: Efficient - no ROM search overhead

### Example with 3 Sensors
- **Startup**: 2-3 seconds total (includes ROM search + initial reads)
- **Each Read Cycle**: ~2.4 seconds (800ms × 3 sensors)
- **No ROM Search Overhead**: Saves ~300-600ms per read cycle

## Hot-Plugging Sensors

**Important: Hot-plugging is NOT supported.**

If you add or remove sensors after device startup:
- ❌ New sensors will NOT be automatically detected
- ❌ Removed sensors will cause read errors
- ✅ Device must be **restarted** to detect sensor changes

This is by design for:
- Performance (no constant bus scanning)
- Stability (consistent sensor list)
- Typical use case (sensors are permanently installed)

## When is This Behavior Appropriate?

✅ **Good for:**
- Fixed sensor installations
- Permanently wired sensors
- Industrial/commercial monitoring
- Outdoor installations
- Any scenario where sensors don't change

❌ **Not ideal for:**
- Development/testing with frequent sensor changes
- Portable/temporary installations
- Educational environments with sensor experimentation

For these cases, simply restart the device to re-scan for sensors.

## Technical Implementation

The ROM search follows the standard Dallas/Maxim 1-Wire protocol:

```cpp
// In init() - called once at startup
int deviceCount = performRomSearch(_romIds);  // Discovers all sensors
LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Found " + std::to_string(deviceCount) + " sensors");

// In readData() - called every cycle
for (size_t i = 0; i < _romIds.size(); i++) {  // Uses cached list
    readSensorByRom(_romIds[i], temp);  // Addresses by cached ROM ID
}
```

## Summary

| Aspect | Behavior |
|--------|----------|
| **ROM Search Frequency** | Once at startup only |
| **Sensor Discovery** | Automatic during initialization |
| **Read Frequency** | Based on configured interval |
| **Read Method** | Uses cached ROM IDs |
| **Hot-Plug Support** | No (restart required) |
| **Performance Impact** | Minimal (no constant scanning) |
| **Typical Startup Time** | 2-3 seconds for 3 sensors |
| **Typical Read Time** | ~800ms per sensor |

## Questions?

If you need different behavior:
1. **Re-scan periodically**: Would require code modification (not recommended - adds complexity)
2. **Re-scan on demand**: Could add a configuration trigger (feature request)
3. **Current behavior**: Optimal for 99% of use cases ✅
