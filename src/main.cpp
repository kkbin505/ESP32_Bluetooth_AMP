#include <Arduino.h>
#include <driver/i2s.h>
#include <lms_filter.h>
#include <Adafruit_NeoPixel.h>

#define NUMPIXELS 1  // 只有1颗LED

#define I2S_SAMPLE_RATE 44100

#define LED_PIN 8
#define LED_COUNT 1  // 只有一个RGB灯珠

// 引脚配置
#define I2S_BCLK 16 // SCK
#define I2S_LRCL 17 // WS
#define I2S_DOUT 10 // 扬声器输出 -< MCU 输出
#define I2S_DIN 18  // 麦克风输出 -> MCU 输入

#define BUFFER_LEN 256

int32_t buffer_in[BUFFER_LEN];
int32_t buffer_out[BUFFER_LEN * 2];
int16_t i2s_buffer[BUFFER_LEN * 2]; // 立体声交错: L R L R ...

// RGB LED
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

// ANC
bool anc_enabled = false;
unsigned long last_toggle_time = 0;
const unsigned long toggle_interval = 3000; // 3秒

const float fs = 44100.0f;
const float fc = 380.0f;
float alpha = 1.0f - expf(-2.0f * M_PI * fc / fs);  
static float lp = 0.0f;            // 低通状态
const float ancGain = 0.7f;        // 反相信号增益，避免啸叫
// 麦克风输入
void setupI2S()
{
  // 配置 I2S
  i2s_config_t i2s_config = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX),
      .sample_rate = I2S_SAMPLE_RATE,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
      .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT, // 单声道
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 4,
      .dma_buf_len = BUFFER_LEN,
      .use_apll = false,
      .tx_desc_auto_clear = false,
      .fixed_mclk = 0};

  i2s_pin_config_t pin_config = {
      .bck_io_num = I2S_BCLK,
      .ws_io_num = I2S_LRCL,
      .data_out_num = I2S_DOUT,
      .data_in_num = I2S_DIN};

  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
}

void setup()
{
  Serial.begin(115200);
  strip.begin();
  strip.show();
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  setupI2S();
  lms_init(64, 0.000001f);  // 滤波器长度64, 步长μ
  Serial.println("I2S full-duplex demo");
}

void loop()
{
  unsigned long now = millis();
  if (now - last_toggle_time >= toggle_interval)
  {
    anc_enabled = !anc_enabled; // 切换开关
    last_toggle_time = now;
    anc_enabled? strip.setPixelColor(0, strip.Color(0, 0, 25)):strip.setPixelColor(0, strip.Color(0, 0, 0));
    // Serial.print("ANC ");
    // Serial.println(anc_enabled ? "ON" : "OFF");
    
  }
  strip.show();
  size_t bytes_read = 0, bytes_written = 0;

  // 读取 I2S 缓冲区
  i2s_read(I2S_NUM_0, i2s_buffer, sizeof(i2s_buffer), &bytes_read, portMAX_DELAY);

  // 处理数据：
  //  样本对数
  int samples = bytes_read / (sizeof(int16_t) * 2);

  // for (int i = 0; i < samples; i++) {
  //   int16_t left = i2s_buffer[2*i];  // 左声道有效
  //   i2s_buffer[2*i + 1] = left;      // 复制给右声道
  // }
  float lp_prev = 0.0f; // 滤波器上一个输出

  for (int i = 0; i < samples; i++)
  {
    int16_t sample = i2s_buffer[2 * i]; // 假设左声道采样
     // 一阶低通滤波
    // lp = (1 - alpha) * lp + alpha * sample;
    // lp_prev = lp;

    // 生成反相信号
    float anti = -sample * ancGain ;
    int16_t out = anc_enabled ?(int16_t)anti : 0 ;
    i2s_buffer[2 * i] = out;      // 左声道
    i2s_buffer[2 * i + 1] = out ; // 右声道（立体声同步）
    // Serial.println(out);
        // 打印滤波前后的值对比
    // Serial.print("Input: ");
    // Serial.print(sample);
    // Serial.print("  LP: ");
    // Serial.println(lp);
  }

  // 写入 I2S 缓冲
  i2s_write(I2S_NUM_0, i2s_buffer, bytes_read, &bytes_written, portMAX_DELAY);

  // 只打印前几个样本（调试用）
  // int samples = bytes_read / 4; // 32bit = 4 bytes
  // for (int i = 0; i < 8 && i < samples; i++) {
  //   Serial.println(buffer_out[i]);
  // }
}
