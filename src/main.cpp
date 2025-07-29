#include <Arduino.h>
#include <BluetoothA2DPSink.h>
#include <AudioTools.h>

I2SStream i2s;
BluetoothA2DPSink a2dp_sink(i2s);
AudioInfo info(44100, 2, 16);

// Then somewhere in your sketch:
void data_received_callback() {
  Serial.println("Data packet received");
}


void setup() {
  Serial.begin(115200);
  Serial.println("Starting Bluetooth A2DP Sink with I2S...");

  // 配置 I2S
  auto cfg = i2s.defaultConfig();
  // cfg.sample_rate = 44100;
  // cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.pin_bck = 14;
  cfg.pin_ws = 15;
  cfg.pin_data = 19;
  i2s.begin(cfg);
  

  a2dp_sink.start("ESP32_PCM5102");
  // In the setup function:
  a2dp_sink.set_on_data_received(data_received_callback);

}

void loop() {
  delay(1000);
}
