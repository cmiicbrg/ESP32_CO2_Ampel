# CO<sub>2</sub> Indicator Light

This CO<sub>2</sub> indicator light uses readily available components. 

## Hardware

* ESP32 Development Board
* MH-Z19B or MH-Z19C (400-5000ppm) Infrared CO<sub>2</sub> Module (needs calibration!!!)
* BME280 3.3V Temperature Humidity Barometric Pressure Sensor Module
* WS2811 Full Color LED Pixel Lights
* Fully 3D-Printed Case

You should be able to do a build for around USD 22,00

## Software 

* The software should work with or wihout WIFi
* If no WIFI is available, you can still query the measurements over Bluetooth
* Using 8 LEDs Color/Lighting scheme will be as follows
  * Temperature (b ... blue, c ... cyan, g ... green, r ... red), there are a litte more shades, but overall lighting is as follows:

    | °C | LED 1 | LED 2 | LED 3 | LED 4 |
    | :-: | :-: | :-: | :-: | :-: |
    | < 19 | b | c | g | |
    | < 22 | | c | g | |
    | < 25 | | | g | |
    | >=25 | | | g | r |

  * CO<sub>2</sub> ppm (g ... green, y ... yellow, o ... orange, r ... red), there are a litte more shades, but overall lighting is as follows:

    | ppm | LED 5 | LED 6 | LED 7 | LED 8 |
    | :-: | :-: | :-: | :-: | :-: |
    | < 800 | g | | | |
    | < 1400 | g | y | | |
    | < 2000 | g | y | o | |
    | < 2600 | g | y | o | r |
    | >= 3000 | g | r | r | r |
  So LED 3 and 8 can be seen as indicator for proper cabling/powering of the LEDs.
  800 ppm is some target referred to in some publications, for classroom-settings this is rather hard to achieve.
* Use a Bluetooth client like NRF Connect to connect. CO<sub>2</sub> ppm will be the only characteristic available right now.

## Config 
If you want to configure initial WIFI and InfluxDB you may want to set following environment variables:
- WIFI_SSID - WIFI access point name
- WIFI_PASS - WIFI password
- INFLUXDB_URL - URL of your Influx-DB
- INFLUXDB_DB_NAME - Name of the Influx DB to write to

If you don't set the vars, WIFI or INFLUX DB client will fail gracefully and you will still be able to get a reading using the LEDs or Bluetooth

