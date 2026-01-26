# PCB Assembly Instructions

This guide covers assembling the IoT Crockpot controller PCB.

## Before You Start

### Required Tools
- Soldering iron (temperature controlled, ~350°C / 660°F)
- Solder (lead-free recommended, 0.5-0.8mm diameter)
- Flux (no-clean recommended)
- Tweezers (fine tip)
- Multimeter
- Magnifying glass or microscope
- ESD protection (wrist strap, mat)

### Optional but Helpful
- Hot air rework station
- Solder paste and stencil (for SMD components)
- Pick and place tweezers
- Helping hands / PCB holder
- Isopropyl alcohol for cleaning

## Component Checklist

*Note: Component values TBD based on final schematic*

### SMD Components
- [ ] ESP32-WROOM-32 module
- [ ] Decoupling capacitors (100nF, 10µF)
- [ ] USB-C connector
- [ ] Voltage regulator (AMS1117-3.3 or similar)
- [ ] Status LEDs
- [ ] Resistors (various values)

### Through-Hole Components
- [ ] Relay or SSR
- [ ] Terminal blocks (for mains wiring)
- [ ] Barrel jack (optional, for external power)
- [ ] Pin headers (for programming/expansion)
- [ ] Temperature sensor connector

### Connectors
- [ ] OLED display header (4-pin I2C)
- [ ] Temperature sensor connector
- [ ] Button connectors (if separate PCB)

## Assembly Order

Follow this order for easiest assembly:

### 1. SMD Components (if applicable)

If using solder paste and reflow:
1. Apply solder paste with stencil
2. Place SMD components
3. Reflow in oven or with hot air

If hand soldering:
1. Start with smallest components
2. Work up to larger packages
3. Save ESP32 module for last

### 2. Voltage Regulator and Power Section

1. Solder the 3.3V regulator
2. Add input/output capacitors
3. Test: Apply 5V, verify 3.3V output

### 3. ESP32 Module

1. Align module carefully
2. Tack opposite corners first
3. Solder remaining pads
4. Check for bridges under module

### 4. USB Connector

1. Align connector
2. Solder mounting tabs first
3. Then solder signal pins
4. Test USB connection

### 5. Through-Hole Components

1. Install resistors and small components
2. Add terminal blocks
3. Install any remaining headers

### 6. Relay/SSR

1. Install relay module
2. Verify orientation
3. Double-check high-voltage connections

## Testing Procedure

### Visual Inspection
1. Check for solder bridges
2. Verify component orientation
3. Look for cold joints

### Power Test
1. **Do NOT connect mains power yet**
2. Connect USB power
3. Measure 3.3V rail
4. Check for excessive heat

### Programming Test
1. Connect USB to computer
2. Verify COM port appears
3. Flash test firmware
4. Check serial output

### Functional Test
1. Load full firmware
2. Verify WiFi connection
3. Test temperature sensor
4. Test relay switching (low voltage only first)
5. Test Telegram commands

### High Voltage Test (CAUTION!)
1. Triple-check all connections
2. Use isolation transformer if available
3. Test with low-power load first (e.g., lamp)
4. Verify relay switches load correctly

## Safety Checklist

Before connecting to mains:

- [ ] All low-voltage testing complete
- [ ] Enclosure properly grounded (if metal)
- [ ] Creepage distances verified
- [ ] No exposed high-voltage traces
- [ ] Strain relief on power cord
- [ ] Fuse installed and correct rating
- [ ] GFCI outlet used for testing

## Enclosure Assembly

1. Verify PCB fits in enclosure
2. Mark and drill holes for:
   - Power cord entry
   - USB port access
   - Display window (if applicable)
   - Ventilation
3. Install cable glands
4. Mount PCB with standoffs
5. Connect all wires
6. Secure lid

## Troubleshooting

### ESP32 Won't Program
- Check USB cable (data cable, not charge-only)
- Verify COM port drivers installed
- Hold BOOT button while programming
- Check for solder bridges on USB connector

### No 3.3V Output
- Check input voltage (5V)
- Verify regulator orientation
- Check for shorts on 3.3V rail

### Temperature Sensor Not Working
- Verify pull-up resistor installed
- Check sensor wiring
- Test sensor independently

### Relay Not Switching
- Check GPIO output with multimeter
- Verify relay coil voltage matches supply
- Check flyback diode orientation (if discrete)

## Version Notes

*Add notes here when PCB revisions are made*

### Rev 0.1
- Initial prototype
- Known issues: TBD
