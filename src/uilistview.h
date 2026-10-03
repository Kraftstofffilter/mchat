// uilistview.h
//
// Copyright (c) 2019-2021 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <ncurses.h>

#include "uiviewbase.h"

class UiListView : public UiViewBase
{
public:
  UiListView(const UiViewParams& p_Params);
  virtual ~UiListView();

  virtual void Draw();

  // chat index drawn at screen position, or -1
  int GetChatIndexAt(int p_Y, int p_X);

  // "top" button in the top padding row, shown while the list is scrolled
  bool IsTopButtonAt(int p_Y, int p_X);
  void ScrollBy(int p_Rows);
  void ScrollToTop();

private:
  WINDOW* m_PaddedWin = nullptr;
  int m_PaddedH = 0;
  int m_PaddedW = 0;
  int m_DrawOffset = 0;
  int m_DrawCount = 0;
  // scroll position set by mouse, kept until the current chat changes
  int m_ManualOffset = -1;
  int m_ManualIndex = -1;
};
