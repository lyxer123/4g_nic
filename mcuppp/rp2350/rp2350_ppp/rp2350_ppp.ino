#include <Arduino.h>
#include <HardwareSerial.h>

// RP2350 Arduino 示例：使用 Serial1 与路由器 PPPoS 串口对接。

auto &pppSerial = Serial1;
const int PPP_RX_PIN = 12;  // 根据开发板实际引脚调整
const int PPP_TX_PIN = 13;
const int PPP_BAUD = 115200;

bool pppConnected = false;

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }

  Serial.println("RP2350 PPP UART example");
  pppSerial.setPinout(PPP_TX_PIN, PPP_RX_PIN);
  pppSerial.begin(PPP_BAUD, SERIAL_8N1);
  Serial.printf("PPP UART started on RX=%d TX=%d @%d\n", PPP_RX_PIN, PPP_TX_PIN, PPP_BAUD);

  // TODO: 初始化 PPP 协议栈，并将 pppSerial 作为底层发送/接收接口。
}

void loop() {
  if (pppSerial.available()) {
    uint8_t buffer[256];
    size_t len = pppSerial.readBytes(buffer, sizeof(buffer));
    if (len > 0) {
      // TODO: 交给 PPP 协议栈处理数据
      // pppInput(buffer, len);
    }
  }

  // 如果 PPP 协议栈有发送数据，请调用 pppSerial.write()

  if (pppConnected) {
    Serial.println("PPP 已连接，开始网络测试");
    // TODO: 使用网络栈发起 ping 或 HTTP 请求
    pppConnected = false;
  }

  delay(20);
}

void onPppConnected() {
  Serial.println("PPP 链路已建立");
  pppConnected = true;
}
