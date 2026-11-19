#include <Arduino.h>
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>
#include <lvgl.h>
#include <mbedtls/base64.h>

#include "board_config.h"
#include "CST816D.h"
#include "ui.h"

// Hardware instances
static LGFX gfx;
static CST816D touch(TP_I2C_SDA_PIN, TP_I2C_SCL_PIN, TP_RST_PIN, TP_INT_PIN);
static Adafruit_NeoPixel leds(LED_NUM, LED_PIN, NEO_GRB + NEO_KHZ800);

// LVGL 8.3 display & input drivers
static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf1 = nullptr;
static lv_color_t *buf2 = nullptr;

// Quadrature rotary encoder ISR state (proven Elecrow transition table)
static volatile int8_t encQuadCount = 0;
static volatile int32_t encPendingDetents = 0;
static volatile uint8_t encLastState = 0;
static portMUX_TYPE encMux = portMUX_INITIALIZER_UNLOCKED;

DRAM_ATTR static const int8_t encTable[16] = {
  0, -1,  1,  0,
  1,  0,  0, -1,
 -1,  0,  0,  1,
  0,  1, -1,  0
};

// Button state machine (single click, double click, long press)
static bool btnPrevDown = false;
static unsigned long btnDownAt = 0;
static bool btnLongHandled = false;
static int btnPendingClicks = 0;
static unsigned long btnLastReleaseAt = 0;
static const unsigned long LONG_PRESS_MS = 550;
static const unsigned long DOUBLE_CLICK_MS = 280;

// Local mirrored state
static int currentVolume = 35;
static bool currentMuted = false;
static bool mediaPlaying = false;
static uint8_t mediaAccentR = 0, mediaAccentG = 210, mediaAccentB = 240;
static unsigned long lastHostMsgAt = 0;
static String serialRxBuf;

// Offset-based Base64 album-art staging buffer (atomic swap on "ae")
static uint8_t *artStageBuf = nullptr;
static size_t artChunksReceived = 0;

// Swipe detection inside touch callback
static volatile bool pendingSwipeToggle = false;

static void IRAM_ATTR encoderISR() {
  uint8_t state = ((uint8_t)digitalRead(ENCODER_A_PIN) << 1) |
                  (uint8_t)digitalRead(ENCODER_B_PIN);
  uint8_t trans = (encLastState << 2) | state;
  int8_t mv = encTable[trans];

  portENTER_CRITICAL_ISR(&encMux);
  encLastState = state;
  if (mv != 0) {
    encQuadCount += mv;
    if (encQuadCount >= 4) {
      encPendingDetents++;
      encQuadCount -= 4;
    } else if (encQuadCount <= -4) {
      encPendingDetents--;
      encQuadCount += 4;
    }
  }
  portEXIT_CRITICAL_ISR(&encMux);
}

static void sendEventJson(const JsonDocument &doc) {
  serializeJson(doc, Serial);
  Serial.write('\n');
}

static void sendMediaCmd(const char *cmd) {
  JsonDocument doc;
  doc["e"] = "media";
  doc["cmd"] = cmd;
  sendEventJson(doc);
}

static void sendVolumeDelta(int delta) {
  currentVolume = constrain(currentVolume + delta * 3, 0, 100);
  currentMuted = false;
  ui_update_volume_local(currentVolume, currentMuted);

  JsonDocument doc;
  doc["e"] = "vol";
  doc["delta"] = delta;
  sendEventJson(doc);
}

static void sendApprovalDecision(const char *id, const char *decision) {
  JsonDocument doc;
  doc["e"] = "approve";
  doc["id"] = id;
  doc["decision"] = decision;
  sendEventJson(doc);
}

static void sendFocusIde(const char *platform) {
  JsonDocument doc;
  doc["e"] = "focus";
  doc["platform"] = platform;
  sendEventJson(doc);
}

static void sendBlenderCmd(const char *act, const char *mode, int dx, int dy) {
  JsonDocument doc;
  doc["e"] = "blender";
  doc["act"] = act;
  doc["mode"] = mode;
  doc["dx"] = dx;
  doc["dy"] = dy;
  sendEventJson(doc);
}

static void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);
  if (gfx.getStartCount() > 0) gfx.endWrite();
  gfx.pushImageDMA(area->x1, area->y1, w, h, (lgfx::rgb565_t *)&color_p->full);
  gfx.waitDMA();
  lv_disp_flush_ready(disp);
}

static void my_touchpad_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data) {
  (void)indev_drv;
  static bool tracking = false;
  static bool swiped = false;
  static int16_t startX = 0, startY = 0, lastX = 0, lastY = 0;

  uint16_t tx = 0, ty = 0;
  uint8_t gesture = GESTURE_NONE;
  bool touched = touch.getTouch(&tx, &ty, &gesture);

  if (touched) {
    data->state = LV_INDEV_STATE_PR;
    data->point.x = tx;
    data->point.y = ty;
    if (!tracking) {
      tracking = true;
      swiped = false;
      startX = tx;
      startY = ty;
    }
    lastX = tx;
    lastY = ty;

    bool swipeAllowed = !ui_is_approval_active();
    if (ui_get_current_view() == VIEW_BLENDER && startY >= 45 && startY <= 315) {
      swipeAllowed = false;  // Reserve pad + cardinal snap ring for 3D controls
    }

    if (!swiped && swipeAllowed) {
      int dx = lastX - startX;
      int dy = lastY - startY;
      if (gesture == GESTURE_SLIDE_LEFT || gesture == GESTURE_SLIDE_RIGHT ||
          (abs(dx) >= 30 && abs(dx) > abs(dy))) {
        swiped = true;
        pendingSwipeToggle = true;
      }
    }
  } else {
    data->state = LV_INDEV_STATE_REL;
    tracking = false;
    swiped = false;
  }
}

static void initBacklight(uint8_t pct) {
  uint8_t duty = (uint8_t)((pct * 255) / 100);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(SCREEN_BACKLIGHT_PIN, 5000, 8);
  ledcWrite(SCREEN_BACKLIGHT_PIN, duty);
#else
  ledcSetup(0, 5000, 8);
  ledcAttachPin(SCREEN_BACKLIGHT_PIN, 0);
  ledcWrite(0, duty);
#endif
}

static void updateAmbientLeds() {
  static uint8_t prevR = 255, prevG = 255, prevB = 255;

  uint8_t r = 0, g = 0, b = 0;
  if (ui_is_approval_active()) {
    // Solid amber for pending approval
    r = 45; g = 24; b = 0;
  } else if (ui_get_agent_state() == AGENT_ERROR) {
    r = 40; g = 0; b = 0;
  } else if (ui_get_agent_state() == AGENT_WORKING) {
    r = 0; g = 14; b = 32;
  } else if (ui_get_agent_state() == AGENT_DONE && ui_get_current_view() == VIEW_AGENT) {
    r = 0; g = 28; b = 8;
  } else if (ui_get_current_view() == VIEW_BLENDER) {
    r = 38; g = 14; b = 0;  // Blender orange
  } else if (mediaPlaying) {
    const float scale = 0.14f;
    r = (uint8_t)(mediaAccentR * scale);
    g = (uint8_t)(mediaAccentG * scale);
    b = (uint8_t)(mediaAccentB * scale);
  } else {
    r = 2; g = 3; b = 6;
  }

  if (r == prevR && g == prevG && b == prevB) return;
  prevR = r; prevG = g; prevB = b;

  for (int i = 0; i < LED_NUM; i++) {
    leds.setPixelColor(i, leds.Color(r, g, b));
  }
  leds.show();
}

static void handleSerialLine(const String &line) {
  if (line.length() < 2) return;

  JsonDocument doc;
  if (deserializeJson(doc, line)) return;

  lastHostMsgAt = millis();
  const char *type = doc["t"] | "";

  if (strcmp(type, "media") == 0) {
    MediaInfo info;
    info.title    = String((const char *)(doc["title"] | ""));
    info.artist   = String((const char *)(doc["artist"] | ""));
    info.app      = String((const char *)(doc["app"] | "MAC MEDIA"));
    info.playing  = doc["playing"] | false;
    info.volume   = doc["vol"] | currentVolume;
    info.muted    = doc["muted"] | false;
    info.progress = doc["pos"] | 0;

    currentVolume = info.volume;
    currentMuted  = info.muted;
    mediaPlaying  = info.playing;
    if (doc["rgb"].is<JsonArray>() && doc["rgb"].size() == 3) {
      mediaAccentR = doc["rgb"][0] | 0;
      mediaAccentG = doc["rgb"][1] | 210;
      mediaAccentB = doc["rgb"][2] | 240;
    }
    info.accentR = mediaAccentR;
    info.accentG = mediaAccentG;
    info.accentB = mediaAccentB;
    ui_update_media(info);
  } else if (strcmp(type, "ac") == 0) {
    // Framed Base64 RGB565 chunk: {"t":"ac","o":<offset>,"d":"<b64>"}
    size_t offset = doc["o"] | 0;
    const char *b64 = doc["d"] | "";
    size_t b64Len = strlen(b64);
    if (artStageBuf && b64Len > 0 && offset < ART_BYTES) {
      size_t outLen = 0;
      uint8_t tmp[600];
      if (mbedtls_base64_decode(tmp, sizeof(tmp), &outLen,
                                (const unsigned char *)b64, b64Len) == 0) {
        if (offset + outLen <= ART_BYTES) {
          memcpy(artStageBuf + offset, tmp, outLen);
          artChunksReceived++;
        }
      }
    }
  } else if (strcmp(type, "ae") == 0) {
    // Commit staged artwork atomically once transfer completes
    uint8_t *dst = ui_get_art_buffer();
    if (dst && artStageBuf && artChunksReceived > 0) {
      memcpy(dst, artStageBuf, ART_BYTES);
      ui_commit_art();
      JsonDocument ack;
      ack["e"] = "art_ok";
      ack["chunks"] = artChunksReceived;
      sendEventJson(ack);
    }
    artChunksReceived = 0;
  } else if (strcmp(type, "art_clear") == 0) {
    artChunksReceived = 0;
    ui_clear_art();
  } else if (strcmp(type, "agent") == 0) {
    AgentInfo info;
    info.platform = String((const char *)(doc["platform"] | "antigravity"));
    info.title    = String((const char *)(doc["title"] | ""));
    info.detail   = String((const char *)(doc["detail"] | ""));
    const char *st = doc["state"] | "idle";
    if (strcmp(st, "working") == 0)      info.state = AGENT_WORKING;
    else if (strcmp(st, "done") == 0)    info.state = AGENT_DONE;
    else if (strcmp(st, "error") == 0)   info.state = AGENT_ERROR;
    else if (strcmp(st, "waiting") == 0) info.state = AGENT_WAITING;
    else                                 info.state = AGENT_IDLE;
    ui_update_agent(info);
  } else if (strcmp(type, "approve") == 0) {
    ApprovalRequest req;
    req.active   = true;
    req.id       = String((const char *)(doc["id"] | ""));
    req.platform = String((const char *)(doc["platform"] | "antigravity"));
    req.tool     = String((const char *)(doc["tool"] | "Action"));
    req.detail   = String((const char *)(doc["detail"] | ""));
    req.selectedOption = 0;
    ui_show_approval(req);
  } else if (strcmp(type, "approve_clear") == 0) {
    ui_clear_approval();
  }
}

static void pollSerial() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n') {
      String line = serialRxBuf;
      serialRxBuf = "";
      line.trim();
      if (line.length() > 0) {
        handleSerialLine(line);
      }
    } else if (c != '\r' && serialRxBuf.length() < 1200) {
      serialRxBuf += c;
    }
  }
}

static void pollEncoderAndButton() {
  // 1. Consume rotary detents
  int32_t detents = 0;
  portENTER_CRITICAL(&encMux);
  detents = encPendingDetents;
  encPendingDetents = 0;
  portEXIT_CRITICAL(&encMux);

  if (detents != 0) {
    if (ui_is_approval_active()) {
      ui_approval_move_selection(detents > 0 ? 1 : -1);
    } else if (ui_get_current_view() == VIEW_BLENDER) {
      sendBlenderCmd("dial", "zoom", (int)detents, 0);
    } else {
      sendVolumeDelta((int)detents);
    }
  }

  // 2. Consume touch swipe toggle
  if (pendingSwipeToggle) {
    pendingSwipeToggle = false;
    ui_toggle_main_view();
  }

  // 3. Poll push switch (active-low)
  unsigned long now = millis();
  bool down = (digitalRead(SWITCH_PIN) == LOW);

  if (down && !btnPrevDown) {
    btnDownAt = now;
    btnLongHandled = false;
  } else if (down && btnPrevDown && !btnLongHandled && (now - btnDownAt >= LONG_PRESS_MS)) {
    btnLongHandled = true;
    btnPendingClicks = 0;
    if (!ui_is_approval_active()) {
      ui_toggle_main_view();
    }
  } else if (!down && btnPrevDown) {
    if (!btnLongHandled && (now - btnDownAt >= 20)) {
      btnPendingClicks++;
      btnLastReleaseAt = now;
    }
  }
  btnPrevDown = down;

  // Dispatch single / double click once double-click window elapses
  if (btnPendingClicks > 0 && (now - btnLastReleaseAt >= DOUBLE_CLICK_MS)) {
    int clicks = btnPendingClicks;
    btnPendingClicks = 0;

    if (ui_is_approval_active()) {
      ui_approval_confirm();
    } else if (ui_get_current_view() == VIEW_MEDIA) {
      if (clicks == 1) sendMediaCmd("toggle");
      else             sendMediaCmd("next");
    } else if (ui_get_current_view() == VIEW_BLENDER) {
      if (clicks == 1) ui_blender_cycle_mode();
      else             sendBlenderCmd("frame", ui_get_blender_mode_str(), 0, 0);
    } else {
      if (clicks == 1) sendFocusIde(ui_get_active_platform());
      else             ui_toggle_main_view();
    }
  }
}

void setup() {
  Serial.setRxBufferSize(16384);
  Serial.begin(115200);
  delay(200);

  pinMode(POWER_LIGHT_PIN, OUTPUT);
  digitalWrite(POWER_LIGHT_PIN, LOW);  // Active-low power LED on 1.46"

  // Enable LCD power rails and addressable LED ring power rail
  pinMode(LCD_PWR_PIN_1, OUTPUT);
  digitalWrite(LCD_PWR_PIN_1, HIGH);
  pinMode(LCD_PWR_PIN_2, OUTPUT);
  digitalWrite(LCD_PWR_PIN_2, HIGH);
  pinMode(LED_PWR_PIN, OUTPUT);
  digitalWrite(LED_PWR_PIN, HIGH);

  // Explicit LCD reset pulse after power rails come up
  pinMode(LCD_RST_PIN, OUTPUT);
  digitalWrite(LCD_RST_PIN, HIGH);
  delay(10);
  digitalWrite(LCD_RST_PIN, LOW);
  delay(10);
  digitalWrite(LCD_RST_PIN, HIGH);
  delay(20);

  pinMode(ENCODER_A_PIN, INPUT_PULLUP);
  pinMode(ENCODER_B_PIN, INPUT_PULLUP);
  pinMode(SWITCH_PIN, INPUT_PULLUP);
  encLastState = ((uint8_t)digitalRead(ENCODER_A_PIN) << 1) |
                 (uint8_t)digitalRead(ENCODER_B_PIN);
  attachInterrupt(digitalPinToInterrupt(ENCODER_A_PIN), encoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENCODER_B_PIN), encoderISR, CHANGE);

  leds.begin();
  leds.setBrightness(255);
  leds.clear();
  leds.show();

  gfx.init();
  gfx.initDMA();
  gfx.fillScreen(TFT_BLACK);
  touch.begin();

  lv_init();
  const size_t bufPixels = SCREEN_WIDTH * 60;
  buf1 = (lv_color_t *)ps_malloc(bufPixels * sizeof(lv_color_t));
  buf2 = (lv_color_t *)ps_malloc(bufPixels * sizeof(lv_color_t));
  if (!buf1) buf1 = (lv_color_t *)malloc(bufPixels * sizeof(lv_color_t));
  lv_disp_draw_buf_init(&draw_buf, buf1, buf2, bufPixels);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = SCREEN_WIDTH;
  disp_drv.ver_res = SCREEN_HEIGHT;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = my_touchpad_read;
  lv_indev_drv_register(&indev_drv);

  ui_init(sendMediaCmd, sendApprovalDecision, sendFocusIde, sendBlenderCmd);
  artStageBuf = (uint8_t *)ps_malloc(ART_BYTES);
  initBacklight(75);

  JsonDocument hello;
  hello["e"] = "hello";
  hello["fw"] = "rotary-hub-1.0";
  hello["board"] = "crowpanel-1.46";
  sendEventJson(hello);
}

void loop() {
  pollSerial();
  pollEncoderAndButton();
  lv_timer_handler();
  updateAmbientLeds();

  if (lastHostMsgAt > 0 && millis() - lastHostMsgAt > 15000) {
    ui_set_connected(false);
    lastHostMsgAt = 0;
  }
  delay(4);
}
