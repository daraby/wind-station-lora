# Wiring Notes (ESP32 DevKit)

SPI (shared):
- SCK  -> GPIO18
- MISO -> GPIO19
- MOSI -> GPIO23

LoRa (SX1276/78):
- CS   -> GPIO5
- RST  -> GPIO14
- DIO0 -> GPIO26

SD Card:
- CS   -> GPIO13

RTC DS3231 (I2C):
- SDA  -> GPIO21
- SCL  -> GPIO22

Wind sensors (example):
- Speed pulse -> GPIO34 (interrupt)
- Direction ADC -> GPIO35
