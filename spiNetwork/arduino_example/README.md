# ESP32 SPI Network Bridge - Arduino Example

This example demonstrates how to use an ESP32-WROOM-1 as a SPI network bridge to provide internet access to other MCUs through the SPI interface.

## Hardware Requirements

- ESP32-WROOM-1 development board
- Host MCU (Arduino, STM32, Raspberry Pi, etc.)
- Jumper wires for SPI connections

## Pin Connections

### ESP32 to Host MCU

| ESP32 Pin | Function | Host MCU Pin |
|-----------|----------|--------------|
| GPIO18    | SCLK     | SPI Clock    |
| GPIO19    | MISO     | SPI MISO     |
| GPIO23    | MOSI     | SPI MOSI     |
| GPIO5     | CS       | Chip Select  |
| GPIO4     | Handshake| Handshake    |
| GPIO2     | Data Ready| Data Ready   |

## Configuration

1. Update the WiFi credentials in the sketch:
```cpp
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
```

2. Configure the SPI pins if needed:
```cpp
#define SPI_CS_PIN    5   // Chip Select pin
#define SPI_CLK_PIN   18  // Clock pin  
#define SPI_MISO_PIN  19  // MISO pin
#define SPI_MOSI_PIN  23  // MOSI pin
#define HANDSHAKE_PIN 4   // Handshake pin for communication
#define DATA_READY_PIN 2  // Data ready pin
```

## SPI Protocol

The SPI communication uses a simple command-based protocol:

### Command Structure
- Byte 0: Command
- Byte 1: Data Length
- Bytes 2-N: Data (if applicable)

### Available Commands

| Command | Value | Description |
|---------|-------|-------------|
| CMD_GET_IP | 0x01 | Get WiFi IP address |
| CMD_SEND_DATA | 0x02 | Send data to network |
| CMD_RECV_DATA | 0x03 | Receive data from network |
| CMD_CONNECT | 0x04 | Establish connection |
| CMD_DISCONNECT | 0x05 | Disconnect |
| CMD_STATUS | 0x06 | Get status information |

### Response Structure
- Byte 0: Command (echo of request)
- Byte 1: Data Length
- Bytes 2-N: Response Data

## Usage

1. Connect the ESP32 to your host MCU using the pin connections above
2. Upload this sketch to the ESP32
3. The ESP32 will automatically connect to WiFi and wait for SPI requests
4. Your host MCU can now communicate with the ESP32 via SPI to access the network

## Host MCU Example Code

```cpp
// Example for Arduino host
#include <SPI.h>

#define CS_PIN    10
#define HANDSHAKE_PIN 9
#define DATA_READY_PIN 8

void setup() {
  Serial.begin(115200);
  pinMode(CS_PIN, OUTPUT);
  pinMode(HANDSHAKE_PIN, OUTPUT);
  pinMode(DATA_READY_PIN, INPUT);
  digitalWrite(CS_PIN, HIGH);
  digitalWrite(HANDSHAKE_PIN, LOW);
  
  SPI.begin();
}

void getESP32IP() {
  digitalWrite(HANDSHAKE_PIN, HIGH);
  delay(1);
  
  digitalWrite(CS_PIN, LOW);
  SPI.transfer(0x01); // CMD_GET_IP
  SPI.transfer(0x00); // No data
  uint8_t cmd = SPI.transfer(0x00);
  uint8_t len = SPI.transfer(0x00);
  
  if (cmd == 0x01 && len == 4) {
    uint32_t ip = 0;
    for (int i = 0; i < 4; i++) {
      ip = (ip << 8) | SPI.transfer(0x00);
    }
    Serial.print("ESP32 IP: ");
    Serial.println(ip);
  }
  
  digitalWrite(CS_PIN, HIGH);
  digitalWrite(HANDSHAKE_PIN, LOW);
}

void loop() {
  getESP32IP();
  delay(5000);
}
```

## Features

- **WiFi Connectivity**: Connects to 2.4GHz WiFi networks
- **SPI Interface**: High-speed SPI communication with host MCU
- **Command Protocol**: Simple command-based SPI protocol
- **Status Monitoring**: Real-time status reporting
- **Error Handling**: Comprehensive error reporting
- **Low Latency**: Efficient data transfer with minimal latency

## Limitations

- Single WiFi network connection
- Basic SPI protocol (no advanced features like QoS)
- No built-in security for SPI communication
- Limited to 2.4GHz WiFi networks

## Troubleshooting

1. **SPI Communication Issues**:
   - Check pin connections
   - Verify SPI clock speed
   - Ensure proper handshaking

2. **WiFi Connection Issues**:
   - Verify SSID and password
   - Check WiFi signal strength
   - Ensure 2.4GHz network

3. **Performance Issues**:
   - Reduce SPI clock speed if errors occur
   - Add delays between operations
   - Check for memory issues

## Advanced Usage

For more advanced features like:
- Multiple network interfaces
- Advanced SPI protocols
- Security features
- Performance optimization

See the ESP-IDF example for more comprehensive implementation.
