#ifndef LIS3DH_H
#define LIS3DH_H

#include <Arduino.h>
#include <Adafruit_LIS3DH.h>
#include <Adafruit_Sensor.h>

#define LIS3DH_DEFAULT_INT_PIN 26

struct AccelData {
  float x;
  float y; 
  float z;
};

class LIS3DH {
public:
  LIS3DH();
  bool Init(uint8_t int_pin = LIS3DH_DEFAULT_INT_PIN);
  uint8_t ClearInterruptSource();
  uint8_t Process(uint8_t waitTime_ms = 0);
  
  // New methods for 3-axis data
  bool ReadAcceleration(AccelData& data);
  sensors_event_t GetEvent();

private:
  uint8_t _int_pin;
  Adafruit_SPIDevice _spi;
  Adafruit_LIS3DH _lis;
};

#endif // LIS3DH_H
