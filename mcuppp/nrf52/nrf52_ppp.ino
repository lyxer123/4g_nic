#include <Arduino.h>
#include <HardwareSerial.h>

// nRF52 Arduino PPP UART 示例
// 使用 Serial1 或 Serial2 作为 PPP 底层串口。

HardwareSerial &pppSerial = Serial1;
const uint32_t PPP_BAUD = 115200;
bool pppConnected = false;

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }

  Serial.println("nRF52 PPP UART example");
  pppSerial.begin(PPP_BAUD);
  Serial.println("PPP serial started");

  // TODO: 初始化 PPP 协议栈并绑定 pppSerial
}

void loop() {
  if (pppSerial.available()) {
    uint8_t buf[256];
    size_t len = pppSerial.readBytes(buf, sizeof(buf));
    if (len > 0) {
      // TODO: 将 buf 数据交给 PPP 协议栈
    }
  }

  // TODO: 当 PPP 底层需要发送时，使用 pppSerial.write()

  if (pppConnected) {
    Serial.println("PPP connected, run network test");
    pppConnected = false;
  }

  delay(20);
}

void onPppConnected() {
  Serial.println("PPP link established");
  pppConnected = true;
}
