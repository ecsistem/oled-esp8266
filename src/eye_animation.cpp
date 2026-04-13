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
  bool evilEyes = false;
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

  void drawDeadEyeSingle(int x, int y, bool leftEye, int phase)
  {
    int centerX = x + eyeWidth / 2;
    int centerY = y + eyeHeight / 2;
    int upperY = y + 8 + (phase % 2);
    int lowerY = y + eyeHeight - 8 - (phase % 2);
    int innerX = leftEye ? centerX + 2 : centerX - 2;
    int outerX = leftEye ? x + eyeWidth - 5 : x + 5;

    display.drawLine(innerX - 8, upperY, outerX, centerY - 1, BLACK);
    display.drawLine(innerX - 8, upperY + 1, outerX, centerY, BLACK);

    display.drawLine(innerX - 8, lowerY, outerX, centerY + 1, BLACK);
    display.drawLine(innerX - 8, lowerY - 1, outerX, centerY, BLACK);

    display.drawLine(centerX - 5, centerY - 2, centerX + 4, centerY + 2, BLACK);
    display.drawLine(centerX - 5, centerY + 2, centerX + 4, centerY - 2, BLACK);

    int glitchY = y + eyeHeight - 4 - (phase % 3);
    display.drawLine(x + 11, glitchY, x + eyeWidth - 11, glitchY, BLACK);
  }

  void drawDeadEyes(int leftX, int rightX, int y)
  {
    int phase = (millis() / 180) % 4;
    drawDeadEyeSingle(leftX, y, true, phase);
    drawDeadEyeSingle(rightX, y, false, phase + 1);
  }

  void drawEvilEyesShape(int leftX, int rightX, int y)
  {
    int leftTopX = leftX + 2;
    int leftTopY = y + 2;
    int leftBottomX = leftX + 2;
    int leftBottomY = y + eyeHeight - 2;
    int leftApexX = leftX + eyeWidth - 2;
    int leftApexY = y + eyeHeight / 2;

    int rightTopX = rightX + eyeWidth - 2;
    int rightTopY = y + 2;
    int rightBottomX = rightX + eyeWidth - 2;
    int rightBottomY = y + eyeHeight - 2;
    int rightApexX = rightX + 2;
    int rightApexY = y + eyeHeight / 2;

    display.fillTriangle(leftTopX, leftTopY, leftBottomX, leftBottomY, leftApexX, leftApexY, WHITE);
    display.fillTriangle(rightTopX, rightTopY, rightBottomX, rightBottomY, rightApexX, rightApexY, WHITE);

    display.drawTriangle(leftTopX, leftTopY, leftBottomX, leftBottomY, leftApexX, leftApexY, BLACK);
    display.drawTriangle(rightTopX, rightTopY, rightBottomX, rightBottomY, rightApexX, rightApexY, BLACK);
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
  bool hasApMode = WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA;
  bool captivePortalActive = captivePortalEnabled && hasApMode;

  deadEyes = !captivePortalActive && WiFi.status() != WL_CONNECTED;
  evilEyes = !deadEyes && (captivePortalActive || millis() < evilUntil);
  angryEyes = !deadEyes && !evilEyes && millis() < angryUntil;

  int totalEyesWidth = (eyeWidth * 2) + eyeGap;
  int startX = (SCREEN_WIDTH - totalEyesWidth) / 2;
  int y = 18;

  int leftX = startX;
  int rightX = startX + eyeWidth + eyeGap;

  if (evilEyes)
  {
    drawEvilEyesShape(leftX, rightX, y);
  }
  else
  {
    display.fillRoundRect(leftX, y, eyeWidth, eyeHeight, eyeRadius, WHITE);
    display.fillRoundRect(rightX, y, eyeWidth, eyeHeight, eyeRadius, WHITE);
  }

  int pupilMinX = -(eyeWidth / 2 - pupilRadius - 4);
  int pupilMaxX = (eyeWidth / 2 - pupilRadius - 4);
  int pupilMinY = -(eyeHeight / 2 - pupilRadius - 4);
  int pupilMaxY = (eyeHeight / 2 - pupilRadius - 4);

  int px = constrain(pupilOffsetX, pupilMinX, pupilMaxX);
  int py = constrain(pupilOffsetY, pupilMinY, pupilMaxY);

  int leftCenterX = leftX + eyeWidth / 2;
  int rightCenterX = rightX + eyeWidth / 2;
  int centerY = y + eyeHeight / 2;
  int leftPx = px;
  int rightPx = px;

  if (evilEyes)
  {
    leftPx = constrain(px + 7, pupilMinX, pupilMaxX);
    rightPx = constrain(px - 7, pupilMinX, pupilMaxX);
  }

  if (deadEyes)
  {
    drawDeadEyes(leftX, rightX, y);
    return;
  }

  if (evilEyes)
  {
    int leftPupilX = leftCenterX + leftPx;
    int rightPupilX = rightCenterX + rightPx;
    int topY = centerY + py - 5;
    int bottomY = centerY + py + 5;

    display.drawLine(leftPupilX, topY, leftPupilX, bottomY, BLACK);
    display.drawLine(leftPupilX + 1, topY, leftPupilX + 1, bottomY, BLACK);
    display.drawLine(rightPupilX, topY, rightPupilX, bottomY, BLACK);
    display.drawLine(rightPupilX + 1, topY, rightPupilX + 1, bottomY, BLACK);
  }
  else
  {
    display.fillCircle(leftCenterX + leftPx, centerY + py, pupilRadius, BLACK);
    display.fillCircle(rightCenterX + rightPx, centerY + py, pupilRadius, BLACK);
  }

  if (angryEyes)
  {
    drawAngryBrows(leftX, rightX, y);
    display.fillRect(leftX + 4, y + 2, eyeWidth - 8, 3, BLACK);
    display.fillRect(rightX + 4, y + 2, eyeWidth - 8, 3, BLACK);
  }

  if (eyelid > 0 && !evilEyes)
  {
    int topCover = eyelid / 2;
    int bottomCover = eyelid - topCover;

    display.fillRect(leftX, y, eyeWidth, topCover, BLACK);
    display.fillRect(rightX, y, eyeWidth, topCover, BLACK);

    display.fillRect(leftX, y + eyeHeight - bottomCover, eyeWidth, bottomCover, BLACK);
    display.fillRect(rightX, y + eyeHeight - bottomCover, eyeWidth, bottomCover, BLACK);
  }
}
