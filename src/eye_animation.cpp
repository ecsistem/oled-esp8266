#include "eye_animation.h"

#include <Arduino.h>
#include <ESP8266WiFi.h>

#include "app_state.h"

namespace
{
  const int eyeWidth = 38;
  const int eyeHeight = 24;
  const int eyeRadius = 8;
  const int eyeGap = 10;
  const int pupilRadius = 4;

  int pupilOffsetX = 0;
  int pupilOffsetY = 0;
  int eyelid = 0;
  bool closingBlink = false;
  bool angryEyes = false;
  bool deadEyes = false;

  unsigned long lastEyeFrame = 0;
  unsigned long nextBlinkAt = 0;
  unsigned long nextLookAt = 0;

  void drawAngryBrows(int leftX, int rightX, int y)
  {
    display.drawLine(leftX + 6, y + 6, leftX + eyeWidth - 6, y + 1, BLACK);
    display.drawLine(leftX + 6, y + 7, leftX + eyeWidth - 6, y + 2, BLACK);

    display.drawLine(rightX + 6, y + 1, rightX + eyeWidth - 6, y + 6, BLACK);
    display.drawLine(rightX + 6, y + 2, rightX + eyeWidth - 6, y + 7, BLACK);
  }

  void drawDeadEyes(int leftX, int rightX, int y)
  {
    int eyeCenterY = y + eyeHeight / 2;

    display.drawLine(leftX + 8, y + 6, leftX + eyeWidth - 8, y + eyeHeight - 6, BLACK);
    display.drawLine(leftX + 8, y + eyeHeight - 6, leftX + eyeWidth - 8, y + 6, BLACK);

    display.drawLine(rightX + 8, y + 6, rightX + eyeWidth - 8, y + eyeHeight - 6, BLACK);
    display.drawLine(rightX + 8, y + eyeHeight - 6, rightX + eyeWidth - 8, y + 6, BLACK);

    display.drawLine(leftX + 12, eyeCenterY + 9, leftX + eyeWidth - 12, eyeCenterY + 9, BLACK);
    display.drawLine(rightX + 12, eyeCenterY + 9, rightX + eyeWidth - 12, eyeCenterY + 9, BLACK);
  }
} // namespace

void updateEyeAnimation()
{
  unsigned long now = millis();

  if (now - lastEyeFrame < 40)
  {
    return;
  }

  lastEyeFrame = now;

  if (nextBlinkAt == 0)
  {
    nextBlinkAt = now + random(1500, 3500);
    nextLookAt = now + random(600, 1200);
  }

  if (now >= nextBlinkAt || eyelid > 0)
  {
    if (eyelid == 0)
    {
      closingBlink = true;
    }

    if (closingBlink)
    {
      eyelid += 4;
      if (eyelid >= eyeHeight)
      {
        eyelid = eyeHeight;
        closingBlink = false;
      }
    }
    else
    {
      eyelid -= 4;
      if (eyelid <= 0)
      {
        eyelid = 0;
        nextBlinkAt = now + random(2000, 4500);
      }
    }
  }

  if (now >= nextLookAt && eyelid < eyeHeight - 6)
  {
    int look = random(0, 5);

    if (look == 0)
    {
      pupilOffsetX = 0;
      pupilOffsetY = 0;
    }
    else if (look == 1)
    {
      pupilOffsetX = -6;
      pupilOffsetY = 0;
    }
    else if (look == 2)
    {
      pupilOffsetX = 6;
      pupilOffsetY = 0;
    }
    else if (look == 3)
    {
      pupilOffsetX = 0;
      pupilOffsetY = -3;
    }
    else
    {
      pupilOffsetX = 0;
      pupilOffsetY = 3;
    }

    nextLookAt = now + random(600, 1600);
  }
}

void drawEyeAnimation()
{
  deadEyes = WiFi.status() != WL_CONNECTED;
  angryEyes = !deadEyes && millis() < angryUntil;

  int totalEyesWidth = (eyeWidth * 2) + eyeGap;
  int startX = (SCREEN_WIDTH - totalEyesWidth) / 2;
  int y = 18;

  int leftX = startX;
  int rightX = startX + eyeWidth + eyeGap;

  display.fillRoundRect(leftX, y, eyeWidth, eyeHeight, eyeRadius, WHITE);
  display.fillRoundRect(rightX, y, eyeWidth, eyeHeight, eyeRadius, WHITE);

  int pupilMinX = -(eyeWidth / 2 - pupilRadius - 4);
  int pupilMaxX = (eyeWidth / 2 - pupilRadius - 4);
  int pupilMinY = -(eyeHeight / 2 - pupilRadius - 4);
  int pupilMaxY = (eyeHeight / 2 - pupilRadius - 4);

  int px = constrain(pupilOffsetX, pupilMinX, pupilMaxX);
  int py = constrain(pupilOffsetY, pupilMinY, pupilMaxY);

  int leftCenterX = leftX + eyeWidth / 2;
  int rightCenterX = rightX + eyeWidth / 2;
  int centerY = y + eyeHeight / 2;

  if (deadEyes)
  {
    drawDeadEyes(leftX, rightX, y);
    return;
  }

  display.fillCircle(leftCenterX + px, centerY + py, pupilRadius, BLACK);
  display.fillCircle(rightCenterX + px, centerY + py, pupilRadius, BLACK);

  if (angryEyes)
  {
    drawAngryBrows(leftX, rightX, y);
    display.fillRect(leftX + 4, y + 2, eyeWidth - 8, 3, BLACK);
    display.fillRect(rightX + 4, y + 2, eyeWidth - 8, 3, BLACK);
  }

  if (eyelid > 0)
  {
    int topCover = eyelid / 2;
    int bottomCover = eyelid - topCover;

    display.fillRect(leftX, y, eyeWidth, topCover, BLACK);
    display.fillRect(rightX, y, eyeWidth, topCover, BLACK);

    display.fillRect(leftX, y + eyeHeight - bottomCover, eyeWidth, bottomCover, BLACK);
    display.fillRect(rightX, y + eyeHeight - bottomCover, eyeWidth, bottomCover, BLACK);
  }
}
