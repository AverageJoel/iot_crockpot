# Development Wiring Guide

This document describes how to wire components for development/testing before the custom PCB is ready.

## ESP32 Development Board Pinout

Using a standard ESP32 DevKit V1 or similar.

### Temperature Sensor (GPIO 4)

**DS18B20 (Recommended for development)**

```
DS18B20          ESP32
─────────        ─────
VDD (Red)   ──── 3.3V
GND (Black) ──── GND
DQ (Yellow) ──── GPIO 4
                  │
                  ├── 4.7kΩ ── 3.3V (pull-up resistor)
```

**NTC Thermistor (Alternative)**

```
                 3.3V
                  │
                  ├── 10kΩ (reference resistor)
                  │
ESP32 GPIO 36 ───┤ (ADC input)
                  │
                  ├── NTC Thermistor (10kΩ @ 25°C)
                  │
                 GND
```

### Relay Module (GPIO 5)

**Using a relay module with opto-isolation:**

```
Relay Module     ESP32
────────────     ─────
VCC         ──── 5V (or 3.3V depending on module)
GND         ──── GND
IN          ──── GPIO 5
```

**IMPORTANT SAFETY NOTES:**
- Never work on mains wiring while plugged in
- Use a relay rated for your mains voltage (120V/240V AC)
- Keep high voltage and low voltage sections separated
- Consider using a commercial smart plug for initial testing

### OLED Display (Optional)

**SSD1306 128x64 I2C OLED:**

```
OLED Display     ESP32
────────────     ─────
VCC         ──── 3.3V
GND         ──── GND
SDA         ──── GPIO 21
SCL         ──── GPIO 22
```

### Control Buttons (Optional)

```
Button           ESP32
──────           ─────
UP Button   ──── GPIO 12 ──┬── GND (internal pull-up enabled)
DOWN Button ──── GPIO 13 ──┤
SELECT      ──── GPIO 14 ──┘
```

## Complete Wiring Diagram

```
                    ┌─────────────────────────────────┐
                    │         ESP32 DevKit            │
                    │                                 │
        3.3V ◄──────┤ 3V3                       GND ├──────► GND
                    │                                 │
   DS18B20 DQ ◄─────┤ GPIO 4              GPIO 5 ├─────► Relay IN
                    │                                 │
    OLED SDA ◄──────┤ GPIO 21            GPIO 12 ├─────► Button UP
    OLED SCL ◄──────┤ GPIO 22            GPIO 13 ├─────► Button DOWN
                    │                     GPIO 14 ├─────► Button SELECT
                    │                                 │
                    │ USB (Programming & Power)       │
                    └─────────────────────────────────┘
```

## Power Supply Options

### Development (USB Power)
- Power ESP32 via USB
- Use 5V relay module powered from VIN pin
- Suitable for testing

### Standalone Operation
- 5V 2A power supply recommended
- Use voltage regulator for 3.3V components
- Consider battery backup for WiFi/settings retention

## Testing Checklist

1. [ ] ESP32 powers on and connects to WiFi
2. [ ] Temperature sensor reads reasonable values
3. [ ] Relay clicks when state changes (test with multimeter)
4. [ ] OLED displays status (if connected)
5. [ ] Buttons change state (if connected)

## Safety Reminders

- **NEVER** work on mains wiring while energized
- Use proper wire gauges for mains current
- Ensure relay is rated for your load
- Keep a fire extinguisher nearby during testing
- Consider using a GFCI outlet for testing
