// Библиотека HardwareSerial для работы с UART
#include <HardwareSerial.h>

// Создаем объект для UART1
HardwareSerial TOFSerial(1);

void setup() {
  Serial.begin(115200); // Для отладки в Serial Monitor
  delay(100);
  
  Serial.println("Init");

  // Инициализируем UART1 для датчика
  // Параметры: скорость, режим, RX пин, TX пин
  TOFSerial.begin(115200, SERIAL_8N1, 5, 4); 
  
  // ... остальная инициализация
}

void loop() {
  // Чтение данных от датчика
  if (TOFSerial.available() > 0) {
    String data = TOFSerial.readStringUntil('\n'); // или другой разделитель
    Serial.print("Расстояние: ");
    Serial.println(data);
  }
  delay(100);
}