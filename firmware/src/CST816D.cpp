#include "CST816D.h"

CST816D::CST816D(int8_t sda_pin, int8_t scl_pin, int8_t rst_pin, int8_t int_pin)
    : _sda(sda_pin), _scl(scl_pin), _rst(rst_pin), _int(int_pin) {}

void CST816D::begin(void) {
  if (_sda != -1 && _scl != -1) {
    Wire1.begin(_sda, _scl, 400000);
  } else {
    Wire1.begin();
  }
  Wire1.setTimeOut(20);

  if (_int != -1) {
    pinMode(_int, OUTPUT);
    digitalWrite(_int, HIGH);
    delay(1);
    digitalWrite(_int, LOW);
    delay(1);
    pinMode(_int, INPUT_PULLUP);
  }

  if (_rst != -1) {
    pinMode(_rst, OUTPUT);
    digitalWrite(_rst, LOW);
    delay(10);
    digitalWrite(_rst, HIGH);
    delay(120);
  }

  // Disable auto-sleep so touch responds immediately
  i2c_write(0xFE, 0xFF);
}

bool CST816D::getTouch(uint16_t *x, uint16_t *y, uint8_t *gesture) {
  uint8_t fingers = 0;
  if (!i2c_read(0x02, &fingers) || fingers == 0) {
    *gesture = GESTURE_NONE;
    return false;
  }

  uint8_t g = 0;
  if (i2c_read(0x01, &g)) {
    if (g == GESTURE_SLIDE_UP || g == GESTURE_SLIDE_DOWN ||
        g == GESTURE_SLIDE_LEFT || g == GESTURE_SLIDE_RIGHT) {
      *gesture = g;
    } else {
      *gesture = GESTURE_NONE;
    }
  } else {
    *gesture = GESTURE_NONE;
  }

  uint8_t data[4] = {0};
  if (!i2c_read_continuous(0x03, data, 4)) {
    return false;
  }

  *x = ((uint16_t)(data[0] & 0x0F) << 8) | data[1];
  *y = ((uint16_t)(data[2] & 0x0F) << 8) | data[3];
  return true;
}

bool CST816D::i2c_read(uint8_t addr, uint8_t *out) {
  for (int attempt = 0; attempt < 2; attempt++) {
    Wire1.beginTransmission(I2C_ADDR_CST816D);
    Wire1.write(addr);
    if (Wire1.endTransmission(false) == 0) {
      if (Wire1.requestFrom((uint8_t)I2C_ADDR_CST816D, (uint8_t)1) == 1) {
        *out = Wire1.read();
        return true;
      }
    }
  }
  return false;
}

bool CST816D::i2c_read_continuous(uint8_t addr, uint8_t *data, uint32_t length) {
  Wire1.beginTransmission(I2C_ADDR_CST816D);
  Wire1.write(addr);
  if (Wire1.endTransmission(true) != 0) return false;

  uint32_t got = Wire1.requestFrom((uint8_t)I2C_ADDR_CST816D, (uint8_t)length);
  if (got < length) return false;

  for (uint32_t i = 0; i < length; i++) {
    data[i] = Wire1.read();
  }
  return true;
}

void CST816D::i2c_write(uint8_t addr, uint8_t data) {
  Wire1.beginTransmission(I2C_ADDR_CST816D);
  Wire1.write(addr);
  Wire1.write(data);
  Wire1.endTransmission();
}
