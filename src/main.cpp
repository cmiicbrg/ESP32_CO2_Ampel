#include <Arduino.h>
#include <FS.h>          // this needs to be first, or it all crashes and burns...
#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager
#include <ArduinoJson.h> // https://github.com/bblanchon/ArduinoJson
#include <SPIFFS.h>

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

#define CO2_WIFI_DEBUG false
#define CO2_LIGHT_DEBUG false

/* WiFi */
#define START_SETUP_PIN 0

bool isWiFiOK = false;
char influxDBURL[40] = "";
char influxDBName[32] = "";
char useWifi[2] = "1";
char useBLE[2] = "1";
bool shouldShowPortal = false;
bool portalRunning = false;

WiFiManager wm;
WiFiManagerParameter influxDBURLParam("influxDBURLID", "Influx DB URL");
WiFiManagerParameter influxDBNameParam("influxDBNameID", "Influx DB Name");
WiFiManagerParameter useWifiParam("useWifiID", "Use Wifi 1/0", useWifi, 2);
WiFiManagerParameter useBLEParam("useBLEID", "Use BLE 1/0", useBLE, 2);

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
InfluxDBClient client;
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

void loadParamsFromSpiffs()
{
  //read configuration from FS json
  Serial.println("mounting FS...");

  if (SPIFFS.begin())
  {
    if (SPIFFS.exists("/config.json"))
    {
      //file exists, reading and loading
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile)
      {
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonDocument jsonDoc(1024);
        deserializeJson(jsonDoc, buf.get());

        if (jsonDoc.containsKey("influxDBURL"))
        {
          strcpy(influxDBURL, jsonDoc["influxDBURL"]);
        }
        if (jsonDoc.containsKey("influxDBName"))
        {
          strcpy(influxDBName, jsonDoc["influxDBName"]);
        }
        if (jsonDoc.containsKey("useWifi"))
        {
          strcpy(useWifi, jsonDoc["useWifi"]);
        }
        if (jsonDoc.containsKey("useBLE"))
        {
          strcpy(useBLE, jsonDoc["useBLE"]);
        }
      }
      else
      {
        Serial.println("failed to load json config");
      }
    }
    else
    {
      Serial.println("/config.json does not exist (yet)");
    }
  }
  else
  {
    Serial.println("failed to mount FS");
  }
}

void saveParams()
{

  Serial.println("Save params");
  if (influxDBURL != influxDBURLParam.getValue() || influxDBName != influxDBNameParam.getValue())
  {
    strcpy(influxDBURL, influxDBURLParam.getValue());
    strcpy(influxDBName, influxDBNameParam.getValue());
    strcpy(useWifi, useWifiParam.getValue());
    strcpy(useBLE, useBLEParam.getValue());

    DynamicJsonDocument jsonDoc(1024);

    jsonDoc["influxDBURL"] = influxDBURL;
    jsonDoc["influxDBName"] = influxDBName;
    jsonDoc["useWifi"] = useWifi;
    jsonDoc["useBLE"] = useBLE;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile)
    {
      Serial.println("failed to open config file for writing");
    }

    serializeJson(jsonDoc, configFile);

    configFile.close();
    client.setConnectionParamsV1(influxDBURL, influxDBName);
  }
}

void setupWifi()
{
// Use Buildflags for faster deployment in Schools
#if defined(INFLUXDB_URL) && defined(INFLUXDB_DB_NAME)
  if (strcmp(influxDBName, "") == 0 && strcmp(influxDBName, "") == 0)
  {
    strcpy(influxDBURL, INFLUXDB_URL);
    strcpy(influxDBName, INFLUXDB_DB_NAME);
  }
#endif

#if defined(WIFI_SSID) && defined(WIFI_PASS) && defined(INFLUXDB_URL) && defined(INFLUXDB_DB_NAME) // all set start wifi
  WiFi.mode(WIFI_STA);
  if (WiFi.psk() == "") // Only on first run
  {
    WiFi.begin(WIFI_SSID, WIFI_PASS);
  }
#endif

  wm.setDebugOutput(CO2_WIFI_DEBUG);

  influxDBURLParam.setValue(influxDBURL, 40);
  influxDBNameParam.setValue(influxDBName, 32);
  useWifiParam.setValue(useWifi, 2);
  useBLEParam.setValue(useBLE, 2);

  wm.addParameter(&influxDBURLParam);
  wm.addParameter(&influxDBNameParam);
  wm.addParameter(&useWifiParam);
  wm.addParameter(&useBLEParam);

  wm.setSaveParamsCallback(saveParams);
  std::vector<const char *> menu = {"wifi", "info", "param", "sep", "restart", "exit"};
  wm.setMenu(menu);

  wm.setConnectTimeout(10);
  wm.setConfigPortalTimeout(120);

  wm.setClass("invert");
  wm.setHostname(("CO2 Ampel " + to_string(chipId)).c_str());
  if (strcmp(useWifi, "1") == 0)
  {
    isWiFiOK = wm.autoConnect(("CO2 Ampel " + to_string(chipId)).c_str(), ("pass" + to_string(chipId)).c_str());
  }
}

void toggleShouldStartPortal()
{
  shouldShowPortal = !shouldShowPortal;
}

void setup()
{
  Serial.begin(115200);
  setChipId();
  loadParamsFromSpiffs(); // read params from config.json

  setupWifi();

  pinMode(START_SETUP_PIN, INPUT_PULLUP);
  attachInterrupt(START_SETUP_PIN, toggleShouldStartPortal, FALLING);

  if (isWiFiOK)
  {
    client.setConnectionParamsV1(influxDBURL, influxDBName);
  }
  mySerial.begin(BAUDRATE, SERIAL_8N1, RX_PIN, TX_PIN);
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

  if (strcmp(useBLE, "1") == 0)
  {
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
}

void loop()
{
  if (millis() - getDataTimer > 30000)
  {
    isWiFiOK = WiFi.status() == WL_CONNECTED;
    readCO2();
    if (strcmp(useBLE, "1") == 0)
    {
      pCharacteristic->setValue(String(lastCO2).c_str());
      if (deviceConnected)
      {
        pCharacteristic->notify();
      }
    }
  }
  if (shouldShowPortal && !portalRunning)
  {
    if (WiFi.status() == WL_CONNECTED)
    {
      wm.startWebPortal();
    }
    else
    {
      isWiFiOK = wm.autoConnect(("CO2 Ampel " + to_string(chipId)).c_str(), ("pass" + to_string(chipId)).c_str());
    }
    portalRunning = true;
  }
  else if (!shouldShowPortal && portalRunning)
  {
    wm.stopWebPortal();
    portalRunning = false;
  }
  if (portalRunning)
  {
    wm.process();
  }
}
