#include <WiFi.h>
#include <SPI.h>
#include <Ethernet.h>

// SPI Network Bridge Configuration
#define SPI_CS_PIN    5   // Chip Select pin
#define SPI_CLK_PIN   18  // Clock pin  
#define SPI_MISO_PIN  19  // MISO pin
#define SPI_MOSI_PIN  23  // MOSI pin
#define HANDSHAKE_PIN 4   // Handshake pin for communication
#define DATA_READY_PIN 2  // Data ready pin

// WiFi Configuration
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// Network Configuration for SPI interface
IPAddress spi_ip(192, 168, 4, 1);
IPAddress spi_gateway(192, 168, 4, 1);
IPAddress spi_subnet(255, 255, 255, 0);

// SPI Command definitions
#define CMD_GET_IP       0x01
#define CMD_SEND_DATA    0x02
#define CMD_RECV_DATA    0x03
#define CMD_CONNECT      0x04
#define CMD_DISCONNECT   0x05
#define CMD_STATUS       0x06

struct SPIDataPacket {
  uint8_t command;
  uint8_t length;
  uint8_t data[256];
};

SPIDataPacket tx_packet;
SPIDataPacket rx_packet;

bool wifi_connected = false;
bool client_connected = false;

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }
  
  Serial.println("ESP32 SPI Network Bridge - Arduino Example");
  Serial.println("=========================================");
  
  // Initialize SPI pins
  pinMode(SPI_CS_PIN, OUTPUT);
  pinMode(HANDSHAKE_PIN, INPUT);
  pinMode(DATA_READY_PIN, OUTPUT);
  digitalWrite(SPI_CS_PIN, HIGH);
  digitalWrite(DATA_READY_PIN, LOW);
  
  // Initialize SPI as slave
  SPI.begin(SPI_CLK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN, SPI_CS_PIN);
  
  // Initialize WiFi
  Serial.println("Connecting to WiFi...");
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    wifi_connected = true;
    Serial.println("\nWiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nFailed to connect to WiFi");
  }
  
  // Initialize SPI communication
  setupSPICommunication();
  
  Serial.println("SPI Network Bridge ready!");
  Serial.println("Waiting for SPI communications...");
}

void setupSPICommunication() {
  // Configure SPI for slave mode
  SPI.setClockDivider(SPI_CLOCK_DIV4);
  SPI.setDataMode(SPI_MODE0);
  SPI.setBitOrder(MSBFIRST);
  
  // Signal ready to host
  digitalWrite(DATA_READY_PIN, HIGH);
}

void loop() {
  // Check for SPI communication requests
  if (digitalRead(HANDSHAKE_PIN) == HIGH) {
    handleSPIRequest();
  }
  
  // Handle WiFi status
  if (wifi_connected && WiFi.status() != WL_CONNECTED) {
    wifi_connected = false;
    Serial.println("WiFi disconnected!");
    // Try to reconnect
    WiFi.reconnect();
  } else if (!wifi_connected && WiFi.status() == WL_CONNECTED) {
    wifi_connected = true;
    Serial.println("WiFi reconnected!");
  }
  
  delay(10);
}

void handleSPIRequest() {
  digitalWrite(DATA_READY_PIN, LOW);
  
  // Wait for CS to go low (active)
  while (digitalRead(SPI_CS_PIN) == HIGH) {
    delayMicroseconds(1);
  }
  
  // Read command from SPI
  rx_packet.command = SPI.transfer(0x00);
  rx_packet.length = SPI.transfer(0x00);
  
  // Read data based on command
  switch (rx_packet.command) {
    case CMD_GET_IP:
      handleGetIP();
      break;
      
    case CMD_SEND_DATA:
      handleSendData();
      break;
      
    case CMD_RECV_DATA:
      handleRecvData();
      break;
      
    case CMD_CONNECT:
      handleConnect();
      break;
      
    case CMD_DISCONNECT:
      handleDisconnect();
      break;
      
    case CMD_STATUS:
      handleStatus();
      break;
      
    default:
      // Unknown command
      tx_packet.command = 0xFF;
      tx_packet.length = 1;
      tx_packet.data[0] = 0x01; // Error code
      break;
  }
  
  // Send response
  sendSPIResponse();
  
  // Wait for CS to go high
  while (digitalRead(SPI_CS_PIN) == LOW) {
    delayMicroseconds(1);
  }
  
  digitalWrite(DATA_READY_PIN, HIGH);
}

void handleGetIP() {
  tx_packet.command = CMD_GET_IP;
  tx_packet.length = 4;
  
  if (wifi_connected) {
    IPAddress ip = WiFi.localIP();
    tx_packet.data[0] = ip[0];
    tx_packet.data[1] = ip[1];
    tx_packet.data[2] = ip[2];
    tx_packet.data[3] = ip[3];
  } else {
    // Return error IP
    tx_packet.data[0] = 0;
    tx_packet.data[1] = 0;
    tx_packet.data[2] = 0;
    tx_packet.data[3] = 0;
  }
}

void handleSendData() {
  // Read data length
  uint8_t data_len = SPI.transfer(0x00);
  tx_packet.command = CMD_SEND_DATA;
  tx_packet.length = 1;
  
  if (data_len > 0 && data_len <= 256) {
    // Read actual data
    for (int i = 0; i < data_len; i++) {
      rx_packet.data[i] = SPI.transfer(0x00);
    }
    
    // Here you would implement actual network data forwarding
    // For this example, we just acknowledge receipt
    tx_packet.data[0] = 0x00; // Success
    
    Serial.print("Received data via SPI: ");
    Serial.println(data_len);
  } else {
    tx_packet.data[0] = 0x02; // Invalid length error
  }
}

void handleRecvData() {
  tx_packet.command = CMD_RECV_DATA;
  tx_packet.length = 1;
  
  // For this example, we just send a status
  // In a real implementation, you would buffer network data
  tx_packet.data[0] = 0x00; // No data available
}

void handleConnect() {
  tx_packet.command = CMD_CONNECT;
  tx_packet.length = 1;
  
  if (wifi_connected) {
    client_connected = true;
    tx_packet.data[0] = 0x00; // Success
    Serial.println("Client connected via SPI");
  } else {
    tx_packet.data[0] = 0x03; // WiFi not connected
  }
}

void handleDisconnect() {
  tx_packet.command = CMD_DISCONNECT;
  tx_packet.length = 1;
  client_connected = false;
  tx_packet.data[0] = 0x00; // Success
  Serial.println("Client disconnected via SPI");
}

void handleStatus() {
  tx_packet.command = CMD_STATUS;
  tx_packet.length = 3;
  
  // Status flags
  tx_packet.data[0] = wifi_connected ? 0x01 : 0x00;
  tx_packet.data[1] = client_connected ? 0x01 : 0x00;
  tx_packet.data[2] = WiFi.status(); // WiFi status code
}

void sendSPIResponse() {
  // Send response header
  SPI.transfer(tx_packet.command);
  SPI.transfer(tx_packet.length);
  
  // Send response data
  for (int i = 0; i < tx_packet.length; i++) {
    SPI.transfer(tx_packet.data[i]);
  }
}

void printWiFiStatus() {
  Serial.println("WiFi Status:");
  Serial.print("  Connected: ");
  Serial.println(wifi_connected ? "Yes" : "No");
  
  if (wifi_connected) {
    Serial.print("  IP Address: ");
    Serial.println(WiFi.localIP());
    Serial.print("  Gateway: ");
    Serial.println(WiFi.gatewayIP());
    Serial.print("  Subnet Mask: ");
    Serial.println(WiFi.subnetMask());
    Serial.print("  RSSI: ");
    Serial.println(WiFi.RSSI());
  }
  
  Serial.print("  Client Connected: ");
  Serial.println(client_connected ? "Yes" : "No");
  Serial.println();
}
