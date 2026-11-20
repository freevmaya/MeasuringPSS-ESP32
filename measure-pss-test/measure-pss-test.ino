#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>

#define SERVICE_UUID "4e292be3-71e6-4ca1-a06e-b1b3660df15c"
#define CHARACTERISTIC_UUID "087dde8f-83e6-4f77-b9b0-7e966a7706a8"

BLECharacteristic *pCharacteristic;
bool deviceConnected = false;

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      Serial.println("✅ Подключено!");
    }
    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      Serial.println("❌ Отключено!");
      BLEDevice::startAdvertising();
    }
};

void setup() {
  Serial.begin(115200);
  Serial.println("Тест BLE NOTIFY");
  
  BLEDevice::init("BLE_Test");
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  
  BLEService *pService = pServer->createService(SERVICE_UUID);
  
  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_NOTIFY
                    );
  pCharacteristic->addDescriptor(new BLE2902());
  
  pService->start();
  
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  BLEDevice::startAdvertising();
  
  Serial.println("Ожидание подключения...");
}

void loop() {
  if (deviceConnected) {
    String data = "Test: " + String(millis()) + "\r\n";
    pCharacteristic->setValue(data);
    pCharacteristic->notify();
    Serial.print("📤 ");
    Serial.println(data);
    delay(1000);
  }
}