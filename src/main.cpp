#include <Arduino.h>
// #include <AudioTools.h>
#include <BluetoothA2DPSink.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <ESP32Encoder.h>

// -------------------- CONFIG --------------------
// Choose which driver to compile for:
#define USE_SH1106
// #define USE_SSD1306
// #define DEBUG

// I2S pings
#define I2S_WS 26
#define I2S_CKL 27
#define I2S_DOUT 14

// I2C pins
#define I2C_SDA 21
#define I2C_SCL 22
#define I2C_ADDR 0x3C

// Screen geometry
const uint8_t SCREEN_W = 128;
const uint8_t SCREEN_H = 32;

// Font choice (u8g2 fonts). Pick a readable small font for title.
#define TITLE_FONT u8g2_font_6x12_tf
#define INFO_FONT u8g2_font_5x8_tr

// How fast title scrolls (ms per pixel shift)
const unsigned long TITLE_SCROLL_INTERVAL = 150;

// If title shorter than this, no scroll
const int TITLE_SCROLL_MARGIN = 4;

// 编码器引脚（示例）
// A/B 使用输入引脚，建议使用不带内部上拉的大多数引脚或启用上拉
const uint8_t ENC_A = 18;   // 输入专用引脚示例（ESP32 输入引脚）
const uint8_t ENC_B = 19;   // 输入专用
const uint8_t ENC_BTN = 25; // 编码器按键（短按静音）
const int PIN_NEXT = 33;
const int PIN_PREV = 32;
const int PIN_PLAY = 23; // 自行选一个空闲 GPIO

ESP32Encoder myEncoder;
int32_t previousEncoderValue = 0;

// 用于 ISR 与主循环通信的变量
volatile int16_t encoderPos = 0;    // 累积的微步（每次状态变化计 +1/-1）
volatile uint8_t lastEncoded = 0;   // 上一个编码器状态（2 bit）
volatile bool encoderMoved = false; // 标记主循环需要处理

// 按键状态（轮询）
bool btnLast = HIGH;
unsigned long btnLastChange = 0;
bool btnHandled = false;
unsigned long btnPressTime = 0;
bool isMuted = false; // 编码器按键短按用于静音切换

// 去抖参数
// 原有去抖常量（你文件里已有 DEBOUNCE_MS = 50）
const unsigned long DEBOUNCE_MS = 50;       // 保留或替换为你原来的值
const unsigned long BTN_LONGPRESS_MS = 800; // 新增：长按阈值

// -------------------- Global objects --------------------
// I2SStream i2s;
BluetoothA2DPSink a2dp_sink;
// AudioInfo info(44100, 2, 16);

#ifdef USE_SH1106
// SH1106 128x64 I2C
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R2, /* reset=*/U8X8_PIN_NONE, /*scl*/ I2C_SCL, /*sda*/ I2C_SDA);
#else
// SSD1306 128x64 I2C
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R2, /* reset=*/U8X8_PIN_NONE, /*scl*/ I2C_SCL, /*sda*/ I2C_SDA);
#endif

int8_t esp32Vol = 10; // 0~100

// metadata 缓存（AVRCP 回调里会更新）
String metaTitle = "--";
String metaArtist = "--";
volatile bool metaUpdated = false;

// title scrolling state (not in ISR)
unsigned long lastTitleScrollMillis = 0;
int titleScrollX = 0; // pixel offset (positive -> left shift)
int titlePixelWidth = 0;
String titleBuf; // local copy for drawing

// 按键状态与防抖记录
struct Button
{
  int pin;
  bool lastState;
  unsigned long lastChange;
  bool handled; // 标记是否已处理这次按下（防止长按重复）
};
Button btnNext = {PIN_NEXT, HIGH, 0, false};
Button btnPrev = {PIN_PREV, HIGH, 0, false};
Button btnPlay = {PIN_PLAY, HIGH, 0, false};

// Then somewhere in your sketch:
void data_received_callback()
{
  // Serial.println("Data packet received");
}

// 将 AVRCP 元数据转换并存储
void avrc_metadata_callback(uint8_t attribute_id, const uint8_t *data);

// 当远端设备改变本地（controller）音量时（可选）
void avrc_rn_volumechange_callback(int vol);

// 初始化按键（上拉，低电平为按下）
void initButtons();

// 按键轮询与去抖（在 loop 中调用）
void pressButtons();

// void IRAM_ATTR handleEncoderISR();

// void setupEncoderPins();

// void processEncoderMovement();

// 编码器按键去抖与处理（短按静音切换）
void handleEncoderButton();

// -------------------- Display helpers --------------------
void prepareTitleForDraw();

// draw volume bar (bottom area)
void drawVolumeBar(int x, int y, int w, int h, int percent);

// draw the UI (title + artist small + vol bar + percent)
void renderUI();

// call periodically to update scrolling
void updateTitleScroll();

void handleVolume();

// -------------------- Setup / Loop --------------------

void setup()
{

#ifdef DEBUG
  Serial.begin(115200);
  Serial.println("Starting A2DP + OLED display...");
#endif

  // Init I2C explicitly with chosen pins
  Wire.begin(I2C_SDA, I2C_SCL);

  // init u8g2
  u8g2.begin();

  // init buttons
  initButtons();

  // Enable the weak pull up resistors
  ESP32Encoder::useInternalWeakPullResistors = puType::up;

  // use pin 19 and 18 for the first encoder
  myEncoder.attachHalfQuad(ENC_A, ENC_B);

  myEncoder.setCount(37);

  myEncoder.clearCount();

#ifdef DEBUG

  Serial.println("Encoder Start = " + String((int32_t)myEncoder.getCount()));
#endif
  // initial blank title processing
  prepareTitleForDraw();

  // 配置 I2S
  i2s_pin_config_t my_pin_config = {
      .bck_io_num = I2S_CKL,    // BCLK
      .ws_io_num = I2S_WS,      // L/R CLK
      .data_out_num = I2S_DOUT, // DATA
      .data_in_num = I2S_PIN_NO_CHANGE};
  a2dp_sink.set_pin_config(my_pin_config);

  // 设置为可记忆设备,上电自动连接
  // a2dp_sink.set_auto_reconnect(true);

  // 请求我们感兴趣的 metadata 字段（Title + Artist）
  // 使用 esp-avrc 常量的位掩码（library 接受 ESP_AVRC_MD_ATTR_*）
  a2dp_sink.set_avrc_metadata_attribute_mask(ESP_AVRC_MD_ATTR_TITLE | ESP_AVRC_MD_ATTR_ARTIST);

  // 注册 metadata 回调
  a2dp_sink.set_avrc_metadata_callback(avrc_metadata_callback);

  // 可选：注册当远端改变音量的通知（这样显示可被远端音量同步）
  a2dp_sink.set_avrc_rn_volumechange(avrc_rn_volumechange_callback);

  // 设置初始音量（0~255）
  uint8_t vol_255 = map(esp32Vol, 0, 100, 0, 255);
  a2dp_sink.set_volume(vol_255);

  // 放在 setup() 里，start() 之前：
  a2dp_sink.set_on_connection_state_changed([](esp_a2d_connection_state_t state, void *)
                                            {
  Serial.print("[A2DP] connection state: ");
  Serial.println((int)state); });

  a2dp_sink.set_avrc_connection_state_callback([](bool connected)
                                               {
  Serial.print("[AVRCP] connected: ");
  Serial.println(connected ? "YES" : "NO"); });

  // a2dp_sink.set_stream_reader(read_data_stream);
  a2dp_sink.start("ESP32_Speaker_2.0");
// In the setup function:
#ifdef DEBUG
  a2dp_sink.set_on_data_received(data_received_callback);
#endif

  // initial render
  renderUI();
}

//~~~~~~~~~~~LOOP~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~//
void loop()
{

  // // 处理编码器移动（发送 AVRCP）
  // processEncoderMovement();

  // // 处理编码器按键（短按静音）
  // handleEncoderButton();

  // 轮询按键
  pressButtons();

  handleVolume();

  // 如果 metadata 有更新则刷新显示
  if (metaUpdated)
  {
    metaUpdated = false;
    prepareTitleForDraw();

#ifdef DEBUG

    // 打印到串口
    Serial.println("===== Song Info =====");
    Serial.print("Title : ");
    Serial.println(metaTitle);
    Serial.print("Artist: ");
    Serial.println(metaArtist);
    Serial.print("Volume: ");
    Serial.print(esp32Vol);
    Serial.println("%");
    Serial.println("=====================");

#endif
  }
  // scroll title if needed (and re-render at each scroll step)
  updateTitleScroll();
  static unsigned long lastRender = 0;
  // throttle renders to avoid burning CPU — render on scroll step or every 500ms
  if (millis() - lastRender > 500 || titlePixelWidth + TITLE_SCROLL_MARGIN > SCREEN_W)
  {
    renderUI();
    lastRender = millis();
  }
  delay(10);
}

// 将 AVRCP 元数据转换并存储
void avrc_metadata_callback(uint8_t attribute_id, const uint8_t *data)
{
  // data 指向 C 字符串（库文档说明），所以直接转换为 String
  if (!data)
    return;
  String s = String((const char *)data);

  // attribute_id 使用 esp_avrc_md_attr_mask_t 定义：
  // 常见： 0x01 = Title, 0x02 = Artist, 0x03 = Album ...
  // 这里做常见映射（也可依据 esp-idf 文档用宏）
  switch (attribute_id)
  {
  case 0x01: // title
    metaTitle = s;
    break;
  case 0x02: // artist
    metaArtist = s;
    break;
  default:
    // 其他属性我们暂时忽略
    break;
  }
  metaUpdated = true;
}

// 当远端设备改变本地（controller）音量时（可选）
void avrc_rn_volumechange_callback(int vol)
{

  // 有些设备上报 0..127，有些上报 0..255
  int mappedPercent;
  // if (vol <= 127)
  // {
  //   // 很可能是 0..127
  mappedPercent = map(constrain(vol, 0, 127), 0, 127, 0, 100);

  // }
  // else
  // {
  //   // 否则当作 0..255
  //   mappedPercent = map(constrain(vol, 0, 255), 0, 255, 0, 100);
  //   Serial.println("Assume scale 0..255");
  // }

  // 更新显示变量（及可选地同步本地硬件增益）
  esp32Vol = mappedPercent;

#ifdef DEBUG
  // 打印原始上报（用于调试）
  Serial.print("AVRCP raw vol reported: ");
  Serial.println(vol);
  Serial.print("Mapped to percent: ");
  Serial.print(esp32Vol);
  Serial.println("%");
#endif

  // 可选：把本地输出增益同步到这个百分比（如果你希望本地硬件也跟随）
  // a2dp_sink.set_volume(map(esp32Vol, 0, 100, 0, 127));

  metaUpdated = true;
}

// 初始化按键（上拉，低电平为按下）
void initButtons()
{
  Button *btns[] = {&btnNext, &btnPrev, &btnPlay};
  for (auto b : btns)
  {
    pinMode(b->pin, INPUT_PULLUP);
    b->lastState = digitalRead(b->pin);
    b->lastChange = millis();
    b->handled = false;
  }
}

// 按键轮询与去抖（在 loop 中调用）
void pressButtons()
{
  Button *btns[] = {&btnNext, &btnPrev};
  for (auto b : btns)
  {
    bool cur = digitalRead(b->pin);
    if (cur != b->lastState)
    {
      // 状态改变，更新时间
      b->lastChange = millis();
      b->lastState = cur;
      b->handled = false;
    }
    else
    {
      // 状态稳定，检查是否是稳定的按下事件
      if (!b->handled && (millis() - b->lastChange) > DEBOUNCE_MS)
      {
        // 低电平为按下（使用 INPUT_PULLUP）
        if (cur == LOW)
        {
          // 识别是哪一个按钮并执行操作
          if (b == &btnNext)
          {
            a2dp_sink.next(); // AVRCP next
#ifdef DEBUG
            Serial.println("Next ");
#endif
          }
          else if (b == &btnPrev)
          {
            a2dp_sink.previous(); // AVRCP previous
#ifdef DEBUG
            Serial.println("Previous ");
#endif
          }
          else if (b == &btnPlay)
          {
            static bool playing = true;
            if (playing)
            {
              a2dp_sink.pause();
            }
            else
            {
              a2dp_sink.play();
            }
            playing = !playing;
          }

          metaUpdated = true;
          b->handled = true;
        }
      }
    }
  }
}

// 编码器按键去抖与处理（短按静音切换）
void handleEncoderButton()
{
  bool cur = digitalRead(ENC_BTN);
  if (cur != btnLast)
  {
    btnLastChange = millis();
    btnLast = cur;
    btnHandled = false;
    if (cur == LOW)
    {
      btnPressTime = millis(); // 记录按下时间
    }
    else
    {
      // 按键释放
      unsigned long held = millis() - btnPressTime;
      if (!btnHandled && (held >= BTN_LONGPRESS_MS))
      {
        // 短按（释放时触发）
        if (held < BTN_LONGPRESS_MS)
        {
          // 切换静音（示例）
          isMuted = !isMuted;
          if (isMuted)
          {
#ifdef DEBUG
            Serial.println("Encoder button: MUTE ON");
#endif
            // 本地静音：把本地输出设为 0（注意：不会改变远端系统音量）
            a2dp_sink.set_volume(0);
          }
          else
          {
#ifdef DEBUG
            Serial.println("Encoder button: MUTE OFF");
#endif
            // 恢复到远端音量显示（若已知 esp32Vol）
            a2dp_sink.set_volume(map(constrain(esp32Vol, 0, 100), 0, 100, 0, 127));
          }
        }
        else
        {
// 长按可以在这里扩展其他功能
#ifdef DEBUG
          Serial.println("Encoder button: LONG PRESS (no action)");
#endif
        }
        btnHandled = true;
      }
    }
  }
  else
  {
    // 状态稳定
    if (!btnHandled && (millis() - btnLastChange) > BTN_LONGPRESS_MS)
    {
      // nothing to do, 等待 release 处理；避免重复
    }
  }
}

// -------------------- Display helpers --------------------
void prepareTitleForDraw()
{
  // make a stable local copy so we don't read volatile String while drawing
  noInterrupts();
  titleBuf = String(metaTitle);
  interrupts();

  // if empty, show placeholder
  if (titleBuf.length() == 0)
    titleBuf = "--";

  // set font and compute pixel width
  u8g2.setFont(TITLE_FONT);
  // u8g2.getUTF8Width expects a C string
  titlePixelWidth = u8g2.getUTF8Width(titleBuf.c_str());

  // reset scrolling if content changed
  titleScrollX = 0;
  lastTitleScrollMillis = millis();
}

// draw volume bar (bottom area)
void drawVolumeBar(int x, int y, int w, int h, int percent)
{
  // border
  u8g2.drawFrame(x, y, w, h);
  // inner width
  int inner = (w - 2) * constrain(percent, 0, 100) / 100;
  if (inner > 0)
  {
    u8g2.drawBox(x + 1, y + 1, inner, h - 2);
  }
}

// draw the UI (title + artist small + vol bar + percent)
void renderUI()
{
  u8g2.clearBuffer();

  // Title area (top). We'll try to center vertically in top ~28px
  u8g2.setFont(TITLE_FONT);
  int titleY = 12; // baseline
  // if title pixel width <= screen, draw centered; else draw with scroll offset
  if (titlePixelWidth + TITLE_SCROLL_MARGIN <= SCREEN_W)
  {
    // centered
    int tx = (SCREEN_W - titlePixelWidth) / 2;
    u8g2.drawUTF8(tx, titleY, titleBuf.c_str());
  }
  else
  {
    // need to scroll horizontally
    // draw title starting at x = -titleScrollX (so increasing scrollX moves left)
    int tx = -titleScrollX;
    u8g2.drawUTF8(tx, titleY, titleBuf.c_str());
    // also draw second copy after it for smooth wrap
    u8g2.drawUTF8(tx + titlePixelWidth + 8, titleY, titleBuf.c_str()); // gap 8px
  }

  // // Artist (smaller) on second line
  // u8g2.setFont(INFO_FONT);
  // noInterrupts();
  // String art = String(metaArtist);
  // interrupts();
  // if (art.length() == 0)
  //   art = "--";
  // int artistWidth = u8g2.getUTF8Width(art.c_str());
  // int ax = (SCREEN_W - artistWidth) / 2;
  // u8g2.drawUTF8(ax, 26, art.c_str());

  // Volume bar (x,y,w,h)
  int barW = SCREEN_W - 16;
  int barH = 10;
  int barX = 8;
  int barY = 22;
  drawVolumeBar(barX, barY, barW, barH, esp32Vol);

  u8g2.sendBuffer();
}

// call periodically to update scrolling
void updateTitleScroll()
{
  if (titlePixelWidth + TITLE_SCROLL_MARGIN <= SCREEN_W)
    return; // no scroll needed

  unsigned long now = millis();
  if (now - lastTitleScrollMillis >= TITLE_SCROLL_INTERVAL)
  {
    lastTitleScrollMillis = now;
    titleScrollX++;
    // when shifted past width + gap -> reset
    if (titleScrollX > titlePixelWidth + 8)
    { // same gap as renderUI uses
      titleScrollX = 0;
    }
  }
}

void handleVolume()
{
  int32_t newVal = myEncoder.getCount();
  int32_t d = newVal - previousEncoderValue;
  if (d != 0)
  {
    previousEncoderValue = newVal; // 消费到当前位置
#ifdef DEBUG
    Serial.println("Delta:" + String(d));
#endif
    int steps = d;
    // if (d > 0) {
    //   for (int i = 0; i < d; ++i) a2dp_sink.volume_up();
    // } else {
    //   for (int i = 0; i < -d; ++i) a2dp_sink.volume_down();
    // }
    // 否则，直接修改本地 volume（0..255 或依库而定）
    int currentLibVol = a2dp_sink.get_volume(); // 库内部的 0..127 或 0..255，根据实现
    // 估算映射：这里假设库使用 0..127. 你可以调整 scale
    int stepValue = 1; // 每步改变多少库单位（试验）
    int newVol = currentLibVol + (steps > 0 ? stepValue * steps : stepValue * steps);
    // 限定范围，若库用 0..127 请用相应上下限
    newVol = constrain(newVol, 0, 127);
#ifdef DEBUG
    Serial.printf("Setting local lib volume: %d -> %d\n", currentLibVol, newVol);
#endif
    a2dp_sink.set_volume(newVol);
    // 若你还需本地显示为 0..100%，把 newVol 映射为百分比并更新 esp32Vol
    esp32Vol = map(newVol, 0, 127, 0, 100);
    metaUpdated = true; // 触发界面刷新
  }
}
