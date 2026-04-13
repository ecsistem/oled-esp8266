#include "eye_animation.h"

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <FluxGarage_RoboEyes.h>

#include "app_state.h"

namespace
{
  RoboEyes<Adafruit_SSD1306> roboEyes(display);

  enum class EyeMode
  {
    normal,
    angry,
    evil,
    dead
  };

  bool roboEyesInitialized = false;
  EyeMode currentMode = EyeMode::normal;

  void initRoboEyesIfNeeded()
  {
    if (roboEyesInitialized)
    {
      return;
    }

    roboEyes.begin(SCREEN_WIDTH, SCREEN_HEIGHT, 100);
    roboEyes.setWidth(38, 38);
    roboEyes.setHeight(24, 24);
    roboEyes.setBorderradius(8, 8);
    roboEyes.setSpacebetween(10);
    roboEyes.setMood(DEFAULT);
    roboEyes.setPosition(DEFAULT);
    roboEyes.setAutoblinker(ON, 3, 2);
    roboEyes.setIdleMode(ON, 2, 2);
    roboEyes.open();

    roboEyesInitialized = true;
  }

  void applyEyeMode(EyeMode mode)
  {
    if (mode == currentMode)
    {
      return;
    }

    currentMode = mode;

    if (mode == EyeMode::dead)
    {
      roboEyes.setMood(TIRED);
      roboEyes.setPosition(DEFAULT);
      roboEyes.setAutoblinker(OFF, 3, 2);
      roboEyes.setIdleMode(OFF, 2, 2);
      roboEyes.close();
      return;
    }

    roboEyes.open();

    if (mode == EyeMode::evil)
    {
      roboEyes.setMood(ANGRY);
      roboEyes.setHeight(20, 20);
      roboEyes.setBorderradius(6, 6);
      roboEyes.setPosition(DEFAULT);
      roboEyes.setAutoblinker(ON, 2, 1);
      roboEyes.setIdleMode(ON, 1, 1);
      return;
    }

    if (mode == EyeMode::angry)
    {
      roboEyes.setMood(ANGRY);
      roboEyes.setHeight(24, 24);
      roboEyes.setBorderradius(8, 8);
      roboEyes.setPosition(DEFAULT);
      roboEyes.setAutoblinker(ON, 3, 2);
      roboEyes.setIdleMode(ON, 2, 1);
      return;
    }

    roboEyes.setMood(DEFAULT);
    roboEyes.setHeight(24, 24);
    roboEyes.setBorderradius(8, 8);
    roboEyes.setPosition(DEFAULT);
    roboEyes.setAutoblinker(ON, 3, 2);
    roboEyes.setIdleMode(ON, 2, 2);
  }
} // namespace

void updateEyeAnimation()
{
  initRoboEyesIfNeeded();

  bool hasApMode = WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA;
  bool captivePortalActive = captivePortalEnabled && hasApMode;
  bool deadEyes = !captivePortalActive && WiFi.status() != WL_CONNECTED;
  bool evilEyes = !deadEyes && (captivePortalActive || millis() < evilUntil);
  bool angryEyes = !deadEyes && !evilEyes && millis() < angryUntil;

  if (deadEyes)
  {
    applyEyeMode(EyeMode::dead);
  }
  else if (evilEyes)
  {
    applyEyeMode(EyeMode::evil);
  }
  else if (angryEyes)
  {
    applyEyeMode(EyeMode::angry);
  }
  else
  {
    applyEyeMode(EyeMode::normal);
  }
}

void drawEyeAnimation()
{
  initRoboEyesIfNeeded();
  roboEyes.update();
}
