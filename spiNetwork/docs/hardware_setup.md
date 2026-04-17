# Hardware Setup Guide for ESP32 SPI Network Bridge

This document provides detailed hardware setup instructions for implementing the ESP32 SPI network bridge.

## Required Components

### ESP32 Device
- **ESP32-WROOM-1** development board
- USB cable for programming and power
- Stable 3.3V power supply (optional but recommended)

### Host MCU Options
- **Arduino Uno/Nano/Mega**: For simple applications
- **STM32 Blue Pill**: For more performance
- **Raspberry Pi**: For Linux-based hosts
- **ESP32/ESP8266**: For dual-ESP32 setups

### Connection Materials
- Jumper wires (male-to-female, male-to-male)
- Breadboard (for prototyping)
- 3.3V logic level converter (if host uses 5V)

## Pin Connections

### Standard ESP32-WROOM-1 Pinout

```
ESP32-WROOM-1
+-------------------+
|                   |
|  EN   3V3   GND   |
|                   |
|  IO2   IO4   IO16 |
|                   |
|  IO5   IO18  IO19 |
|                   |
|  IO21  IO22  IO23 |
|                   |
|  TX0   RX0   IO0  |
|                   |
+-------------------+
```

### SPI Connection Table

| ESP32 Pin | Function | Host MCU Pin | Voltage | Notes |
|-----------|----------|--------------|---------|-------|
| GPIO18    | SCLK     | SPI CLK      | 3.3V    | Clock signal |
| GPIO19    | MISO     | SPI MISO     | 3.3V    | ESP32 to Host |
| GPIO23    | MOSI     | SPI MOSI     | 3.3V    | Host to ESP32 |
| GPIO5     | CS       | SPI CS       | 3.3V    | Chip Select |
| GPIO4     | Handshake| GPIO         | 3.3V    | Communication control |
| GPIO2     | Data RDY | GPIO         | 3.3V    | Data ready indicator |
| 3V3       | Power    | 3.3V         | 3.3V    | Power supply |
| GND       | Ground   | GND          | -       | Common ground |

## Voltage Level Considerations

### 3.3V Systems (Recommended)
- Most modern MCUs (STM32, ESP32, Raspberry Pi)
- Direct connection possible
- Best signal integrity

### 5V Systems (Arduino Uno/Nano)
- **Required**: Logic level converter
- Convert 5V signals to 3.3V for ESP32
- ESP32 outputs are safe for 5V inputs (tolerant)

### Logic Level Converter Wiring
```
Arduino (5V) -----> Converter -----> ESP32 (3.3V)
     HV                 LV
     |                  |
   5V ----> HV         3.3V ----> LV
   GND ----> GND       GND ----> GND
```

## Wiring Diagrams

### Arduino Uno Connection
```
Arduino Uno                    ESP32-WROOM-1
----------                    -------------
D13 (SCLK)  <-----> GPIO18 (SCLK)
D12 (MISO)  <-----> GPIO19 (MISO)
D11 (MOSI)  <-----> GPIO23 (MOSI)
D10 (CS)   <-----> GPIO5  (CS)
D9  (HSK)  <-----> GPIO4  (Handshake)
D8  (DRDY) <-----> GPIO2  (Data Ready)
5V          -----> VIN (if using external power)
GND         -----> GND
```

### STM32 Blue Pill Connection
```
STM32 Blue Pill              ESP32-WROOM-1
---------------              -------------
PA5 (SCLK)    <-----> GPIO18 (SCLK)
PA6 (MISO)    <-----> GPIO19 (MISO)
PA7 (MOSI)    <-----> GPIO23 (MOSI)
PB0 (CS)      <-----> GPIO5  (CS)
PB1 (HSK)     <-----> GPIO4  (Handshake)
PB10 (DRDY)   <-----> GPIO2  (Data Ready)
3.3V          -----> 3V3
GND           -----> GND
```

### Raspberry Pi Connection
```
Raspberry Pi                  ESP32-WROOM-1
-----------                  -------------
GPIO11 (SCLK) <-----> GPIO18 (SCLK)
GPIO9  (MISO) <-----> GPIO19 (MISO)
GPIO10 (MOSI) <-----> GPIO23 (MOSI)
GPIO8  (CS)   <-----> GPIO5  (CS)
GPIO7  (HSK)  <-----> GPIO4  (Handshake)
GPIO12 (DRDY) <-----> GPIO2  (Data Ready)
3.3V          -----> 3V3
GND           -----> GND
```

## Power Considerations

### Power Requirements
- **ESP32**: 160-260mA during normal operation
- **Peak Current**: Up to 500mA during WiFi transmission
- **Recommended Supply**: 500mA minimum

### Power Options

#### Option 1: USB Power (Simple)
- Power ESP32 via USB
- Power host MCU separately
- Common ground connection

#### Option 2: Shared 3.3V Supply
- Single 3.3V power supply (500mA+)
- Power both devices from same source
- Better signal integrity

#### Option 3: Battery Power
- LiPo battery with 3.3V regulator
- Suitable for portable applications
- Monitor battery voltage

## Signal Integrity

### Best Practices
1. **Short Wires**: Keep connections as short as possible
2. **Ground Plane**: Use breadboard with ground plane if possible
3. **Decoupling**: Add 100nF capacitor near ESP32 power pins
4. **Shielding**: Use shielded cables for long distances

### Maximum Cable Length
- **Breadboard**: <10cm recommended
- **Jumper Wires**: <20cm recommended
- **Ribbon Cable**: <50cm with proper grounding

### Troubleshooting Signal Issues
- Add pull-up resistors (10k) on CS and handshaking lines
- Reduce SPI clock speed
- Check for ground loops
- Use oscilloscope to verify signals

## Physical Layout

### Breadboard Layout Example
```
+---------------------------------------+
| Breadboard                            |
|                                       |
| ESP32          Host MCU                |
| +-----+        +-----+                |
| |ESP32|        |MCU  |                |
| +-----+        +-----+                |
|                                       |
| Power Rails:                          |
| +3.3V -----+----+----+----+           |
| GND -------+----+----+----+           |
|                                       |
+---------------------------------------+
```

### PCB Design Considerations
- 4-layer PCB recommended for production
- Separate analog and digital grounds
- Proper decoupling capacitors
- EMI shielding for WiFi antenna

## Testing and Verification

### Basic Connectivity Test
1. **Power Test**: Verify both devices power on
2. **Ground Test**: Check continuity between grounds
3. **Signal Test**: Verify SPI signals with oscilloscope
4. **Communication Test**: Run simple SPI echo test

### Signal Quality Check
- **Clock Signal**: Clean square wave, no ringing
- **Data Signals**: Proper voltage levels, clean transitions
- **Handshaking**: Proper timing relationships

## Common Hardware Issues

### Power Issues
- **Brownouts**: Insufficient current supply
- **Noise**: Poor power supply filtering
- **Voltage Drops**: Long power wires

### Signal Issues
- **Crosstalk**: Signals interfering with each other
- **Reflections**: Impedance mismatch
- **Noise**: EMI interference

### Connection Issues
- **Bad Contacts**: Loose connections
- **Wrong Wiring**: Incorrect pin assignments
- **Short Circuits**: Accidental connections

## Safety Considerations

### Electrical Safety
- Double-check voltage levels
- Use proper grounding
- Avoid short circuits
- Use current limiting where appropriate

### ESD Protection
- Handle components with care
- Use anti-static measures
- Ground yourself before handling

## Bill of Materials

### Minimal Setup
- ESP32-WROOM-1 dev board: ~$10
- Arduino Uno: ~$20
- Jumper wires: ~$5
- **Total**: ~$35

### Recommended Setup
- ESP32-WROOM-1 dev board: ~$10
- STM32 Blue Pill: ~$2
- Logic level converter: ~$2
- Breadboard and wires: ~$10
- **Total**: ~$24

### Professional Setup
- Custom ESP32 PCB: ~$15
- Host MCU PCB: ~$10
- Enclosure: ~$5
- Professional cables: ~$10
- **Total**: ~$40

---

**Note**: Always verify pin assignments for your specific ESP32 board variant, as some boards may have different pin mappings or limited pin availability.
