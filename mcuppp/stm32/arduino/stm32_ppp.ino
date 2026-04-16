#include <Arduino.h>

// STM32 Arduino PPP UART 示例
// 该示例使用 Serial1 或 Serial2 与路由器 PPPoS 服务通信，
// 仅提供串口传输层骨架。

HardwareSerial &pppSerial = Serial1;
const uint32_t PPP_BAUD = 115200;
const int PPP_RX_PIN = PA10; // 示例，实际按开发板引脚调整
const int PPP_TX_PIN = PA9;

bool pppConnected = false;

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }

  Serial.println("STM32 Arduino PPP UART example");

  pppSerial.begin(PPP_BAUD, SERIAL_8N1, PPP_RX_PIN, PPP_TX_PIN);
  Serial.printf("PPP UART started on RX=%d TX=%d @%u\n", PPP_RX_PIN, PPP_TX_PIN, PPP_BAUD);

  // TODO: 在此初始化 PPP 协议栈，并绑定 pppSerial 作为底层传输
}

void loop() {
  if (pppSerial.available()) {
    uint8_t buf[256];
    size_t len = pppSerial.readBytes(buf, sizeof(buf));
    if (len > 0) {
      // TODO: 将 buf 数据交给 PPP 协议栈处理
      // pppInput(buf, len);
    }
  }

  // TODO: 当 PPP 协议栈有数据要发送时，调用 pppSerial.write()
  // if (pppHasData) pppSerial.write(pppTxBuf, pppTxLen);

  if (pppConnected) {
    Serial.println("PPP 已连接，网络测试开始");
    // TODO: 使用网络栈执行 ping 或 HTTP GET
    pppConnected = false;
  }

  delay(20);
}

void onPppConnected() {
  Serial.println("PPP link is up");
  pppConnected = true;
}
