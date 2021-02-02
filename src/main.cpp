#include <Arduino.h>
#include <WiFi.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>

#include "MHZ19.h"
#include <InfluxDbClient.h>
#include "FastLED.h"
#include "ampelLeds.h"
#include <sstream>
#include <EEPROM.h>
#include <Wire.h>
#include <Adafruit_BME280.h>

/* WiFi */
const char *ssid = "***";
const char *password = "***";
bool isWiFiOK = true;

IPAddress softAPIP;

/* BLE */
#define SERVICE_UUID "7ec15f94-396e-11eb-adc1-0242ac120002"
#define CHARACTERISTIC_UUID "7ec161e2-396e-11eb-adc1-0242ac120002"
BLECharacteristic *pCharacteristic;
bool deviceConnected = false;

/* MH Z19B */
#define RX_PIN 16
#define TX_PIN 17
#define BAUDRATE 9600 // Native to the sensor (do not change)
MHZ19 myMHZ19;
HardwareSerial mySerial(2);
unsigned long getDataTimer = 0;
int lastCO2 = 0;

/* Influx DB */
#define INFLUXDB_URL "***"
#define INFLUXDB_DB_NAME "***"

InfluxDBClient client(INFLUXDB_URL, INFLUXDB_DB_NAME);
Point sensor("Environment");

/* BME280 */
Adafruit_BME280 bme;
bool bmeOK = true;

/* miscellaneous */
uint32_t chipId = 0;
std::string to_string(const uint32_t n)
{
  std::ostringstream stm;
  stm << n;
  return stm.str();
}

std::string shortName = "ESP_";
String deviceName = "CO2 Ampel ";

void setChipId()
{
  for (int i = 0; i < 17; i = i + 8)
  {
    chipId |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
  }
}

class MyServerCallbacks : public BLEServerCallbacks
{
  void onConnect(BLEServer *pServer)
  {
    deviceConnected = true;
  };

  void onDisconnect(BLEServer *pServer)
  {
    deviceConnected = false;
  }
};

void readCO2()
{
  bme.takeForcedMeasurement();
  float temp = bme.readTemperature();

  float CO2;
  CO2 = myMHZ19.getCO2(); // Request CO2 (as ppm)


  showCO2(CO2);
  showTemp(temp);
  FastLED.show();

  Serial.print("CO2 (ppm): ");
  Serial.println(CO2);
  getDataTimer = millis();
  lastCO2 = CO2;

  if (isWiFiOK)
  {
    sensor.clearFields();
    sensor.clearTags();

    sensor.addTag("device", deviceName + chipId);
    sensor.addField("ppm", CO2);
    if (bmeOK)
    {
      sensor.addField("temp", temp);
      sensor.addField("humidity", bme.readHumidity());
      sensor.addField("pressure", bme.readPressure());
    }
    client.writePoint(sensor);
  }
}

void initFastLED()
{
  defineColors();
  FastLED.addLeds<CHIPSET, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.clear();
  setPixel(2, green[0]);
  setPixel(4, green[0]);
  FastLED.show();
}

void setup()
{
  setChipId();
  Serial.begin(115200); //

  mySerial.begin(BAUDRATE, SERIAL_8N1, RX_PIN, TX_PIN);

  WiFi.begin(ssid, password);
  if (WiFi.waitForConnectResult() != WL_CONNECTED)
  {
    isWiFiOK = false;
  }
  if (isWiFiOK)
  {
    Serial.println();
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  }
  myMHZ19.begin(mySerial);
  myMHZ19.setRange(5000);

  initFastLED();

  if (!bme.begin(0x76, &Wire))
  {
    bmeOK = false;
    Serial.println("Could not find a valid BME280 sensor, check wiring!");
  }
  else
  {
    bmeOK = true;
    bme.setSampling(Adafruit_BME280::MODE_FORCED,
                    Adafruit_BME280::SAMPLING_X1, // temperature
                    Adafruit_BME280::SAMPLING_X1, // pressure
                    Adafruit_BME280::SAMPLING_X1, // humidity
                    Adafruit_BME280::FILTER_OFF);
  }

  readCO2();

  BLEDevice::init("CO2 Ampel " + to_string(chipId));
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  BLEService *pService = pServer->createService(SERVICE_UUID);
  pCharacteristic = pService->createCharacteristic(
      CHARACTERISTIC_UUID,
      BLECharacteristic::PROPERTY_READ |
          BLECharacteristic::PROPERTY_NOTIFY);

  pCharacteristic->setValue(String(lastCO2).c_str());
  pService->start();
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();

  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06); // functions that help with iPhone connections issue
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();
}

void loop()
{
  if (millis() - getDataTimer > 30000)
  {
    readCO2();
    pCharacteristic->setValue(String(lastCO2).c_str());
    if (deviceConnected)
    {

      pCharacteristic->notify();
    }
  }
}
