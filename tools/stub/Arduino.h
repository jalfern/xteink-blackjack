#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define HIGH 1
#define LOW 0
#define INPUT 0
#define INPUT_PULLUP 2
#define OUTPUT 1
#define ADC_11db 3
#define LED_BUILTIN 2
typedef uint8_t byte;
void pinMode(int,int); void digitalWrite(int,int); int digitalRead(int);
void delay(unsigned long); void delayMicroseconds(unsigned int);
unsigned long millis(); unsigned long micros();
int analogRead(int); void analogSetAttenuation(int);
struct SerialCls { void begin(unsigned long); void printf(const char*,...);
  void println(const char*); }; extern SerialCls Serial;
