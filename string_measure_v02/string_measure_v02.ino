#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include <BLE2901.h>

#include <Arduino.h>
#include <Wire.h>

#include "HX711.h"
#include "TOF_Sense.h"

#define SERVICE_UUID        "4e292be3-71e6-4ca1-a06e-b1b3660df15c"
#define CHARACTERISTIC_UUID "d65ce50e-3baa-4405-b561-39980883f500"
#define NOTIFY_UUID "087dde8f-83e6-4f77-b9b0-7e966a7706a8"

#define DT  3
#define SCK 2

#define MAX_DELAY 1000
#define MIN_DELAY 1000

HX711 scale;

float calibration_factor = 1;
float units;
float ounces;

BLECharacteristic *pCharacteristic;
BLECharacteristic *pSend;
BLE2901 *descriptor_2901 = NULL;
bool deviceConnected = false;
String clientAddress = "";
int clientIdCounter = 0;

// Переменные для адаптивной задержки (расстояние)
unsigned long lastDistanceChangeTime = 0;
unsigned long lastDistanceValue = 0;
int currentDelay = MAX_DELAY;
bool isFastMode = false;

// Переменные для адаптивной задержки (вес)
unsigned long lastWeightChangeTime = 0;
float lastWeightValue = 0;
bool isWeightFastMode = false;

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

    clientIdCounter++;
    clientAddress = "Client_" + String(clientIdCounter);

    Serial.println("========================================");
    Serial.println("✅ Bluetooth клиент подключен!");
    Serial.print("📱 ID клиента: ");
    Serial.println(clientAddress);
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

    clientAddress = "";

    BLEDevice::startAdvertising();
    Serial.println("📡 Реклама перезапущена");
  }
};

void BLE_Start() {
  BLEDevice::deinit(true);
  delay(100);

  BLEDevice::init("Measuring PSS");
  Serial.println("🔵 BLE инициализирован");

  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

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

  // Характеристика для уведомлений
  pCharacteristic = pService->createCharacteristic(
                        NOTIFY_UUID,
                        BLECharacteristic::PROPERTY_NOTIFY
                      );

  // ИСПРАВЛЕНО: стандартный CCCD без кастомных колбэков.
  // Подписку будем проверять через pCharacteristic->getSubscribedCount().
  pCharacteristic->addDescriptor(new BLE2902());

  // Дескриптор с описанием (необязателен)
  descriptor_2901 = new BLE2901();
  descriptor_2901->setDescription("Sensor Data");
  descriptor_2901->setAccessPermissions(ESP_GATT_PERM_READ);
  pCharacteristic->addDescriptor(descriptor_2901);

  pService->start();
  Serial.println("📋 Сервис создан и запущен");

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMaxPreferred(0x12);

  BLEDevice::startAdvertising();
  Serial.println("📡 Реклама запущена. Имя устройства: Measuring PSS");
}

void setup() {
  Serial.begin(115200);

  #ifdef ARDUINO_ESP32C3_DEV
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

  // Инициализация переменных для адаптивной задержки (вес) — в граммах
  float initialWeightSum = 0;
  for (int i = 0; i < 5; i++) {
    initialWeightSum += scale.get_units();
    delay(10);
  }
  lastWeightValue = initialWeightSum / 5;
  lastWeightChangeTime = millis();

  Serial.print("📊 Начальные значения: Вес = ");
  Serial.print(lastWeightValue);
  Serial.print(" г, Расстояние = ");
  Serial.print(lastDistanceValue);
  Serial.println(" мм");
  Serial.println("========================================");
}

TOF_Parameter TOF_Read()
{
    uint8_t read_buf[256];
    TOF_Parameter TOF_0;

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

// Вес в граммах
String getDataWithWeightAndDistance(unsigned long &distance, float &weight) {
  float weightSum = 0;
  for (int i = 0; i < 3; i++) {
    weightSum += scale.get_units();
    delay(10);
  }
  weight = weightSum / 3;

  TOF_Parameter TOF_0 = TOF_Read();
  distance = TOF_0.dis;

  return "{\"Weight\": " + String(weight) + ", \"Distance\": " + String(distance) + "}";
}

int updateAdaptiveDelay(unsigned long currentDistance, float currentWeight,
                        unsigned long &lastDistance, float &lastWeight,
                        unsigned long &lastChangeTime, bool &fastMode,
                        unsigned long &lastWeightChangeTime, bool &weightFastMode) {
  unsigned long currentTime = millis();
  bool anyChange = false;

  long distanceChange = abs((long)currentDistance - (long)lastDistance);
  if (distanceChange > 5) {
    fastMode = true;
    lastChangeTime = currentTime;
    lastDistance = currentDistance;
    anyChange = true;
    if (currentDelay != MIN_DELAY) {
      currentDelay = MIN_DELAY;
      Serial.println("⚡ Быстрый режим (изменение расстояния > 5 мм)");
    }
  }

  // Порог изменения веса — 500 грамм
  float weightChange = abs(currentWeight - lastWeight);
  if (weightChange > 500.0) {
    weightFastMode = true;
    lastWeightChangeTime = currentTime;
    lastWeight = currentWeight;
    anyChange = true;
    if (currentDelay != MIN_DELAY) {
      currentDelay = MIN_DELAY;
      Serial.println("⚡ Быстрый режим (изменение веса > 500 г)");
    }
  }

  if (fastMode) {
    unsigned long timeSinceLastChange = currentTime - lastChangeTime;
    if (timeSinceLastChange >= 60000) {
      fastMode = false;
      Serial.println("⏱️ Таймаут быстрого режима по расстоянию истек");
    }
  }

  if (weightFastMode) {
    unsigned long timeSinceWeightChange = currentTime - lastWeightChangeTime;
    if (timeSinceWeightChange >= 60000) {
      weightFastMode = false;
      Serial.println("⏱️ Таймаут быстрого режима по весу истек");
    }
  }

  if (fastMode || weightFastMode) {
    if (!anyChange) {
      lastDistance = currentDistance;
      lastWeight = currentWeight;
    }
    return MIN_DELAY;
  }

  lastDistance = currentDistance;
  lastWeight = currentWeight;

  if (currentDelay != MAX_DELAY) {
    currentDelay = MAX_DELAY;
    Serial.println("💤 Медленный режим (нет изменений > 1 минуты)");
  }

  return MAX_DELAY;
}

String getData() {
  for (int i = 0; i < 5; i ++) {
    units = + scale.get_units(), 10;
  }
  units = units / 5;

  TOF_Parameter TOF_0 = TOF_Read();
  return "{\"Weight\": " + String(units) + ", \"Distance\": " + String(TOF_0.dis) + "}";
}

String getDataWithDistance(unsigned long &distance) {
  for (int i = 0; i < 3; i ++) {
    units = + scale.get_units(), 10;
  }
  units = units / 3;

  TOF_Parameter TOF_0 = TOF_Read();
  distance = TOF_0.dis;

  return "{\"Weight\": " + String(units) + ", \"Distance\": " + String(TOF_0.dis) + "}";
}

void sendDataInChunks(String data) {
  const int CHUNK_SIZE = 18;
  int totalLen = data.length();

  if (totalLen <= CHUNK_SIZE) {
    pCharacteristic->setValue(data);
    pCharacteristic->notify();
    return;
  }

  int chunks = (totalLen + CHUNK_SIZE - 1) / CHUNK_SIZE;
  for (int i = 0; i < chunks; i++) {
    int start = i * CHUNK_SIZE;
    int end = min((i + 1) * CHUNK_SIZE, totalLen);
    String chunk = data.substring(start, end);

    pCharacteristic->setValue(chunk);
    pCharacteristic->notify();
    delay(20);
  }
}

void loop() {
  static int sendCount = 0;
  unsigned long currentDistance = 0;
  float currentWeight = 0;

  String dataToSend = getDataWithWeightAndDistance(currentDistance, currentWeight);

  int delayTime = updateAdaptiveDelay(
    currentDistance, currentWeight,
    lastDistanceValue, lastWeightValue,
    lastDistanceChangeTime, isFastMode,
    lastWeightChangeTime, isWeightFastMode
  );

  if (deviceConnected) {
    // В Bluedroid нет getSubscribedCount().
    // Просто вызываем notify() — если клиент не подписан, стек молча отбросит пакет.
    sendDataInChunks(dataToSend);
    sendCount++;

    Serial.print("📤 [#");
    Serial.print(sendCount);
    Serial.print("] Задержка: ");
    Serial.print(delayTime);
    Serial.print("мс | Вес: ");
    Serial.print(currentWeight);
    Serial.print(" г | Расст: ");
    Serial.print(currentDistance);
    Serial.println(" мм");
  } else {
    Serial.print("⏸️ [ОЖИДАНИЕ] Задержка: ");
    Serial.print(delayTime);
    Serial.print("мс | Вес: ");
    Serial.print(currentWeight);
    Serial.print(" г | Расст: ");
    Serial.print(currentDistance);
    Serial.println(" мм");
  }

  delay(delayTime);
}