#include <Arduino.h>
#include <driver/i2s.h>

#define I2S_SAMPLE_RATE 16000
#define I2S_READ_LEN    1024

// 引脚配置
#define I2S_BCLK  16  // SCK
#define I2S_LRCL  17  // WS
#define I2S_DOUT  18  // 麦克风输出 -> MCU 输入
#define I2S_DIN   15  // 扬声器输出 -< MCU 输出

#define BUFFER_LEN 256

int32_t buffer_in[BUFFER_LEN];
int32_t buffer_out[BUFFER_LEN];

//麦克风输入
void setupI2S() {
  // 配置 I2S
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX),
    .sample_rate = I2S_SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT, // 单声道
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = BUFFER_LEN,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCLK,
    .ws_io_num = I2S_LRCL,
    .data_out_num = I2S_DIN, 
    .data_in_num = I2S_DOUT
  };

  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
}



void setup() {
  Serial.begin(115200);
  setupI2S();
  Serial.println("I2S full-duplex demo");
}

void loop() {
  int32_t buffer[BUFFER_LEN];
  size_t bytes_read = 0, bytes_written = 0;

  // 读取 I2S 缓冲区
  i2s_read(I2S_NUM_0, (void*)buffer, sizeof(buffer), &bytes_read, portMAX_DELAY);
  //处理数据：
  // 简单直通处理: 麦克风采集的数据直接发给耳机
  // for (int i = 0; i < BUFFER_LEN; i++) {
  //   // 简单缩小音量，避免过载
  //   buffer[i] = buffer[i] >> 8;
  // }
  // 写入 I2S 缓冲
  i2s_write(I2S_NUM_0, (void*)buffer, sizeof(buffer), &bytes_written, portMAX_DELAY);

  // 只打印前几个样本（调试用）
  int samples = bytes_read / 4; // 32bit = 4 bytes
  for (int i = 0; i < 8 && i < samples; i++) {
    Serial.println(buffer[i]);
  }

}
