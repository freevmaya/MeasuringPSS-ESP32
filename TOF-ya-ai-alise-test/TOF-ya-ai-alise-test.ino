#include <HardwareSerial.h>

HardwareSerial tof(1);
#define TOF_RX 20   // сюда идёт TX датчика
#define TOF_TX 21   // сюда идёт RX датчика

uint8_t frame[16];
float   distance_m = 0;
uint8_t dist_status = 0;   // 1 = измерение валидное, 0 = нет
uint16_t signal_strength = 0;

void setup() {
  Serial.begin(115200);
  // UART датчика на 921600, дефолтные настройки 8N1
  tof.begin(921600, SERIAL_8N1, TOF_RX, TOF_TX);
}

void loop() {
  // ждём начало кадра: 0x57 0x00
  if (tof.available() < 16) return;

  uint8_t b = tof.peek();
  if (b != 0x57) { tof.read(); return; }

  tof.readBytes(frame, 16);
  if (frame[1] != 0x00) return;          // не кадр измерений

  uint8_t checksum = 0;
  for (int i = 0; i < 15; i++) checksum += frame[i];
  if (checksum != frame[15]) return;     // битый кадр — пропускаем

  uint8_t id = frame[3];
  // расстояние в мм, 3 байта, little-endian (байты 8..10)
  uint32_t dis_mm = frame[8] | (frame[9] << 8) | ((uint32_t)frame[10] << 16);
  dist_status = frame[11];
  signal_strength = frame[12] | (frame[13] << 8);

  distance_m = dis_mm / 1000.0;

  Serial.printf("id:%u  dist: %.3f m  status:%u  signal:%u\n",
                id, distance_m, dist_status, signal_strength);
}
