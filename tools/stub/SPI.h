#pragma once
#include <Arduino.h>
class SPIClass { public: void begin(int,int,int,int);
  uint8_t transfer(uint8_t); void transferBytes(const uint8_t*,uint8_t*,size_t); };
extern SPIClass SPI;
