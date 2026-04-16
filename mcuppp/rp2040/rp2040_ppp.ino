#include <Arduino.h>
#include <HardwareSerial.h>

// RP2040 Arduino 示例：使用 Serial1 与路由器 PPPoS 串口对接。
// 该示例提供串口传输层骨架，需接入 MCU 端 PPP 协议栈。

HardwareSerial pppSerial(1);
const int PPP_RX_PIN = 4;   // RP2040 RX 引脚
const int PPP_TX_PIN = 5;   // RP2040 TX 引脚
const int PPP_BAUD = 115200;

bool pppConnected = false;

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }

  Serial.println("RP2040 PPP UART example");
  pppSerial.begin(PPP_BAUD, SERIAL_8N1, PPP_RX_PIN, PPP_TX_PIN);
  Serial.printf("PPP UART started on RX=%d TX=%d @%d\n", PPP_RX_PIN, PPP_TX_PIN, PPP_BAUD);

  // TODO: 初始化 PPP 协议栈，绑定 pppSerial 作为底层传输接口。
}

void loop() {
  if (pppSerial.available()) {
    uint8_t buffer[256];
    size_t len = pppSerial.readBytes(buffer, sizeof(buffer));
    if (len > 0) {
      // TODO: 把 buffer 数据交给 PPP 协议栈处理
      // pppInput(buffer, len);
    }
  }

  // TODO: 当 PPP 协议栈有数据要发送时，调用 pppSerial.write()
  // if (pppNeedsSend) pppSerial.write(pppSendBuffer, pppSendLen);

  if (pppConnected) {
    // PPP 已建立，执行网络测试
    // TODO: 使用 MCU 端网络栈发起 ping 或 HTTP 请求
    // 例如：httpGet("http://www.baidu.com");
    pppConnected = false;
  }

  delay(10);
}

// 这里是一个示意函数，代表外部 PPP 协议栈在链路建立后调用。
void onPppConnected() {
  Serial.println("PPP link is up");
  pppConnected = true;
}
