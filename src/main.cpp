#include <Arduino.h>
// #include <AudioTools.h>
#include <BluetoothA2DPSink.h>

// #define DEBUG

#define I2S_WS   26
#define I2S_CKL  27
#define I2S_DOUT 14
                      
const int vol = 30;// 0~100

// I2SStream i2s;
BluetoothA2DPSink a2dp_sink;
// AudioInfo info(44100, 2, 16);

// Then somewhere in your sketch:
void data_received_callback() {
  Serial.println("Data packet received");
}

// void read_data_stream(const uint8_t *data, uint32_t length) {
//   // 将音量映射为 0.0 ~ 1.0
//   float gain = vol / 100.0f;
//   int16_t *samples = (int16_t *)data;
//   size_t samples_len = length / 2; // 16-bit stereo

//   for (size_t i = 0; i < samples_len; i++) {
//     int32_t val = (int32_t)(samples[i] * gain);
//     if (val > 32767) val = 32767;
//     if (val < -32768) val = -32768;
//     samples[i] = (int16_t)val;
//   }
// }

void setup() {

  #ifdef DEBUG
    Serial.begin(115200);
    Serial.println("Starting Bluetooth A2DP Sink with I2S...");
  #endif

  // 配置 I2S
  i2s_pin_config_t my_pin_config = {
      .bck_io_num = I2S_CKL,   // BCLK
      .ws_io_num = I2S_WS,    // L/R CLK
      .data_out_num = I2S_DOUT, // DATA
      .data_in_num = I2S_PIN_NO_CHANGE
  };

  // 设置为可记忆设备,上电自动连接
  // a2dp_sink.set_auto_reconnect(true);
  
    // 设置初始音量（0~255）
  uint8_t vol_255 = map(vol, 0, 100, 0, 255);

  a2dp_sink.set_volume(vol_255);  
  a2dp_sink.set_pin_config(my_pin_config);
  // a2dp_sink.set_stream_reader(read_data_stream);
  a2dp_sink.start("ESP32_MAX98357");
  // In the setup function:
  #ifdef DEBUG
    a2dp_sink.set_on_data_received(data_received_callback);
  #endif
}

void loop() {
  delay(1000);
}
