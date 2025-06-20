#include "lis3dh.h"

#define LIS_ENABLED 1
#if LIS_ENABLED

#include <SPI.h>

LIS3DH::LIS3DH() :
_spi(LIS_CS, LIS_SCK, LIS_MISO, LIS_MOSI, 1000000U, SPI_BITORDER_MSBFIRST, SPI_MODE3),
_lis((int8_t)LIS_CS)
{
  _int_pin = 0;
}

bool LIS3DH::Init(uint8_t int_pin) {
  _int_pin = int_pin;
  
  Serial.println("Starting LIS");
  if (!_lis.begin()) {
    Serial.println("Could not start LIS");
    return false;
  }
  Serial.println("LIS Found");

  // disable the data ready interrupt since we don't need it to wake up
  _lis.enableDRDY(false, 1);

  _lis.setRange(LIS3DH_RANGE_2_G);

  Serial.print("Range = ");
  Serial.print(2 << _lis.getRange());
  Serial.println("G");

  // 0 = turn off click detection & interrupt
  // 1 = single click only interrupt output
  // 2 = double click only interrupt output, detect single click
  // Adjust threshhold, higher numbers are less sensitive
  _lis.setClick(1, 80);
  delay(100);
  ClearInterruptSource();

  if (_int_pin != 0) {
    pinMode(_int_pin, INPUT);
  }

  return true;
}

uint8_t LIS3DH::ClearInterruptSource() {
  if (_int_pin == 0)
    return 0;

  return Process();
}

uint8_t LIS3DH::Process(uint8_t waitTime) {
  uint8_t click_source = _lis.getClick();
  if (click_source == 0) return false;
  if (!(click_source & 0x30)) return false;
  Serial.print("Click detected (0x");
  Serial.print(click_source, HEX);
  Serial.print("): ");
  if (click_source & 0x10) Serial.print(" single click");
  if (click_source & 0x20) Serial.print(" double click");
  Serial.println();

  if (waitTime > 0)
    delay(waitTime);

  return click_source;
}

bool LIS3DH::ReadAcceleration(AccelData& data) {
  // Read normalized acceleration values (-1.0 to +1.0 g)
  _lis.read();
  data.x = _lis.x_g;
  data.y = _lis.y_g;
  data.z = _lis.z_g;
  
  return true;
}

sensors_event_t LIS3DH::GetEvent() {
  sensors_event_t event;
  _lis.getEvent(&event);
  return event;
}

#endif // LIS_ENABLED
