#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <BLE2901.h>

#include <Arduino.h>
#include <Wire.h>

#include "HX711.h"                                            // подключаем библиотеку для работы с тензодатчиком
#include "TOF_Sense.h"

#define SERVICE_UUID        "4e292be3-71e6-4ca1-a06e-b1b3660df15c"
#define CHARACTERISTIC_UUID "d65ce50e-3baa-4405-b561-39980883f500"
#define NOTIFY_UUID "087dde8f-83e6-4f77-b9b0-7e966a7706a8"

#define DT  3                                                // Указываем номер вывода, к которому подключен вывод DT датчика
#define SCK 2                                                // Указываем номер вывода, к которому подключен вывод SCK датчика

HX711 scale;                                                  // создаём объект scale

float calibration_factor = 1;                            // вводим калибровочный коэффициент
float units;                                                  // задаём переменную для измерений в граммах
float ounces;                                                 // задаём переменную для измерений в унциях

BLECharacteristic *pCharacteristic;
BLECharacteristic *pSend;
BLE2901 *descriptor_2901 = NULL;
bool deviceConnected = false;
String clientAddress = "";  // Переменная для хранения ID клиента
int clientIdCounter = 0;    // Счетчик подключений

// Переменные для адаптивной задержки (расстояние)
unsigned long lastDistanceChangeTime = 0;
unsigned long lastDistanceValue = 0;
int currentDelay = 1000;  // Начальная задержка 1000 мс
bool isFastMode = false;  // Флаг быстрого режима по расстоянию

// Переменные для адаптивной задержки (вес)
unsigned long lastWeightChangeTime = 0;
float lastWeightValue = 0;
bool isWeightFastMode = false;  // Флаг быстрого режима по весу

class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      String value = pCharacteristic->getValue();

      if (value.length() > 0) {
        Serial.print("📨 Получена команда от клиента ");
        Serial.print(clientAddress);
        Serial.print(": ");
        Serial.println(value);
      }
    }
};

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *pServer) {
    deviceConnected = true;
    
    // Присваиваем уникальный ID для каждого подключения
    clientIdCounter++;
    clientAddress = "Client_" + String(clientIdCounter);
    
    Serial.println("========================================");
    Serial.println("✅ Bluetooth клиент подключен!");
    Serial.print("📱 ID клиента: ");
    Serial.println(clientAddress);
    
    // Получаем дескриптор CCCD и проверяем подписку
    BLE2902 *cccd = (BLE2902*)pCharacteristic->getDescriptorByUUID(BLEUUID((uint16_t)0x2902));
    if (cccd) {
      // Получаем значение как массив байтов
      uint8_t* cccdValuePtr = cccd->getValue();
      uint16_t cccdValue = 0;
      
      // Если есть данные, преобразуем их в uint16_t
      if (cccd->getLength() >= 2) {
        cccdValue = cccdValuePtr[0] | (cccdValuePtr[1] << 8);
      }
      
      Serial.print("📊 Статус подписки: ");
      if (cccdValue & 0x0001) {
        Serial.println("NOTIFICATION (0x0001)");
      } else if (cccdValue & 0x0002) {
        Serial.println("INDICATION (0x0002)");
      } else {
        Serial.println("Не подписан (0x0000)");
      }
    }
    
    Serial.println("📱 Устройство готово к приему данных");
    Serial.println("========================================");
  };

  void onDisconnect(BLEServer *pServer) {
    deviceConnected = false;
    Serial.println("========================================");
    Serial.println("❌ Bluetooth клиент отключен");
    Serial.print("📱 ID отключившегося клиента: ");
    Serial.println(clientAddress);
    Serial.println("🔄 Ожидание нового подключения...");
    Serial.println("========================================");
    
    clientAddress = "";  // Очищаем адрес клиента
    
    // Перезапускаем рекламу для новых подключений
    BLEDevice::startAdvertising();
    Serial.println("📡 Реклама перезапущена");
  }
};

void BLE_Start() {
  // Очищаем память BLE перед инициализацией
  BLEDevice::deinit(true);
  delay(100);
  
  BLEDevice::init("Measuring PSS");
  Serial.println("🔵 BLE инициализирован");

  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  // ОТКЛЮЧАЕМ БЕЗОПАСНОСТЬ
  BLESecurity *pSecurity = new BLESecurity();
  pSecurity->setAuthenticationMode(ESP_LE_AUTH_NO_BOND);
  pSecurity->setCapability(ESP_IO_CAP_NONE);

  BLEService *pService = pServer->createService(SERVICE_UUID);

  // Характеристика для записи/чтения
  BLECharacteristic *pWriteChar = pService->createCharacteristic(
                                    CHARACTERISTIC_UUID,
                                    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE
                                  );
  pWriteChar->addDescriptor(new BLE2902());
  pWriteChar->setCallbacks(new MyCallbacks());

  // Характеристика для уведомлений с УВЕЛИЧЕННЫМ размером MTU
  pCharacteristic = pService->createCharacteristic(
                        NOTIFY_UUID,
                        BLECharacteristic::PROPERTY_NOTIFY
                      );
  
  // Добавляем CCCD дескриптор
  BLE2902 *cccdDescriptor = new BLE2902();
  cccdDescriptor->setNotifications(true);
  pCharacteristic->addDescriptor(cccdDescriptor);
  
  // Добавляем дескриптор с описанием
  descriptor_2901 = new BLE2901();
  descriptor_2901->setDescription("Sensor Data");
  descriptor_2901->setAccessPermissions(ESP_GATT_PERM_READ);
  pCharacteristic->addDescriptor(descriptor_2901);

  pService->start();
  Serial.println("📋 Сервис создан и запущен");

  // Настройка рекламы с увеличенным MTU
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);  // Увеличиваем MTU
  pAdvertising->setMaxPreferred(0x12);
  
  BLEDevice::startAdvertising();
  Serial.println("📡 Реклама запущена. Имя устройства: Measuring PPS");
}

void setup() {
  Serial.begin(115200);
  
  // Специальная настройка для ESP32-C3
  #ifdef ARDUINO_ESP32C3_DEV
    // Для ESP32-C3 нужно увеличить стек BLE
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));
  #endif

  Serial.println("========================================");
  Serial.println("🚀 ESP32-C3 BLE + Датчики");
  Serial.println("========================================");
  Serial.println("Запуск инициализации...");

  BLE_Start();

  Serial.print("⚙️ Инициализация тензодатчика (HX711)...");
  scale.begin(DT, SCK);
  scale.set_scale();
  scale.tare();
  scale.set_scale(calibration_factor);
  Serial.println(" ✓");

  Serial.print("⚙️ Инициализация I2C (TOF датчик)...");
  Wire.begin();
  Wire.setClock(400000);
  Serial.println(" ✓");

  Serial.println("========================================");
  Serial.println("✅ Система полностью инициализирована!");
  Serial.println("📱 Подключитесь к устройству 'Measuring PSS'");
  Serial.println("========================================");
  
  // Инициализация переменных для адаптивной задержки (расстояние)
  TOF_Parameter initialTOF = TOF_Read();
  lastDistanceValue = initialTOF.dis;
  lastDistanceChangeTime = millis();
  
  // Инициализация переменных для адаптивной задержки (вес)
  float initialWeightSum = 0;
  for (int i = 0; i < 5; i++) {
    initialWeightSum += scale.get_units();
    delay(10);
  }
  lastWeightValue = (initialWeightSum / 5) * 0.035274;  // Конвертируем в унции
  lastWeightChangeTime = millis();
  
  Serial.print("📊 Начальные значения: Вес = ");
  Serial.print(lastWeightValue);
  Serial.print(" унций, Расстояние = ");
  Serial.print(lastDistanceValue);
  Serial.println(" мм");
  Serial.println("========================================");
}

TOF_Parameter TOF_Read()
{
    uint8_t read_buf[256];
    TOF_Parameter TOF_0;

    //UNO R3 cannot read all the data at once, so it reads the data twice.
    I2C_Read_Nbyte(0x00, read_buf, TOF_REGISTER_TOTAL_SIZE/2);
    I2C_Read_Nbyte(TOF_REGISTER_TOTAL_SIZE/2, &read_buf[TOF_REGISTER_TOTAL_SIZE/2], TOF_REGISTER_TOTAL_SIZE/2);

    TOF_0.interface_mode = read_buf[TOF_ADDR_MODE] & 0x07;

    TOF_0.id = read_buf[TOF_ADDR_ID];

    TOF_0.uart_baudrate = (unsigned long)(((unsigned long)read_buf[TOF_ADDR_UART_BAUDRATE + 3] << 24) | ((unsigned long)read_buf[TOF_ADDR_UART_BAUDRATE + 2] << 16) |
                           ((unsigned long)read_buf[TOF_ADDR_UART_BAUDRATE + 1] << 8) | (unsigned long)read_buf[TOF_ADDR_UART_BAUDRATE]);

    TOF_0.system_time = (unsigned long)(((unsigned long)read_buf[TOF_ADDR_SYSTEM_TIME + 3] << 24) | ((unsigned long)read_buf[TOF_ADDR_SYSTEM_TIME + 2] << 16) |
                         ((unsigned long)read_buf[TOF_ADDR_SYSTEM_TIME + 1] << 8) | (unsigned long)read_buf[TOF_ADDR_SYSTEM_TIME]);

    TOF_0.dis = (unsigned long)(((unsigned long)read_buf[TOF_ADDR_DIS + 3] << 24) | ((unsigned long)read_buf[TOF_ADDR_DIS + 2] << 16) |
                 ((unsigned long)read_buf[TOF_ADDR_DIS + 1] << 8) | (unsigned long)read_buf[TOF_ADDR_DIS]);

    TOF_0.dis_status = ((read_buf[TOF_ADDR_DIS_STATUS]) | (read_buf[TOF_ADDR_DIS_STATUS + 1] << 8));

    TOF_0.signal_strength = ((read_buf[TOF_ADDR_SIGNAL_STRENGTH]) | (read_buf[TOF_ADDR_SIGNAL_STRENGTH + 1] << 8));
    TOF_0.range_precision = read_buf[TOF_ADDR_RANGE_PRECISION];  

    return TOF_0;
}

// Новая функция, возвращающая данные и оба параметра
String getDataWithWeightAndDistance(unsigned long &distance, float &weight) {
  // Измеряем вес (усреднение)
  float weightSum = 0;
  for (int i = 0; i < 3; i++) {
    weightSum += scale.get_units();
    delay(10);
  }
  weight = (weightSum / 3) * 0.035274;  // Конвертируем в унции
  
  // Измеряем расстояние
  TOF_Parameter TOF_0 = TOF_Read();
  distance = TOF_0.dis;
  
  return "{\"Weight\": " + String(weight) + ", \"Distance\": " + String(distance) + "}";
}

// Функция для обновления адаптивной задержки с учетом веса и расстояния
int updateAdaptiveDelay(unsigned long currentDistance, float currentWeight, 
                        unsigned long &lastDistance, float &lastWeight,
                        unsigned long &lastChangeTime, bool &fastMode,
                        unsigned long &lastWeightChangeTime, bool &weightFastMode) {
  unsigned long currentTime = millis();
  bool anyChange = false;
  
  // === Проверка изменения РАССТОЯНИЯ (> 5 мм) ===
  long distanceChange = abs((long)currentDistance - (long)lastDistance);
  if (distanceChange > 5) {
    fastMode = true;
    lastChangeTime = currentTime;
    lastDistance = currentDistance;
    anyChange = true;
    
    if (currentDelay != 20) {
      currentDelay = 20;
      Serial.println("⚡ Быстрый режим: задержка 20 мс (изменение расстояния > 5 мм)");
    }
  }
  
  // === Проверка изменения ВЕСА (> 500 грамм или 17.637 унций) ===
  float weightChange = abs(currentWeight - lastWeight);
  if (weightChange > 17.637) {  // 500 грамм в унциях
    weightFastMode = true;
    lastWeightChangeTime = currentTime;
    lastWeight = currentWeight;
    anyChange = true;
    
    if (currentDelay != 20) {
      currentDelay = 20;
      Serial.println("⚡ Быстрый режим: задержка 20 мс (изменение веса > 500 г)");
    }
  }
  
  // === Проверка быстрого режима по расстоянию ===
  if (fastMode) {
    unsigned long timeSinceLastChange = currentTime - lastChangeTime;
    if (timeSinceLastChange >= 60000) {
      fastMode = false;
      Serial.println("⏱️ Таймаут быстрого режима по расстоянию истек");
    }
  }
  
  // === Проверка быстрого режима по весу ===
  if (weightFastMode) {
    unsigned long timeSinceWeightChange = currentTime - lastWeightChangeTime;
    if (timeSinceWeightChange >= 60000) {
      weightFastMode = false;
      Serial.println("⏱️ Таймаут быстрого режима по весу истек");
    }
  }
  
  // === Определение итоговой задержки ===
  // Если активен хотя бы один быстрый режим
  if (fastMode || weightFastMode) {
    // Обновляем lastDistance и lastWeight только если они не были обновлены
    if (!anyChange) {
      lastDistance = currentDistance;
      lastWeight = currentWeight;
    }
    return 20;
  }
  
  // === Медленный режим ===
  lastDistance = currentDistance;
  lastWeight = currentWeight;
  
  // Возвращаем в медленный режим только если оба таймаута истекли
  if (currentDelay != 1000) {
    currentDelay = 1000;
    Serial.println("💤 Медленный режим: задержка 1000 мс (нет изменений > 1 минуты)");
  }
  
  return 1000;
}

String getData() {
  for (int i = 0; i < 5; i ++) {
    units = + scale.get_units(), 10;
  }
  units = units / 5;
  ounces = units * 0.035274;                                  
  
  TOF_Parameter TOF_0 = TOF_Read();

  return "{\"Weight\": " + String(ounces) + ", \"Distance\": " + String(TOF_0.dis) + "}";
}

String getDataWithDistance(unsigned long &distance) {
  for (int i = 0; i < 3; i ++) {
    units = + scale.get_units(), 10;
  }
  units = units / 3;
  ounces = units * 0.035274;

  TOF_Parameter TOF_0 = TOF_Read();
  distance = TOF_0.dis;

  return "{\"Weight\": " + String(ounces) + ", \"Distance\": " + String(TOF_0.dis) + "}";
}

void sendDataInChunks(String data) {
  const int CHUNK_SIZE = 18; // Безопасный размер для отправки
  int totalLen = data.length();
  
  // Если данные короткие, отправляем сразу
  if (totalLen <= CHUNK_SIZE) {
    pCharacteristic->setValue(data);
    pCharacteristic->notify();
    return;
  }
  
  // Разбиваем на части
  int chunks = (totalLen + CHUNK_SIZE - 1) / CHUNK_SIZE;
  for (int i = 0; i < chunks; i++) {
    int start = i * CHUNK_SIZE;
    int end = min((i + 1) * CHUNK_SIZE, totalLen);
    String chunk = data.substring(start, end);
    
    pCharacteristic->setValue(chunk);
    pCharacteristic->notify();
    delay(20); // Небольшая задержка
  }
}

void loop() {
  static int sendCount = 0;
  unsigned long currentDistance = 0;
  float currentWeight = 0;
  
  // Получаем данные с датчиков
  String dataToSend = getDataWithWeightAndDistance(currentDistance, currentWeight);
  
  // Обновляем адаптивную задержку с учетом веса и расстояния
  int delayTime = updateAdaptiveDelay(
    currentDistance, currentWeight,
    lastDistanceValue, lastWeightValue,
    lastDistanceChangeTime, isFastMode,
    lastWeightChangeTime, isWeightFastMode
  );
  
  if (deviceConnected) {
    // Проверяем подписку клиента
    BLE2902 *cccd = (BLE2902*)pCharacteristic->getDescriptorByUUID(BLEUUID((uint16_t)0x2902));
    bool isSubscribed = false;
    
    if (cccd) {
      uint8_t* cccdValuePtr = cccd->getValue();
      uint16_t cccdValue = 0;
      
      if (cccd->getLength() >= 2) {
        cccdValue = cccdValuePtr[0] | (cccdValuePtr[1] << 8);
      }
      
      isSubscribed = (cccdValue & 0x0001) || (cccdValue & 0x0002);
    }
    
    if (isSubscribed) {
      sendDataInChunks(dataToSend);
      sendCount++;
      
      Serial.print("📤 [#");
      Serial.print(sendCount);
      Serial.print("] Задержка: ");
      Serial.print(delayTime);
      Serial.print("мс | Вес: ");
      Serial.print(currentWeight);
      Serial.print(" унций | Расст: ");
      Serial.print(currentDistance);
      Serial.println(" мм");
    } else {
      Serial.println("⏸️ Клиент не подписан на уведомления");
    }
  } else {
    Serial.print("⏸️ [ОЖИДАНИЕ] Задержка: ");
    Serial.print(delayTime);
    Serial.print("мс | Вес: ");
    Serial.print(currentWeight);
    Serial.print(" унций | Расст: ");
    Serial.print(currentDistance);
    Serial.println(" мм");
  }
  
  delay(delayTime);
}