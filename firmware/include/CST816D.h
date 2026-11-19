#pragma once

#include <Arduino.h>
#include <Wire.h>

#define I2C_ADDR_CST816D 0x15

enum TouchGesture : uint8_t {
  GESTURE_NONE       = 0x00,
  GESTURE_SLIDE_DOWN = 0x01,
  GESTURE_SLIDE_UP   = 0x02,
  GESTURE_SLIDE_LEFT = 0x03,
  GESTURE_SLIDE_RIGHT= 0x04,
  GESTURE_SINGLE_TAP = 0x05,
  GESTURE_DOUBLE_TAP = 0x0B,
  GESTURE_LONG_PRESS = 0x0C
};

class CST816D {
 public:
  CST816D(int8_t sda_pin = -1, int8_t scl_pin = -1, int8_t rst_pin = -1, int8_t int_pin = -1);
  void begin(void);
  bool getTouch(uint16_t *x, uint16_t *y, uint8_t *gesture);

 private:
  int8_t _sda;
  int8_t _scl;
  int8_t _rst;
  int8_t _int;

  bool i2c_read(uint8_t addr, uint8_t *out);
  bool i2c_read_continuous(uint8_t addr, uint8_t *data, uint32_t length);
  void i2c_write(uint8_t addr, uint8_t data);
};
