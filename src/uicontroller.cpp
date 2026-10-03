// uicontroller.cpp
//
// Copyright (c) 2019-2023 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "uicontroller.h"

#include <cwchar>

#include <unistd.h>
#include <sys/select.h>

#include "uikeyconfig.h"
#include "uikeyinput.h"

MEVENT UiController::s_MouseEvent = {};

UiController::UiController()
{
}

UiController::~UiController()
{
}

void UiController::Init()
{
}

void UiController::Cleanup()
{
}

wint_t UiController::GetKey(int p_TimeOutMs, bool p_AllowMouse /*= false*/)
{
  static const wint_t keyMouse = UiKeyConfig::GetOffsettedKeyCode(KEY_MOUSE, true);
  static const wint_t keyUp = UiKeyConfig::GetKey("up");
  static const wint_t keyDown = UiKeyConfig::GetKey("down");

  fd_set fds;
  FD_ZERO(&fds);
  FD_SET(STDIN_FILENO, &fds);
  int maxfd = STDIN_FILENO;
  struct timeval tv = { (p_TimeOutMs / 1000), (p_TimeOutMs % 1000) * 1000 };
  wint_t key = 0;
  select(maxfd + 1, &fds, NULL, NULL, &tv); // ignore select() rv to get resize events
  if (FD_ISSET(STDIN_FILENO, &fds))
  {
    UiKeyInput::GetWch(&key);
  }

  if (key == keyMouse)
  {
    if (getmouse(&s_MouseEvent) != OK)
    {
      return 0;
    }

    // callers without mouse handling (dialogs) get the wheel as up/down keys
    if (!p_AllowMouse)
    {
      if (s_MouseEvent.bstate & BUTTON4_PRESSED) return keyUp;
      if (s_MouseEvent.bstate & BUTTON5_PRESSED) return keyDown;
      return 0;
    }
  }

  return key;
}

void UiController::SetMouseEnabled(bool p_Enabled)
{
  // reset first, so the terminal's mouse reporting is re-enabled after an
  // external program (attachment viewer, editor) switched it off
  mousemask(0, nullptr);
  if (p_Enabled)
  {
    mousemask(BUTTON1_PRESSED | BUTTON4_PRESSED | BUTTON5_PRESSED, nullptr);
    mouseinterval(0);
  }
}

const MEVENT& UiController::GetMouseEvent()
{
  return s_MouseEvent;
}
