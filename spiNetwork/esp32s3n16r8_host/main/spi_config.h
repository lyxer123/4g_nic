#ifndef SPI_CONFIG_H
#define SPI_CONFIG_H

// SPI引脚配置（与配置界面保持一致）
#define SPI_MOSI_PIN        16      // 主机输出
#define SPI_MISO_PIN        17      // 主机输入
#define SPI_CLK_PIN         18      // 时钟输出
#define SPI_CS_PIN          8       // 片选输出
#define SPI_HANDSHAKE_PIN   3       // 从机握手信号 (输入)
#define SPI_DATA_READY_PIN  46      // 从机数据就绪 (输入)

// SPI配置参数
#define SPI_CLK_MHZ         30      // 30MHz时钟
/* 与路由器侧 esp-iot-bridge spi_slave_api.c 中 SPI_BUFFER_SIZE(1600) 一致 */
#define SPI_BUFFER_SIZE     1600
#define SPI_QUEUE_SIZE      20      // ESP到Host队列大小

// 其他公共配置
#define SPI_MAX_RETRY       3       // 最大重试次数
#define SPI_BUFFER_POOL_SIZE 8      // 内存池缓冲区数量

#endif // SPI_CONFIG_H
