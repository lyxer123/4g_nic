# ESP32 SPI Network Bridge Examples

This directory contains comprehensive examples for implementing SPI network bridge functionality using ESP32-WROOM-1 to provide internet access to other MCUs through the SPI interface.

## Overview

The ESP-IoT-Bridge component supports SPI interface communication, allowing ESP32 to act as a network bridge for other microcontrollers. This enables devices without native WiFi capabilities to access the internet through the ESP32's SPI interface.

## Directory Structure

```
spiNetwork/
|-- esp-idf_example/          # ESP-IDF based implementation
|   |-- main.c               # Main application code
|   |-- CMakeLists.txt       # Build configuration
|   |-- sdkconfig.defaults   # Default configuration
|
|-- arduino_example/         # Arduino based implementation
|   |-- spi_network_bridge.ino  # Arduino sketch
|   |-- README.md            # Arduino-specific documentation
|
|-- docs/                    # Additional documentation
```

## Hardware Requirements

- **ESP32-WROOM-1**: Main bridge device
- **Host MCU**: Any microcontroller with SPI interface (Arduino, STM32, Raspberry Pi, etc.)
- **SPI Connections**: 6-wire SPI interface with handshaking

### Pin Connections (ESP32-WROOM-1)

| ESP32 Pin | Function | Description |
|-----------|----------|-------------|
| GPIO18    | SCLK     | SPI Clock |
| GPIO19    | MISO     | Master In Slave Out |
| GPIO23    | MOSI     | Master Out Slave In |
| GPIO5     | CS       | Chip Select |
| GPIO4     | Handshake| Communication handshake |
| GPIO2     | Data Ready| Data ready indicator |

## Features

### ESP-IDF Example
- **Full ESP-IoT-Bridge Integration**: Uses official esp-iot-bridge component
- **WiFi Station Mode**: Connects to existing WiFi networks
- **DHCP Server**: Provides IP addresses to connected devices
- **NAT Support**: Network address translation for multiple devices
- **High Performance**: Optimized for throughput and low latency

### Arduino Example
- **Simple Protocol**: Easy-to-understand command-based SPI protocol
- **Basic WiFi**: Standard WiFi connectivity
- **Status Monitoring**: Real-time connection status
- **Error Handling**: Comprehensive error reporting
- **Easy Integration**: Simple to integrate with existing projects

## Quick Start

### ESP-IDF Example

1. **Setup Environment**:
   ```bash
   # Set ESP-IDF environment
   . $HOME/esp/esp-idf/export.sh
   ```

2. **Configure WiFi**:
   Edit `main.c` and update WiFi credentials:
   ```c
   #define EXAMPLE_ESP_WIFI_SSID     "YOUR_WIFI_SSID"
   #define EXAMPLE_ESP_WIFI_PASS     "YOUR_WIFI_PASSWORD"
   ```

3. **Build and Flash**:
   ```bash
   cd spiNetwork/esp-idf_example
   idf.py set-target esp32
   idf.py menuconfig  # Optional: configure settings
   idf.py build flash monitor
   ```

4. **Verify Operation**:
   - ESP32 will connect to WiFi
   - SPI interface will be available at 192.168.4.1
   - DHCP server will assign IPs to connected devices

### Arduino Example

1. **Install Arduino IDE**:
   - Install Arduino IDE 2.0+
   - Add ESP32 board support via Board Manager

2. **Configure Sketch**:
   - Open `spi_network_bridge.ino`
   - Update WiFi credentials:
   ```cpp
   const char* ssid = "YOUR_WIFI_SSID";
   const char* password = "YOUR_WIFI_PASSWORD";
   ```

3. **Upload and Test**:
   - Select ESP32 Dev Module board
   - Upload sketch to ESP32
   - Monitor Serial output for status

## Protocol Details

### ESP-IDF Implementation
Uses the official ESP-IoT-Bridge SPI protocol with:
- Standard Ethernet frame format
- Automatic flow control
- High-speed data transfer
- Native network stack integration

### Arduino Implementation
Custom SPI protocol with commands:
- `0x01` - Get IP address
- `0x02` - Send data
- `0x03` - Receive data
- `0x04` - Connect
- `0x05` - Disconnect
- `0x06` - Get status

## Performance Characteristics

| Metric | ESP-IDF | Arduino |
|--------|---------|---------|
| Throughput | ~10 Mbps | ~1 Mbps |
| Latency | <10ms | ~50ms |
| CPU Usage | Low | Medium |
| Memory Usage | ~50KB | ~30KB |
| Features | Full bridge | Basic bridge |

## Use Cases

### Industrial Applications
- **IoT Gateways**: Connect sensors without WiFi to internet
- **Industrial Controllers**: Add network capability to PLCs
- **Data Loggers**: Enable remote data collection

### Consumer Applications
- **Smart Home**: Bridge legacy devices to smart home networks
- **Retro Computing**: Add internet to vintage computers
- **DIY Projects**: Simple network connectivity for hobby projects

### Automotive Applications
- **Vehicle Telematics**: Connect vehicle ECUs to cloud
- **Fleet Management**: Network connectivity for tracking systems
- **Infotainment**: Bridge multimedia systems to internet

## Troubleshooting

### Common Issues

1. **SPI Communication Failure**:
   - Check pin connections
   - Verify SPI clock speed (try lower speeds first)
   - Ensure proper handshaking timing

2. **WiFi Connection Issues**:
   - Verify SSID and password
   - Check 2.4GHz network availability
   - Monitor signal strength

3. **Performance Issues**:
   - Reduce SPI clock speed if errors occur
   - Check for memory constraints
   - Monitor CPU usage

### Debug Tools

- **Serial Monitor**: Monitor ESP32 status messages
- **Logic Analyzer**: Analyze SPI timing and protocol
- **Network Tools**: Ping, traceroute to verify connectivity
- **WiFi Analyzer**: Check signal strength and interference

## Advanced Configuration

### ESP-IDF Menuconfig Options
- `Bridge Configuration` - Enable/disable interfaces
- `SPI Configuration` - Adjust SPI parameters
- `WiFi Configuration` - Optimize WiFi settings
- `LwIP Configuration` - Tune network stack

### Arduino Customization
- Modify SPI pins for different hardware
- Adjust protocol for specific requirements
- Add custom commands for specialized applications

## Security Considerations

- **SPI Security**: Physical access to SPI interface
- **WiFi Security**: Use WPA2/WPA3 networks
- **Network Security**: Implement firewall rules
- **Data Protection**: Encrypt sensitive data

## Support and Contributing

For issues and contributions:
1. Check existing documentation
2. Review hardware connections
3. Test with minimal configuration
4. Provide detailed bug reports

## License

This example code follows the same license as the ESP-IoT-Bridge component (Apache 2.0).

---

**Note**: These examples are designed for ESP32-WROOM-1 but can be adapted for other ESP32 variants with appropriate pin configuration changes.
