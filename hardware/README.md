# IoT Crockpot Hardware

KiCad project files for the IoT Crockpot controller PCB.

## Project Files

- `iot_crockpot.kicad_pro` - KiCad project file
- `iot_crockpot.kicad_sch` - Schematic
- `iot_crockpot.kicad_pcb` - PCB layout

## Directory Structure

```
hardware/
├── iot_crockpot.kicad_pro    # Project file
├── iot_crockpot.kicad_sch    # Schematic
├── iot_crockpot.kicad_pcb    # PCB layout
├── symbols/                   # Custom schematic symbols
├── footprints/                # Custom component footprints
├── 3dmodels/                  # 3D models for visualization
├── production/                # Generated output files
│   ├── gerbers/              # Gerber files for fabrication
│   ├── bom/                  # Bill of materials
│   └── assembly/             # Assembly drawings
└── README.md                  # This file
```

## Components (TBD)

### Core Components
- **MCU**: ESP32-WROOM-32 or ESP32-S3 module
- **Temperature Sensor**: Options under consideration:
  - DS18B20 (digital, waterproof probe available)
  - MAX31855 + thermocouple (higher temperature range)
  - NTC thermistor (simple, low cost)
- **Relay/SSR**: Options under consideration:
  - Solid State Relay (SSR) for silent operation
  - Mechanical relay with snubber circuit

### Optional Components
- **Display**: SSD1306 128x64 OLED or small TFT
- **Buttons**: 3x tactile switches for local control
- **Power**: 5V/3.3V regulation from USB or barrel jack

## Design Considerations

### Safety
- Mains voltage isolation (relay side vs logic side)
- Proper creepage and clearance distances
- Fuse protection on mains input
- Ground plane for noise immunity

### Thermal
- Temperature sensor placement for accurate readings
- Heat dissipation for SSR (if used)

### EMC
- Decoupling capacitors near ESP32
- Ground pour on both layers
- Keep high-frequency traces short

## Fabrication Notes

When ordering PCBs:
1. Generate Gerbers to `production/gerbers/`
2. Specify 2-layer board, 1.6mm thickness
3. HASL or ENIG finish
4. Green solder mask (or preference)
5. White silkscreen

## Version History

- **v0.1** - Initial placeholder schematic
