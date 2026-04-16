#include <Arduino.h>
#include <SoftwareSerial.h>

// AVR Arduino PPP 串口示例
// 该示例使用 SoftwareSerial 或硬件 Serial1 连接到路由器 PPPoS 服务。

SoftwareSerial pppSerial(10, 11); // RX, TX - 按实际接线调整
const uint32_t PPP_BAUD = 115200;
bool pppConnected = false;

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }

  Serial.println("AVR PPP UART example");
  pppSerial.begin(PPP_BAUD);
  Serial.println("PPP serial started");

  // TODO: 初始化 PPP 协议栈并绑定 pppSerial 作为底层传输
}

void loop() {
  if (pppSerial.available()) {
    uint8_t buf[128];
    size_t len = pppSerial.readBytes(buf, sizeof(buf));
    if (len > 0) {
      // TODO: 将串口接收数据输入 PPP 协议栈
      // pppInput(buf, len);
    }
  }

  // TODO: 当 PPP 协议栈向下层发送数据时，调用 pppSerial.write()

  if (pppConnected) {
    Serial.println("PPP 链路已建立，可执行网络测试");
    pppConnected = false;
  }

  delay(50);
}

void onPppConnected() {
  Serial.println("PPP connected");
  pppConnected = true;
}
