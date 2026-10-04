// uilock.h
//
// Privacy lock: hides the whole UI behind a black screen after a period
// without input, when the terminal loses focus, or on request, until a PIN
// is entered. The PIN is stored only as a salted PBKDF2-HMAC-SHA256 hash in
// <confdir>/lock.pin. Protects against people looking at the screen, not
// against someone with keyboard and shell access.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <cstdint>
#include <string>

#include <ncurses.h>

class UiLock
{
public:
  static bool IsLocked();
  static void Lock();

  // any user input while unlocked; restarts the idle timer
  static void NoteActivity();
  // seconds since the last user input
  static int64_t IdleSec();

  // input while locked; returns true when the UI just got unlocked
  static bool Key(wint_t p_Key);

  // draws the lock screen when it changed (or p_Force)
  static void Draw(bool p_Force = false);
  static void SetDirty();

  // --set-pin: interactive PIN change on the terminal; returns exit code
  static int SetPinCli();
};
