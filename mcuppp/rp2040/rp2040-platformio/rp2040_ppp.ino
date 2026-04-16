#include <Arduino.h>

// RP2040 Arduino 示例：使用 Serial1 与路由器 PPPoS 串口对接。
// 该示例提供串口传输层骨架，使用模拟 PPP 连接逻辑，确保可编译。

auto& pppSerial = Serial1;
const int PPP_RX_PIN = 4;   // RP2040 RX 引脚
const int PPP_TX_PIN = 5;   // RP2040 TX 引脚
const int PPP_BAUD = 115200;

bool pppConnected = false;
unsigned long lastHandshakeMillis = 0;
const unsigned long HANDSHAKE_INTERVAL = 3000;

void pppInit() {
  Serial.println("Initializing PPP transport...");
  pppSerial.setPinout(PPP_TX_PIN, PPP_RX_PIN);
  pppSerial.begin(PPP_BAUD);
  Serial.print("PPP UART started on RX=");
  Serial.print(PPP_RX_PIN);
  Serial.print(" TX=");
  Serial.print(PPP_TX_PIN);
  Serial.print(" @");
  Serial.println(PPP_BAUD);
  lastHandshakeMillis = millis();
}

void pppSendData(const uint8_t* data, size_t length) {
  if (length == 0) {
    return;
  }
  pppSerial.write(data, length);
  pppSerial.flush();
}

void pppInput(const uint8_t* data, size_t length) {
  // 这里仅演示接收数据的骨架。实际应用时，将数据交给 PPP 协议栈处理。
  Serial.print("Received PPP data: ");
  for (size_t i = 0; i < length; i++) {
    Serial.print(data[i], HEX);
    Serial.print(' ');
  }
  Serial.println();
}

void onPppConnected() {
  Serial.println("PPP link is up");
  pppConnected = true;
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }

  Serial.println("RP2040 PPP UART example");
  pppInit();
}

void loop() {
  if (pppSerial.available()) {
    uint8_t buffer[256];
    size_t len = pppSerial.readBytes(buffer, sizeof(buffer));
    if (len > 0) {
      pppInput(buffer, len);
    }
  }

  unsigned long now = millis();
  if (!pppConnected && now - lastHandshakeMillis >= HANDSHAKE_INTERVAL) {
    Serial.println("Simulating PPP handshake request...");
    const char handshake[] = "PPP_HELLO";
    pppSendData(reinterpret_cast<const uint8_t*>(handshake), strlen(handshake));
    lastHandshakeMillis = now;
  }

  if (!pppConnected && pppSerial.available() > 0) {
    // 演示：收到任意数据即可认为连接建立。
    onPppConnected();
  }

  if (pppConnected) {
    Serial.println("PPP 已建立，等待应用层处理...");
    pppConnected = false;
  }

  delay(10);
}
