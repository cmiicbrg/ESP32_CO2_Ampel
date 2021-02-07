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

//Set to false to prevent leaking secrets in serial console
#define CO2_WIFI_DEBUG true
#define CO2_LIGHT_DEBUG true
#define FORMAT_SPIFFS_ON_FAIL true

/* WiFi */
#define START_SETUP_PIN 0

bool isWiFiOK = false;
char influxDBURL[40] = "";
char influxDBOrg[32] = "";
char influxDBBucket[32] = "";
char influxDBToken[128] = "";
char useWifi[2] = "0";
char useBLE[2] = "0";
bool shouldShowPortal = false;
bool portalRunning = false;

WiFiManager wm;
WiFiManagerParameter influxDBURLParam("influxDBURLID", "Influx DB URL");
WiFiManagerParameter influxDBOrgParam("influxDBOrgID", "Influx DB Org");
WiFiManagerParameter influxDBBucketParam("influxDBBucketID", "Influx DB Bucket");
WiFiManagerParameter influxDBTokenParam("influxDBTokenID", "Influx DB Token");
WiFiManagerParameter useWifiParam("useWifiID", "Use Wifi 1/0", useWifi, 2);
WiFiManagerParameter useBLEParam("useBLEID", "Use BLE 1/0", useBLE, 2);

/* BLE */
#define SERVICE_UUID "7ec15f94-396e-11eb-adc1-0242ac120002"
#define CHARACTERISTIC_UUID "7ec161e2-396e-11eb-adc1-0242ac120002"
BLECharacteristic *pCharacteristic;
bool deviceConnected = false;
bool shouldUseBluetooth = false;

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
bool shouldWriteToInflux = false;

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
  Serial.print("Temp: ");
  Serial.println(temp);

  showCO2(CO2);
  showTemp(temp);
  FastLED.show();

  Serial.print("CO2 (ppm): ");
  Serial.println(CO2);
  getDataTimer = millis();
  lastCO2 = CO2;

  if (isWiFiOK && shouldWriteToInflux)
  {
    sensor.clearFields();
    sensor.clearTags();

    sensor.addTag("device", deviceName + chipId);
    sensor.addTag("SSID", WiFi.SSID());

    sensor.addField("rssi", WiFi.RSSI());
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
  if (CO2_LIGHT_DEBUG)
  {
    Serial.println("cleared Fastled, setting 2 and 4 to green");
  }
  setPixel(2, green[0]);
  setPixel(4, green[0]);
  FastLED.show();
}

void loadParamsFromSpiffs()
{
  //read configuration from FS json
  Serial.println("mounting FS...");

  if (SPIFFS.begin(FORMAT_SPIFFS_ON_FAIL))
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
        if (jsonDoc.containsKey("influxDBOrg"))
        {
          strcpy(influxDBOrg, jsonDoc["influxDBOrg"]);
        }
        if (jsonDoc.containsKey("influxDBBucket"))
        {
          strcpy(influxDBBucket, jsonDoc["influxDBBucket"]);
        }
        if (jsonDoc.containsKey("influxDBToken"))
        {
          strcpy(influxDBToken, jsonDoc["influxDBToken"]);
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
  if (influxDBURL != influxDBURLParam.getValue() || influxDBBucket != influxDBBucketParam.getValue())
  {
    strcpy(influxDBURL, influxDBURLParam.getValue());
    strcpy(influxDBOrg, influxDBOrgParam.getValue());
    strcpy(influxDBBucket, influxDBBucketParam.getValue());
    if (strcmp(influxDBTokenParam.getValue(), "") != 0)
    {
      strcpy(influxDBToken, influxDBTokenParam.getValue());
    }
    strcpy(useWifi, useWifiParam.getValue());
    strcpy(useBLE, useBLEParam.getValue());

    DynamicJsonDocument jsonDoc(1024);

    jsonDoc["influxDBURL"] = influxDBURL;
    jsonDoc["influxDBOrg"] = influxDBOrg;
    jsonDoc["influxDBBucket"] = influxDBBucket;
    jsonDoc["influxDBToken"] = influxDBToken;
    jsonDoc["useWifi"] = useWifi;
    jsonDoc["useBLE"] = useBLE;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile)
    {
      Serial.println("failed to open config file for writing");
    }

    serializeJson(jsonDoc, configFile);

    configFile.close();
    if (CO2_LIGHT_DEBUG)
    {
      Serial.println("### INFLUX ###");
      Serial.print("URL: ");
      Serial.println(influxDBURL);
      Serial.print("Org: ");
      Serial.println(influxDBOrg);
      Serial.print("Bucket: ");
      Serial.println(influxDBBucket);
      Serial.print("Token: ");
      Serial.println(influxDBToken);
    }

    client.setConnectionParams(influxDBURL, influxDBOrg, influxDBBucket, influxDBToken);
    shouldWriteToInflux = client.validateConnection();
  }
}

void initIfAllBuildFlagsAreSet()
{
  // Use Buildflags for faster deployment in Schools
#if defined(INFLUXDB_URL) && defined(INFLUXDB_DB_ORG) && defined(INFLUXDB_DB_BUCKET) && defined(INFLUXDB_DB_TOKEN)
  if (strcmp(influxDBBucket, "") == 0 && strcmp(influxDBOrg, "") == 0 && strcmp(influxDBBucket, "") == 0 && strcmp(influxDBToken, "") == 0)
  {
    strcpy(influxDBURL, INFLUXDB_URL);
    strcpy(influxDBOrg, INFLUXDB_DB_ORG);
    strcpy(influxDBBucket, INFLUXDB_DB_BUCKET);
    strcpy(influxDBToken, INFLUXDB_DB_TOKEN);
  }
#endif

#if defined(WIFI_SSID) && defined(WIFI_PASS) && defined(INFLUXDB_URL) && defined(INFLUXDB_DB_ORG) && defined(INFLUXDB_DB_BUCKET) && defined(INFLUXDB_DB_TOKEN) // all set start wifi
  WiFi.mode(WIFI_STA);
  if (WiFi.psk() == "") // Only on first run
  {
    WiFi.begin(WIFI_SSID, WIFI_PASS);
  }
#endif
}

void setupWifi()
{
  initIfAllBuildFlagsAreSet();
  wm.setDebugOutput(CO2_WIFI_DEBUG);

  influxDBURLParam.setValue(influxDBURL, 40);
  influxDBOrgParam.setValue(influxDBOrg, 32);
  influxDBBucketParam.setValue(influxDBBucket, 32);
  //Don't set token, otherwise it can be read from the web portal
  influxDBTokenParam.setValue("", 128);
  useWifiParam.setValue(useWifi, 2);
  useBLEParam.setValue(useBLE, 2);

  wm.addParameter(&influxDBURLParam);
  wm.addParameter(&influxDBOrgParam);
  wm.addParameter(&influxDBBucketParam);
  wm.addParameter(&influxDBTokenParam);
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

  //isWiFiOK = WiFi.status() == WL_CONNECTED;

  if (isWiFiOK)
  {
    if (CO2_LIGHT_DEBUG)
    {
      Serial.println("### INFLUX ###");
      Serial.print("URL: ");
      Serial.println(influxDBURL);
      Serial.print("Org: ");
      Serial.println(influxDBOrg);
      Serial.print("Bucket: ");
      Serial.println(influxDBBucket);
      Serial.print("Token: ");
      Serial.println(influxDBToken);
    }
    client.setConnectionParams(influxDBURL, influxDBOrg, influxDBBucket, influxDBToken);
    shouldWriteToInflux = client.validateConnection();
  }

  mySerial.begin(BAUDRATE, SERIAL_8N1, RX_PIN, TX_PIN);
  myMHZ19.begin(mySerial);
  // if (myMHZ19.errorCode != RESULT_OK) {

  // }
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

  // can't use Bluetooth if InfluxDB uses TLS (memory allocation error,...)
  shouldUseBluetooth = strcmp(useBLE, "1") == 0 && !(shouldWriteToInflux && strstr(influxDBURL, "https"));
  if (CO2_LIGHT_DEBUG)
  {
    Serial.print("Should use Bluetooth: ");
    Serial.println(useBLE);
    Serial.print("May use Bluetooth: ");
    Serial.println(shouldUseBluetooth);
  }
  if (shouldUseBluetooth)
  {
    if (CO2_LIGHT_DEBUG)
    {
      Serial.println("Setting up Bluetooth");
    }
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
    if (shouldUseBluetooth)
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
