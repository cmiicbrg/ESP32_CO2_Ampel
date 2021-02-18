#include <FastLED.h>

#ifndef MyLED_H_
#define MyLED_H_

#define LED_PIN 5
#define COLOR_ORDER GRB
#define CHIPSET WS2811
#define NUM_LEDS 8
#define FRAMES_PER_SECOND 200

CRGB leds[NUM_LEDS];
bool gReverseDirection = false;

CRGB green[3];
CRGB yellow[3];
CRGB orange[3];
CRGB red[3];
CRGB blue[3];
CRGB cyan[3];
CRGB off;

void defineColors()
{
    off = CRGB(0, 0, 0);

    green[0] = CRGB(20, 0, 0);
    green[1] = CRGB(40, 0, 0);
    green[2] = CRGB(80, 0, 0);

    yellow[0] = CRGB(32, 40, 0);
    yellow[1] = CRGB(64, 80, 0);
    yellow[2] = CRGB(96, 120, 0);

    orange[0] = CRGB(16, 64, 0);
    orange[1] = CRGB(32, 128, 0);
    orange[2] = CRGB(64, 255, 0);

    red[0] = CRGB(0, 64, 0);
    red[1] = CRGB(0, 128, 0);
    red[2] = CRGB(0, 255, 0);

    blue[0] = CRGB(0, 0, 40);
    blue[1] = CRGB(0, 0, 80);
    blue[2] = CRGB(0, 0, 160);

    cyan[0] = CRGB(20, 0, 40);
    cyan[1] = CRGB(40, 0, 80);
    cyan[2] = CRGB(80, 0, 160);
}

void showCO2(float ppm)
{
    if (ppm < 800.0)
    {
        leds[5] = off;
        leds[6] = off;
        leds[7] = off;
    }
    else if (ppm < 1000.0)
    {
        leds[5] = yellow[0];
        leds[6] = off;
        leds[7] = off;
    }
    else if (ppm < 1200.0)
    {
        leds[5] = yellow[1];
        leds[6] = off;
        leds[7] = off;
    }
    else if (ppm < 1400.0)
    {
        leds[5] = yellow[2];
        leds[6] = off;
        leds[7] = off;
    }
    else if (ppm < 1600.0)
    {
        leds[5] = yellow[2];
        leds[6] = orange[0];
        leds[7] = off;
    }
    else if (ppm < 1800.0)
    {
        leds[5] = yellow[2];
        leds[6] = orange[1];
        leds[7] = off;
    }
    else if (ppm < 2000.0)
    {
        leds[5] = yellow[2];
        leds[6] = orange[2];
        leds[7] = off;
    }
    else if (ppm < 2200.0)
    {
        leds[5] = yellow[2];
        leds[6] = orange[2];
        leds[7] = red[0];
    }
    else if (ppm < 2400.0)
    {
        leds[5] = yellow[2];
        leds[6] = orange[2];
        leds[7] = red[1];
    }
    else if (ppm < 2600.0)
    {
        leds[5] = yellow[2];
        leds[6] = orange[2];
        leds[7] = red[2];
    }
    else if (ppm < 2800.0)
    {
        leds[5] = yellow[2];
        leds[6] = red[2];
        leds[7] = red[2];
    }
    else if (ppm < 3000.0)
    {
        leds[5] = orange[2];
        leds[6] = red[2];
        leds[7] = red[2];
    }
    else if (ppm >= 3000.0)
    {
        leds[5] = red[2];
        leds[6] = red[2];
        leds[7] = red[2];
    }
}

void showTemp(float temp)
{
    if (temp < 17.0)
    {
        leds[0] = blue[2];
        leds[1] = cyan[2];
        leds[2] = green[0];
        leds[3] = off;
    }
    else if (temp < 18.0)
    {
        leds[0] = blue[0];
        leds[1] = cyan[2];
        leds[2] = green[0];
        leds[3] = off;
    }
    else if (temp < 19.0)
    {
        leds[0] = off;
        leds[1] = cyan[2];
        leds[2] = green[0];
        leds[3] = off;
    }
    else if (temp < 20.0)
    {
        leds[0] = off;
        leds[1] = cyan[1];
        leds[2] = green[0];
        leds[3] = off;
    }
    else if (temp < 21.0)
    {
        leds[0] = off;
        leds[1] = cyan[0];
        leds[2] = green[0];
        leds[3] = off;
    }
    else if (temp < 25.0)
    {
        leds[0] = off;
        leds[1] = off;
        leds[2] = green[0];
        leds[3] = off;
    }
    else if (temp < 26.0)
    {
        leds[0] = off;
        leds[1] = off;
        leds[2] = green[0];
        leds[3] = red[0];
    }
    else if (temp < 27.0)
    {
        leds[0] = off;
        leds[1] = off;
        leds[2] = green[0];
        leds[3] = red[1];
    }
    else if (temp < 60.0)
    {
        leds[0] = off;
        leds[1] = off;
        leds[2] = green[0];
        leds[3] = red[2];
    }
}

void setPixel(int index, CRGB color)
{
    leds[index] = color;
}

#endif