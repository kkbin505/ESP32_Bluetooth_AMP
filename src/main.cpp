#include <Arduino.h>
#include <driver/i2s.h>
#include <lms_filter.h>
#include <Adafruit_NeoPixel.h>

#define NUMPIXELS 1 // 只有1颗LED

#define I2S_SAMPLE_RATE 44100

#define LED_PIN 8
#define LED_COUNT 1 // 只有一个RGB灯珠

// 引脚配置
#define I2S_0_BCLK 16 // SCK
#define I2S_0_LRCL 17 // WS
#define I2S_0_DOUT 10 // 扬声器输出 -< MCU 输出
#define I2S_0_DIN 18  // 麦克风输出 -> MCU 输入

#define I2S_1_BCLK 37 // SCK
#define I2S_1_LRCL 36 // WS
#define I2S_1_DIN 35  // 麦克风输出 -> MCU 输入

#define BUFFER_LEN 256

#define LMS_LENGTH 64
#define LMS_STEP 1e-7

int16_t buffer_mic0[BUFFER_LEN * 2];
int16_t buffer_mic1[BUFFER_LEN * 2];
int16_t buffer_out[BUFFER_LEN * 2];
// int16_t i2s_buffer[BUFFER_LEN * 2]; // 立体声交错: L R L R ...

// RGB LED
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

// ANC
bool anc_enabled = false;
unsigned long last_toggle_time = 0;
const unsigned long toggle_interval = 5000; // 3秒

const float fs = 44100.0f;
const float fc = 380.0f;
float alpha = 1.0f - expf(-2.0f * M_PI * fc / fs);
static float lp = 0.0f;     // 低通状态
const float ancGain = 0.7f; // 反相信号增益，避免啸叫
// 麦克风输入
void setupI2S0()
{
  // 配置 I2S
  i2s_config_t i2s_config = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX),
      .sample_rate = I2S_SAMPLE_RATE,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
      .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT, // 立体声
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 4,
      .dma_buf_len = BUFFER_LEN,
      .use_apll = false,
      .tx_desc_auto_clear = false,
      .fixed_mclk = 0};

  i2s_pin_config_t pin_config = {
      .bck_io_num = I2S_0_BCLK,
      .ws_io_num = I2S_0_LRCL,
      .data_out_num = I2S_0_DOUT,
      .data_in_num = I2S_0_DIN};

  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
}

// 第二麦克风I2S配置
void setupI2S1()
{
  i2s_config_t i2s_config1 = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
      .sample_rate = I2S_SAMPLE_RATE,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
      .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = 0,
      .dma_buf_count = 4,
      .dma_buf_len = BUFFER_LEN,
      .use_apll = false};

  i2s_pin_config_t pin_config1 = {
      .bck_io_num = I2S_1_BCLK,
      .ws_io_num = I2S_1_LRCL,
      .data_out_num = I2S_PIN_NO_CHANGE,
      .data_in_num = I2S_1_DIN};

  i2s_driver_install(I2S_NUM_1, &i2s_config1, 0, NULL);
  i2s_set_pin(I2S_NUM_1, &pin_config1);
}

void setup()
{
  Serial.begin(115200);
  strip.begin();
  strip.show();
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  setupI2S0();
  setupI2S1();
  lms_init(LMS_LENGTH, LMS_STEP); // 滤波器长度64, 步长μ
  Serial.println("I2S full-duplex ANC demo");
}

void loop()
{
  unsigned long now = millis();
  if (now - last_toggle_time >= toggle_interval)
  {
    anc_enabled = !anc_enabled; // 切换开关
    last_toggle_time = now;
    anc_enabled ? strip.setPixelColor(0, strip.Color(0, 0, 25)) : strip.setPixelColor(0, strip.Color(0, 0, 0));
    // Serial.print("ANC ");
    // Serial.println(anc_enabled ? "ON" : "OFF");
  }
  strip.show();
  size_t bytes_read0 = 0, bytes_written = 0, bytes_read1 = 0;

  // 读取 I2S 缓冲区
  i2s_read(I2S_NUM_0, buffer_mic0, sizeof(buffer_mic0), &bytes_read0, portMAX_DELAY);
  i2s_read(I2S_NUM_1, buffer_mic1, sizeof(buffer_mic1), &bytes_read1, portMAX_DELAY);

  // 处理数据：
  //  样本对数
  int stereo_samples0 = bytes_read0 / sizeof(int16_t);
  int stereo_samples1 = bytes_read1 / sizeof(int16_t);
  int samples = min(stereo_samples0, stereo_samples1) / 2; // 单声道样本数

  // for (int i = 0; i < samples; i++) {
  //   int16_t left = i2s_buffer[2*i];  // 左声道有效
  //   i2s_buffer[2*i + 1] = left;      // 复制给右声道
  // }
  // float lp_prev = 0.0f; // 滤波器上一个输出

  for (int i = 0; i < samples; i++)
  {
    // 取左声道样本并归一化
    float x = (float)buffer_mic0[2 * i] / 32768.0f; // 参考信号
    float d = (float)buffer_mic1[2 * i] / 32768.0f; // 目标信号

    // LMS 计算误差 (自适应输出)
    float e = lms_calculate(x, d);

    // 误差回放到耳机
    int16_t out = anc_enabled ? e * 32768.0f * ancGain : 0;
    buffer_out[2 * i] = out;
    buffer_out[2 * i + 1] = out;

    if (i % 100 == 0)
    { // 减少串口打印频率
      Serial.print(buffer_mic0[2 * i]);
      Serial.print(",");
      Serial.print(buffer_mic1[2 * i]);
      Serial.print(",");
      Serial.println(out);
    }
  }

  // 写入 I2S 缓冲
  i2s_write(I2S_NUM_0, buffer_out, bytes_read0, &bytes_written, portMAX_DELAY);

  // 只打印前几个样本（调试用）
  // for (int i = 0; i < 8 && i < samples; i++) {
  //   Serial.print(buffer_out[i]);
  //   Serial.print(",");
  //   Serial.println(buffer_mic1[i]);
  // }
}
