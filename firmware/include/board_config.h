#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <lgfx/v1/panel/Panel_LCD.hpp>

// IDEAQUX / Elecrow CrowPanel 1.46" HMI ESP32-S3 Rotary Display (360x360)
#define SCREEN_WIDTH  360
#define SCREEN_HEIGHT 360

// Power rails (MUST be driven HIGH before LCD / LED ring init!)
#define LCD_PWR_PIN_1  1
#define LCD_PWR_PIN_2  2
#define LED_PWR_PIN    17
#define LCD_RST_PIN    14

// Touch (CST816T/D over I2C)
#define TP_I2C_SDA_PIN 6
#define TP_I2C_SCL_PIN 7
#define TP_INT_PIN     5
#define TP_RST_PIN     13

// Rotary Encoder & Switch
#define ENCODER_A_PIN  45
#define ENCODER_B_PIN  42
#define SWITCH_PIN     41

// Backlight & Ambient RGB LEDs (8x WS2812 ring on 1.46" model)
#define SCREEN_BACKLIGHT_PIN 46
#define POWER_LIGHT_PIN      40  // Active-low on 1.46"
#define LED_PIN              48
#define LED_NUM              8

// Elecrow CrowPanel 1.46" custom 360x360 init sequence (JD9855 / patched ST77961)
struct Panel_CrowPanel146 : public lgfx::Panel_LCD {
  Panel_CrowPanel146(void) {
    _cfg.panel_width = _cfg.memory_width = 360;
    _cfg.panel_height = _cfg.memory_height = 390;
    _cfg.dummy_read_pixel = 8;
  }

 protected:
  const uint8_t* getInitCommands(uint8_t listno) const override {
    static constexpr uint8_t list0[] = {
        0xDE, 1, 0x00,
        0xDF, 2, 0x98, 0x55,
        0xB2, 1, 0x1F,
        0xB7, 4, 0x00, 0x1D, 0x00, 0x45,
        0xBB, 6, 0x1B, 0x64, 0xC4, 0x1E, 0x3E, 0xF5,
        0xBC, 4, 0x03, 0x20, 0xF3, 0xC0,
        0xC0, 2, 0x22, 0xA1,
        0xC3, 11, 0x00, 0x02, 0x2A, 0x0B, 0x08, 0x48, 0x08, 0x04, 0x62, 0x30, 0x30,
        0xC4, 11, 0x40, 0x00, 0xAD, 0x68, 0x43, 0x07, 0x04, 0x16, 0x43, 0x07, 0x04,
        0xC8, 32,
            0x3F, 0x36, 0x30, 0x2D, 0x2F, 0x31, 0x2B, 0x2A,
            0x27, 0x25, 0x1F, 0x14, 0x10, 0x09, 0x04, 0x00,
            0x3F, 0x36, 0x30, 0x2D, 0x2F, 0x31, 0x2B, 0x2A,
            0x27, 0x25, 0x1F, 0x14, 0x10, 0x09, 0x04, 0x00,
        0xD3, 2, 0x28, 0x13,
        0xD9, 6, 0x00, 0x00, 0xFF, 0x00, 0xF0, 0x00,
        0xDE, 1, 0x01,
        0xB7, 8, 0x13, 0xE7, 0x64, 0x39, 0x06, 0x36, 0x19, 0x1C,
        0xBE, 1, 0x00,
        0xC1, 3, 0x00, 0x4A, 0x80,
        0xC2, 4, 0x00, 0x16, 0xDA, 0xE7,
        0xC7, 8, 0x00, 0x00, 0x00, 0x38, 0x08, 0x08, 0x00, 0x01,
        0xC8, 6, 0x00, 0x00, 0x00, 0x00, 0x15, 0x3B,
        0xC9, 5, 0x00, 0x16, 0x06, 0x04, 0x0A,
        0xCA, 5, 0x08, 0x35, 0x16, 0x1F, 0x1F,
        0xCB, 5, 0x01, 0x16, 0x07, 0x05, 0x0B,
        0xCC, 5, 0x09, 0x35, 0x16, 0x1F, 0x1F,
        0xCD, 5, 0x01, 0x16, 0x09, 0x0B, 0x05,
        0xCE, 5, 0x07, 0x15, 0x1F, 0x16, 0x1F,
        0xCF, 5, 0x00, 0x16, 0x08, 0x0A, 0x04,
        0xD0, 5, 0x06, 0x15, 0x1F, 0x16, 0x1F,
        0xD1, 2, 0x02, 0x30,
        0xD2, 5, 0x02, 0x03, 0x52, 0xDF, 0xDD,
        0xD3, 3, 0x3B, 0x04, 0x48,
        0xD5, 7, 0x10, 0x10, 0x07, 0x07, 0x0F, 0x94, 0x26,
        0xD6, 3, 0x00, 0x00, 0x40,
        0xD7, 3, 0x00, 0x00, 0x20,
        0xDE, 1, 0x02,
        0xB6, 1, 0x1C,
        0xDE, 1, 0x00,
        0x4D, 1, 0x00,
        0x4E, 1, 0x00,
        0x4F, 1, 0x00,
        0x4C, 1 + CMD_INIT_DELAY, 0x01, 10,
        0x4C, 1, 0x00,
        0x35, 0,
        0x3A, 1, 0x05,
        0x2A, 4, 0x00, 0x00, 0x01, 0x67,
        0x2B, 4, 0x00, 0x00, 0x01, 0x67,
        CMD_SLPOUT, CMD_INIT_DELAY, 120,
        CMD_DISPON, 0,
        0xFF, 0xFF,
    };
    switch (listno) {
      case 0: return list0;
      default: return nullptr;
    }
  }

  void setColorDepth_impl(lgfx::color_depth_t depth) override {
    _write_depth = ((int)depth & lgfx::color_depth_t::bit_mask) > 16 ? lgfx::rgb888_3Byte : lgfx::rgb565_2Byte;
    _read_depth = _write_depth;
  }
};

class LGFX : public lgfx::LGFX_Device {
  Panel_CrowPanel146 _panel_instance;
  lgfx::Bus_SPI _bus_instance;

 public:
  LGFX(void) {
    {
      auto cfg = _bus_instance.config();
      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 80000000;
      cfg.freq_read = 20000000;
      cfg.spi_3wire = true;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = 10;
      cfg.pin_mosi = 11;
      cfg.pin_miso = -1;
      cfg.pin_dc = 3;
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }

    {
      auto cfg = _panel_instance.config();
      cfg.pin_cs = 9;
      cfg.pin_rst = LCD_RST_PIN;
      cfg.pin_busy = -1;
      cfg.memory_width = 360;
      cfg.memory_height = 360;
      cfg.panel_width = 360;
      cfg.panel_height = 360;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits = 1;
      cfg.readable = false;
      cfg.invert = false;
      cfg.rgb_order = true;
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;
      _panel_instance.config(cfg);
    }

    setPanel(&_panel_instance);
  }
};
