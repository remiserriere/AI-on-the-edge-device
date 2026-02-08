# Parameter `LEDType`
Default Value: `WS2812`

Type of the `WS2812x` addressable RGB LED which is connected to GPIO12 (See `IO12` parameter).

## Supported LED Types

- **WS2812** - Original WS2812 LEDs (default)
- **WS2812B** - Improved WS2812B variant (most common)
- **WS2812B_NEWVARIANT** - Newer WS2812B with different timing
- **WS2812B_OLDVARIANT** - Older WS2812B timing
- **WS2812C** - WS2812C variant

## Why GPIO12 is Safe for WS2812 LEDs

**Important:** While GPIO12 cannot be used for I²C or 1-Wire sensors (due to boot strapping pin conflict with pull-up resistors), **WS2812 LEDs are perfectly safe on GPIO12**.

### Technical Reason

- **WS2812 uses NO pull-up resistors** - Data line is actively driven (push-pull)
- **Idle state is LOW** - When unpowered, data line does NOT pull GPIO12 HIGH
- **Boot behavior** - GPIO12 reads LOW at boot → 3.3V flash mode → normal boot ✅

This is why the original design specifically chose GPIO12 for WS2812 external flash LEDs.

See [IO12 Parameter Documentation](IO12.md#exception-ws2812-leds-are-safe-on-gpio12) for complete technical explanation.

## Configuration

Set this parameter to match your specific LED type for optimal timing and compatibility.
