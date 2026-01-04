/*
Wind Station - ESP32 + LoRa (P2P) + SD + RTC
- Samples wind every 10 seconds
- Aggregates to 1-minute values (avg/gust/dir vector-avg)
- Sends one LoRa packet per minute
- Store & Forward: logs to SD, uses ACK + backfill on gaps

NOTE: You MUST calibrate:
1) speed_from_pulses() according to your anemometer datasheet
2) dir_from_adc() according to your wind vane voltage steps
3) battery reading (add divider + ADC pin)
*/

#include <SPI.h>
#include <LoRa.h>
#include <SD.h>
#include <Wire.h>
#include "RTClib.h"
#include <math.h>

static const int LORA_CS   = 5;
static const int LORA_RST  = 14;
static const int LORA_DIO0 = 26;

static const int SD_CS     = 13;

static const int WIND_PULSE_PIN = 34;
static const int WIND_DIR_ADC   = 35;
static const int BAT_ADC_PIN    = 32;

static const long LORA_FREQ = 868300000; // 868.3 MHz
static const uint8_t NODE_ID = 1;

static const uint32_t SAMPLE_PERIOD_MS = 10000;
static const uint32_t TX_PERIOD_MS     = 60000;

static const uint8_t  BACKFILL_MAX_FRAMES_PER_CYCLE = 6;
static const uint32_t ACK_WAIT_MS = 900;

RTC_DS3231 rtc;

volatile uint32_t wind_pulse_count = 0;
uint32_t seq_counter = 0;

struct Sample { float speed_knots; float dir_deg; };
Sample samples[6];
uint8_t sample_idx = 0;

uint32_t last_sample_ms = 0;
uint32_t last_tx_ms = 0;

uint32_t last_ack_seq = 0;
bool sd_ok = false;

static uint16_t crc16_ccitt(const uint8_t* data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= (uint16_t)data[i] << 8;
    for (int j = 0; j < 8; j++) {
      crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
    }
  }
  return crc;
}

static uint32_t unix_now() {
  if (!rtc.begin()) return (uint32_t)(millis() / 1000);
  DateTime now = rtc.now();
  return (uint32_t)now.unixtime();
}

static float dir_from_adc(int adc) {
  return (adc * 360.0f) / 4095.0f; // placeholder: replace with LUT
}

static float speed_from_pulses(uint32_t pulses_in_10s) {
  float hz = pulses_in_10s / 10.0f;
  float kmh = hz * 2.4f; // placeholder: replace per datasheet
  return kmh * 0.539957f;
}

static uint16_t read_battery_mv() {
  return 0; // placeholder: implement with divider + ADC
}

static void IRAM_ATTR on_wind_pulse() { wind_pulse_count++; }

static float avg_dir_deg(const Sample* s, uint8_t n) {
  float sum_sin = 0, sum_cos = 0;
  for (uint8_t i = 0; i < n; i++) {
    float rad = s[i].dir_deg * 3.1415926f / 180.0f;
    sum_sin += sinf(rad);
    sum_cos += cosf(rad);
  }
  float mean = atan2f(sum_sin, sum_cos) * 180.0f / 3.1415926f;
  if (mean < 0) mean += 360.0f;
  return mean;
}

static float avg_speed_knots(const Sample* s, uint8_t n) {
  float sum = 0;
  for (uint8_t i = 0; i < n; i++) sum += s[i].speed_knots;
  return sum / (float)n;
}

static float max_speed_knots(const Sample* s, uint8_t n) {
  float mx = 0;
  for (uint8_t i = 0; i < n; i++) if (s[i].speed_knots > mx) mx = s[i].speed_knots;
  return mx;
}

static void sd_log(uint32_t ts, uint32_t seq, float avgK, float gustK, float dirD, uint16_t batmV) {
  if (!sd_ok) return;
  File f = SD.open("/wind.csv", FILE_APPEND);
  if (!f) return;
  f.printf("%lu,%lu,%.1f,%.1f,%.0f,%u\n", (unsigned long)ts, (unsigned long)seq, avgK, gustK, dirD, batmV);
  f.close();
}

static bool sd_get_frame_by_seq(uint32_t target_seq, String& out_csv_line) {
  if (!sd_ok) return false;
  File f = SD.open("/wind.csv", FILE_READ);
  if (!f) return false;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    int p1 = line.indexOf(',');
    if (p1 < 0) continue;
    int p2 = line.indexOf(',', p1 + 1);
    if (p2 < 0) continue;
    uint32_t seq = (uint32_t)line.substring(p1 + 1, p2).toInt();
    if (seq == target_seq) { out_csv_line = line; f.close(); return true; }
  }
  f.close();
  return false;
}

static void send_frame(uint8_t msg_type, uint32_t ts, uint32_t seq, float avgK, float gustK, float dirD, uint16_t batmV) {
  uint8_t buf[32];
  size_t idx = 0;
  auto put8  = [&](uint8_t v){ buf[idx++] = v; };
  auto put16 = [&](uint16_t v){ buf[idx++] = (v >> 8) & 0xFF; buf[idx++] = v & 0xFF; };
  auto put32 = [&](uint32_t v){ buf[idx++] = (v >> 24) & 0xFF; buf[idx++] = (v >> 16) & 0xFF; buf[idx++] = (v >> 8) & 0xFF; buf[idx++] = v & 0xFF; };

  put8(NODE_ID);
  put8(msg_type);
  put32(seq);
  put32(ts);

  uint16_t avg_x10  = (uint16_t)max(0, (int)lroundf(avgK * 10.0f));
  uint16_t gust_x10 = (uint16_t)max(0, (int)lroundf(gustK * 10.0f));
  uint16_t dir_deg  = (uint16_t)lroundf(dirD);

  put16(avg_x10);
  put16(gust_x10);
  put16(dir_deg);
  put16(batmV);

  uint8_t flags = 0;
  put8(flags);

  uint16_t crc = crc16_ccitt(buf, idx);
  put16(crc);

  LoRa.beginPacket();
  LoRa.write(buf, idx);
  LoRa.endPacket();
}

static bool wait_for_ack(uint32_t* out_last_seq) {
  uint32_t start = millis();
  while (millis() - start < ACK_WAIT_MS) {
    int psize = LoRa.parsePacket();
    if (psize <= 0) continue;
    uint8_t rb[16];
    int n = 0;
    while (LoRa.available() && n < (int)sizeof(rb)) rb[n++] = (uint8_t)LoRa.read();
    if (n < 8) continue;

    uint16_t got_crc = ((uint16_t)rb[n-2] << 8) | rb[n-1];
    uint16_t calc_crc = crc16_ccitt(rb, n - 2);
    if (got_crc != calc_crc) continue;

    uint8_t node = rb[0];
    uint8_t type = rb[1];
    if (node != NODE_ID || type != 100) continue;

    uint32_t last_seq = ((uint32_t)rb[2] << 24) | ((uint32_t)rb[3] << 16) | ((uint32_t)rb[4] << 8) | rb[5];
    *out_last_seq = last_seq;
    return true;
  }
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(WIND_PULSE_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(WIND_PULSE_PIN), on_wind_pulse, RISING);

  SPI.begin(18, 19, 23, SD_CS);
  sd_ok = SD.begin(SD_CS);
  Serial.printf("SD: %s\n", sd_ok ? "OK" : "FAIL");

  Wire.begin(21, 22);
  rtc.begin();

  LoRa.setPins(LORA_CS, LORA_RST, LORA_DIO0);
  if (!LoRa.begin(LORA_FREQ)) {
    Serial.println("LoRa init failed!");
    while (true) delay(1000);
  }
  LoRa.setSpreadingFactor(10);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  LoRa.enableCrc();

  last_sample_ms = millis();
  last_tx_ms = millis();
}

void loop() {
  uint32_t now_ms = millis();

  if (now_ms - last_sample_ms >= SAMPLE_PERIOD_MS) {
    last_sample_ms += SAMPLE_PERIOD_MS;

    noInterrupts();
    uint32_t pulses = wind_pulse_count;
    wind_pulse_count = 0;
    interrupts();

    float speedK = speed_from_pulses(pulses);
    int adc = analogRead(WIND_DIR_ADC);
    float dirD = dir_from_adc(adc);

    if (sample_idx < 6) samples[sample_idx++] = { speedK, dirD };
    Serial.printf("Sample: %.1f kn, %.0f deg\n", speedK, dirD);
  }

  if (now_ms - last_tx_ms >= TX_PERIOD_MS && sample_idx >= 6) {
    last_tx_ms += TX_PERIOD_MS;

    float avgK = avg_speed_knots(samples, 6);
    float gustK = max_speed_knots(samples, 6);
    float dirD  = avg_dir_deg(samples, 6);

    uint32_t ts = unix_now();
    uint16_t batmV = read_battery_mv();

    seq_counter++;

    sd_log(ts, seq_counter, avgK, gustK, dirD, batmV);
    send_frame(1, ts, seq_counter, avgK, gustK, dirD, batmV);

    uint32_t ack_seq = 0;
    bool got = wait_for_ack(&ack_seq);
    if (got) last_ack_seq = ack_seq;

    if (got && ack_seq + 1 < seq_counter) {
      uint8_t sent = 0;
      for (uint32_t s = ack_seq + 1; s < seq_counter && sent < BACKFILL_MAX_FRAMES_PER_CYCLE; s++) {
        String line;
        if (!sd_get_frame_by_seq(s, line)) continue;

        int a = line.indexOf(',');
        int b = line.indexOf(',', a+1);
        int c = line.indexOf(',', b+1);
        int d = line.indexOf(',', c+1);
        int e = line.indexOf(',', d+1);

        uint32_t ts2  = (uint32_t)line.substring(0,a).toInt();
        uint32_t seq2 = (uint32_t)line.substring(a+1,b).toInt();
        float avg2    = line.substring(b+1,c).toFloat();
        float gust2   = line.substring(c+1,d).toFloat();
        float dir2    = line.substring(d+1,e).toFloat();
        uint16_t bat2 = (uint16_t)line.substring(e+1).toInt();

        send_frame(2, ts2, seq2, avg2, gust2, dir2, bat2);
        sent++;
        delay(120);
      }
    }

    sample_idx = 0;
  }

  delay(10);
}
