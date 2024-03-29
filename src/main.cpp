#include <Arduino.h>
#include <FS.h>          // this needs to be first, or it all crashes and burns...
#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager
#include <ArduinoJson.h> // https://github.com/bblanchon/ArduinoJson
#include <SPIFFS.h>

#include <Update.h>
#include "Version.h"

#include "MHZ19.h"
#include <InfluxDbClient.h>
#include "FastLED.h"
#include "ampelLeds.h"
#include <sstream>
#include <EEPROM.h>
#include <Wire.h>
#include <Adafruit_BME280.h>

// Set to false to prevent leaking secrets in serial console
#define CO2_WIFI_DEBUG false
#define CO2_LIGHT_DEBUG false
#define FORMAT_SPIFFS_ON_FAIL true

/* WiFi */
#define START_SETUP_PIN 0

bool isWiFiOK = false;
char influxDBURL[40] = "";
char influxDBOrg[32] = "";
char influxDBBucket[32] = "";
char influxDBToken[128] = "";
char lastestVersionURL[60] = "";
char firmwarePath[60] = "";
char useWifi[2] = "1";
bool shouldShowPortal = false;
bool portalRunning = false;
char tempOffsetBME[5] = "-3.0";

WiFiManager wm;
WiFiManagerParameter influxDBURLParam("influxDBURLID", "Influx DB URL");
WiFiManagerParameter influxDBOrgParam("influxDBOrgID", "Influx DB Org");
WiFiManagerParameter influxDBBucketParam("influxDBBucketID", "Influx DB Bucket");
WiFiManagerParameter influxDBTokenParam("influxDBTokenID", "Influx DB Token");
WiFiManagerParameter lastestVersionURLParam("lastestVersionURLID", "URL with string of last version number");
WiFiManagerParameter firmwarePathParam("firmwarePathID", "Urlpath of firmware.bin");
WiFiManagerParameter useWifiParam("useWifiID", "Use Wifi 1/0", useWifi, 2);
WiFiManagerParameter tempOffsetBMEParam("tempOffsetBME", "Temperature offset for BME", tempOffsetBME, 5);
WiFiManagerParameter calibrateNowParam("calibrateNow", "Calibrate MH-Z19B now to 400 ppm", "0", 2);


/* MH Z19B */
#define RX_PIN 16
#define TX_PIN 17
#define BAUDRATE 9600 // Native to the sensor (do not change)
#define MEASUREMENT_INTERVAL 10000
MHZ19 myMHZ19;
HardwareSerial mySerial(2);
unsigned long getDataTimer = 0;
int lastCO2 = 0;
bool MHZ19OK = false;
unsigned long lastSuccessfulWriteTimer = 0;
unsigned long readCount = 0;

/* Influx DB */
InfluxDBClient client;
Point sensor("Environment");
bool shouldWriteToInflux = false;

/* BME280 */
// Temperature compensation for the setup
// #define TEMP_COMPENSATION -3.0f
// Altitude of the location
#define ALTITUDE 277.0f
Adafruit_BME280 bme;
bool bmeOK = false;

/* miscellaneous */
String chipId = "";

String deviceName = "CO2 Ampel ";

unsigned long getBlinkTimer = 0;

String newVersion = "";
unsigned long lastUpdateTimer = 0;
unsigned int checkCount = 0;

unsigned long timeWithReadingAbove400 = 0;
unsigned long timeWithReadingBelow500 = 0;

unsigned int sampleCounter = 0;
#define SAMPLE_SIZE 30
float samples[SAMPLE_SIZE];
unsigned int s1DiffBelowThresholdCount = 0;
// float diffs[SAMPLE_SIZE];

void setChipId()
{
  uint32_t lchipId = 0;
  for (int i = 0; i < 17; i = i + 8)
  {
    lchipId |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
  }
  chipId = String(lchipId);
}

void checkUpdate()
{
  if (WiFi.isConnected())
  {
    WiFiClientSecure *client = new WiFiClientSecure;
    client->setInsecure();
    if (client)
    {
      {
        // Add a scoping block for HTTPClient https to make sure it is destroyed before WiFiClientSecure *client is
        HTTPClient https;
        https.setUserAgent(deviceName + chipId + " " + VERSION);
        Serial.print("[HTTPS] begin...\n");
        if (https.begin(*client, lastestVersionURL))
        { // HTTPS
          Serial.print("[HTTPS] GET...\n");
          // start connection and send HTTP header
          int httpCode = https.GET();

          // httpCode will be negative on error
          if (httpCode > 0)
          {
            // HTTP header has been send and Server response header has been handled
            Serial.printf("[HTTPS] GET... code: %d\n", httpCode);

            // file found at server
            if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY)
            {
              std::string nV;
              newVersion = https.getStream().readStringUntil('\n');
              Serial.println(newVersion);
            }
          }
          else
          {
            Serial.printf("[HTTPS] GET... failed, error: %s\n", https.errorToString(httpCode).c_str());
          }

          https.end();
        }
        else
        {
          Serial.printf("[HTTPS] Unable to connect\n");
        }

        // End extra scoping block
      }
      delete client;
    }
    else
    {
      Serial.println("Unable to create client");
    }
  }
}
void processOTAUpdate()
{
  if (WiFi.isConnected())
  {
    WiFiClientSecure *client = new WiFiClientSecure;
    client->setInsecure();
    if (client)
    {
      {
        // Add a scoping block for HTTPClient https to make sure it is destroyed before WiFiClientSecure *client is
        HTTPClient https;
        https.setUserAgent(deviceName + chipId + " " + VERSION);

        Serial.print("[HTTPS] begin...\n");
        if (https.begin(*client, firmwarePath))
        { // HTTPS
          Serial.print("[HTTPS] GET...\n");
          // start connection and send HTTP header
          int httpCode = https.GET();

          // httpCode will be negative on error
          if (httpCode > 0)
          {
            // HTTP header has been send and Server response header has been handled
            Serial.printf("[HTTPS] GET... code: %d\n", httpCode);
            if (httpCode == HTTP_CODE_FOUND)
            {
              Serial.println("https.hasHeader(\"location\")???");
              Serial.println(https.hasHeader("location"));
            }
            // file found at server
            if (httpCode == HTTP_CODE_OK) // || httpCode == HTTP_CODE_MOVED_PERMANENTLY)
            {
              int contentLength = https.getSize();
              Serial.println(contentLength);
              if (contentLength <= 0)
              {
                Serial.println("Content-Length not defined");
                return;
              }
              bool canBegin = Update.begin(contentLength);
              if (!canBegin)
              {
                Serial.println("Not enough space to begin OTA");
                return;
              }
              Serial.println("Will begin OTA Update");
              Client &client = https.getStream();
              int written = Update.writeStream(client);
              if (written != contentLength)
              {
                Serial.println(String("OTA written ") + written + " / " + contentLength + " bytes");
                return;
              }

              if (!Update.end())
              {
                Serial.println("Error #" + String(Update.getError()));
                return;
              }

              if (!Update.isFinished())
              {
                Serial.println("Update failed.");
                return;
              }

              Serial.println("Update successfully completed. Rebooting.");
              ESP.restart();
            }
            else if (httpCode == HTTP_CODE_MOVED_PERMANENTLY)
            {
              Serial.println("Moved Permanently");
            }
          }
          else
          {
            Serial.printf("[HTTPS] GET... failed, error: %s\n", https.errorToString(httpCode).c_str());
          }

          https.end();
        }
        else
        {
          Serial.printf("[HTTPS] Unable to connect\n");
        }

        // End extra scoping block
      }
      delete client;
    }
    else
    {
      Serial.println("Unable to create client");
    }
  }
}

void readCO2()
{
  if (MHZ19OK)
  {
    // bme.takeForcedMeasurement();
    float temp = bme.readTemperature();
    float tempCompensation = bme.getTemperatureCompensation();

    float CO2;
    CO2 = myMHZ19.getCO2(); // Request CO2 (as ppm)
    float mhzTemp = myMHZ19.getTemperature();
    readCount++;
    if (CO2 > 0.0f && !(readCount <= 4 && CO2 > 1400)) // reading is sometimes zero or too high on the first readings -> don't publish obviously wrong values
    {
      showCO2(CO2);
      if (bmeOK)
      {
        showTemp(temp);
      }
      FastLED.show();

      Serial.print("CO2 (ppm): ");
      Serial.println(CO2);
      Serial.print("Temp: ");
      Serial.println(temp);

      if (CO2 > 400.0f)
      {
        timeWithReadingAbove400 = millis();
      }
      if (CO2 < 500.0f)
      {
        timeWithReadingBelow500 = millis();
      }
      if (millis() - timeWithReadingAbove400 > 600000)
      {
        // All readings in the last 10 Minutes have been below 400 -> calibrate
        Serial.println("Calibrating ..");
        myMHZ19.calibrate();
      }
      samples[sampleCounter % SAMPLE_SIZE] = CO2;
      float ssDiff = 0;
      float s1Diff = 0;
      // float s2Diff = 0;
      // float s3Diff = 0;
      // float s4Diff = 0;
      // float s5Diff = 0;

      if (readCount >= SAMPLE_SIZE)
      {
        ssDiff = samples[sampleCounter] - samples[(sampleCounter + 1) % SAMPLE_SIZE];
        if (CO2_LIGHT_DEBUG)
        {
          Serial.print("ssDiff: ");
          Serial.println(ssDiff);
        }
      }
      if (readCount > SAMPLE_SIZE / 5)
      {
        s1Diff = samples[sampleCounter] - samples[(sampleCounter - SAMPLE_SIZE / 5) % SAMPLE_SIZE];
        if (s1Diff < -15)
        {
          s1DiffBelowThresholdCount++;
        }
        else
        {
          s1DiffBelowThresholdCount = 0;
        }
        if (s1DiffBelowThresholdCount >= SAMPLE_SIZE && ssDiff < -400)
        {
          if (millis() - timeWithReadingBelow500 > 14400000)
          {
            Serial.println("Calibrating ..");
            myMHZ19.calibrate();
          }
        }
        if (CO2_LIGHT_DEBUG)
        {
          Serial.print("s1Diff: ");
          Serial.println(s1Diff);
        }
      }

      sampleCounter++;
      if (sampleCounter >= SAMPLE_SIZE)
      {
        sampleCounter = 0;
      }

      if (isWiFiOK && shouldWriteToInflux)
      {
        sensor.clearFields();
        sensor.clearTags();

        sensor.addTag("device", deviceName + chipId);
        sensor.addTag("SSID", WiFi.SSID());

        sensor.addField("rssi", WiFi.RSSI());
        sensor.addField("ppm", CO2);
        sensor.addField("mhzTemp", mhzTemp);
        sensor.addField("readCountSinceLastBoot", readCount);
        sensor.addField("ssDiff", ssDiff);
        sensor.addField("s1Diff", s1Diff);
        sensor.addField("timeAbove500", millis() - timeWithReadingBelow500);
        if (bmeOK)
        {
          float pressure = bme.readPressure();
          sensor.addField("seaLevelPressure", bme.seaLevelForAltitude(ALTITUDE, pressure));
          sensor.addField("temp", temp);
          sensor.addField("tempCompensation", tempCompensation);
          sensor.addField("humidity", bme.readHumidity());
          sensor.addField("pressure", pressure);
        }
        client.writePoint(sensor);
        lastSuccessfulWriteTimer = millis();
      }

      lastCO2 = CO2;
    }
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
  // read configuration from FS json
  Serial.println("mounting FS...");

  if (SPIFFS.begin(FORMAT_SPIFFS_ON_FAIL))
  {
    if (SPIFFS.exists("/config.json"))
    {
      // file exists, reading and loading
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
        if (jsonDoc.containsKey("lastestVersionURL"))
        {
          strcpy(lastestVersionURL, jsonDoc["lastestVersionURL"]);
        }
        if (jsonDoc.containsKey("firmwarePath"))
        {
          strcpy(firmwarePath, jsonDoc["firmwarePath"]);
        }
        if (jsonDoc.containsKey("useWifi"))
        {
          strcpy(useWifi, jsonDoc["useWifi"]);
        }
        if (jsonDoc.containsKey("tempOffsetBME"))
        {
          strcpy(tempOffsetBME, jsonDoc["tempOffsetBME"]);
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

void storeParamsInJSON()
{
  DynamicJsonDocument jsonDoc(1024);

  jsonDoc["influxDBURL"] = influxDBURL;
  jsonDoc["influxDBOrg"] = influxDBOrg;
  jsonDoc["influxDBBucket"] = influxDBBucket;
  jsonDoc["influxDBToken"] = influxDBToken;
  jsonDoc["lastestVersionURL"] = lastestVersionURL;
  jsonDoc["firmwarePath"] = firmwarePath;
  jsonDoc["useWifi"] = useWifi;
  jsonDoc["tempOffsetBME"] = tempOffsetBME;

  File configFile = SPIFFS.open("/config.json", "w");
  if (!configFile)
  {
    Serial.println("failed to open config file for writing");
  }

  serializeJson(jsonDoc, configFile);

  configFile.close();
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
    strcpy(lastestVersionURL, lastestVersionURLParam.getValue());
    strcpy(firmwarePath, firmwarePathParam.getValue());

    strcpy(useWifi, useWifiParam.getValue());

    strcpy(tempOffsetBME, tempOffsetBMEParam.getValue());

    storeParamsInJSON();

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
      Serial.print("Latest URL: ");
      Serial.println(lastestVersionURL);
      Serial.print("Firmware Path: ");
      Serial.println(firmwarePath);
    }

    client.setConnectionParams(influxDBURL, influxDBOrg, influxDBBucket, influxDBToken);
    shouldWriteToInflux = client.validateConnection();

    if (strcmp(calibrateNowParam.getValue(), "1") == 0)
    {
      Serial.println("Calibrating...");
      myMHZ19.calibrate();
    }
  }
}

void initIfAllBuildFlagsAreSet()
{
  // Use Buildflags for faster deployment in Schools
#if defined(INFLUXDB_URL) && defined(INFLUXDB_DB_ORG) && defined(INFLUXDB_DB_BUCKET) && defined(INFLUXDB_DB_TOKEN) && defined(LATEST_VERSION_URL) && defined(FIRMWARE_PATH)
  if (strcmp(influxDBBucket, "") == 0 && strcmp(influxDBOrg, "") == 0 && strcmp(influxDBBucket, "") == 0 && strcmp(influxDBToken, "") == 0)
  {
    strcpy(influxDBURL, INFLUXDB_URL);
    strcpy(influxDBOrg, INFLUXDB_DB_ORG);
    strcpy(influxDBBucket, INFLUXDB_DB_BUCKET);
    strcpy(influxDBToken, INFLUXDB_DB_TOKEN);
    strcpy(lastestVersionURL, LATEST_VERSION_URL);
    strcpy(firmwarePath, FIRMWARE_PATH);
    storeParamsInJSON();
  }
#endif

#if defined(WIFI_SSID) && defined(WIFI_PASS) && defined(INFLUXDB_URL) && defined(INFLUXDB_DB_ORG) && defined(INFLUXDB_DB_BUCKET) && defined(INFLUXDB_DB_TOKEN) && defined(LATEST_VERSION_URL) && defined(FIRMWARE_PATH) // all set start wifi
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
  // Don't set token, otherwise it can be read from the web portal
  influxDBTokenParam.setValue("", 128);
  lastestVersionURLParam.setValue(lastestVersionURL, 32);
  firmwarePathParam.setValue(firmwarePath, 32);
  useWifiParam.setValue(useWifi, 2);
  tempOffsetBMEParam.setValue(tempOffsetBME, 5);

  wm.addParameter(&influxDBURLParam);
  wm.addParameter(&influxDBOrgParam);
  wm.addParameter(&influxDBBucketParam);
  wm.addParameter(&influxDBTokenParam);
  wm.addParameter(&lastestVersionURLParam);
  wm.addParameter(&firmwarePathParam);
  wm.addParameter(&useWifiParam);
  wm.addParameter(&tempOffsetBMEParam);
  wm.addParameter(&calibrateNowParam);

  wm.setSaveParamsCallback(saveParams);
  std::vector<const char *> menu = {"wifi", "info", "param", "sep", "restart", "exit"};
  wm.setMenu(menu);

  wm.setConnectTimeout(10);
  wm.setConfigPortalTimeout(120);

  wm.setClass("invert");
  wm.setHostname((deviceName + chipId).c_str());
  if (strcmp(useWifi, "1") == 0)
  {
    isWiFiOK = wm.autoConnect((deviceName + chipId).c_str(), ("pass" + chipId).c_str());
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
      Serial.print("Latest URL: ");
      Serial.println(lastestVersionURL);
      Serial.print("Firmware Path: ");
      Serial.println(firmwarePath);
    }
    client.setConnectionParams(influxDBURL, influxDBOrg, influxDBBucket, influxDBToken);
    client.setInsecure();
    shouldWriteToInflux = client.validateConnection();
  }

  mySerial.begin(BAUDRATE, SERIAL_8N1, RX_PIN, TX_PIN);
  myMHZ19.begin(mySerial);
  MHZ19OK = myMHZ19.errorCode == RESULT_OK;
  myMHZ19.setRange(5000);
  myMHZ19.autoCalibration(false);

  initFastLED();

  if (!bme.begin(0x76, &Wire))
  {
    bmeOK = false;
    Serial.println("Could not find a valid BME280 sensor, check wiring!");
  }
  else
  {
    bmeOK = true;
    bme.setSampling(Adafruit_BME280::MODE_NORMAL,
                    Adafruit_BME280::SAMPLING_X1, // temperature
                    Adafruit_BME280::SAMPLING_X1, // pressure
                    Adafruit_BME280::SAMPLING_X1, // humidity
                    Adafruit_BME280::FILTER_OFF);

    // bme.setTemperatureCompensation(TEMP_COMPENSATION);
    bme.setTemperatureCompensation(atof(tempOffsetBME));
  }

  readCO2();

}

void loop()
{
  if (millis() - getDataTimer > MEASUREMENT_INTERVAL)
  {
    isWiFiOK = WiFi.status() == WL_CONNECTED;
    readCO2();
    getDataTimer = millis();
  }
  if (millis() - getBlinkTimer > 500)
  {
    MHZ19OK = myMHZ19.errorCode == RESULT_OK;
    if (!MHZ19OK)
    {
      if (leds[4] == green[0])
      {
        setPixel(4, red[0]);
      }
      else
      {
        setPixel(4, green[0]);
      }
    }
    if (!bmeOK)
    {
      if (leds[2] == green[0])
      {
        setPixel(2, red[0]);
      }
      else
      {
        setPixel(2, green[0]);
      }
    }
    FastLED.show();
    getBlinkTimer = millis();
  }
  if (shouldShowPortal && !portalRunning)
  {
    if (WiFi.status() == WL_CONNECTED)
    {
      wm.startWebPortal();
    }
    else
    {
      isWiFiOK = wm.autoConnect((deviceName + chipId).c_str(), ("pass" + chipId).c_str());
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
  if (isWiFiOK && ((millis() > 45000 && checkCount == 0) || millis() - lastUpdateTimer > 3600000)) // 43200000))
  {
    checkUpdate();

    if (Version(VERSION) < Version(newVersion.c_str()))
    {
      processOTAUpdate();
    }
    checkCount++;
    lastUpdateTimer = millis();
  }
  if (!isWiFiOK && strcmp(useWifi, "1") == 0 && strcmp(influxDBBucket, "") != 0 && strcmp(influxDBOrg, "") != 0 && strcmp(influxDBBucket, "") != 0 && strcmp(influxDBToken, "") != 0)
  {
    if (millis() - lastSuccessfulWriteTimer > 3600000)
    {
      ESP.restart();
    }
  }
}
