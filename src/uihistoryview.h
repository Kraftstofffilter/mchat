// uihistoryview.h
//
// Copyright (c) 2019-2023 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <string>
#include <utility>
#include <vector>

#include "uiviewbase.h"

class UiHistoryView : public UiViewBase
{
public:
  UiHistoryView(const UiViewParams& p_Params);
  virtual ~UiHistoryView();

  virtual void Draw();
  int GetHistoryShowCount();

  // message offset (0 = newest) drawn at screen position, or -1
  int GetMessageOffsetAt(int p_Y, int p_X, bool* p_IsAttachment = nullptr);
  bool Contains(int p_Y, int p_X);
  bool IsScrollBarAt(int p_Y, int p_X);
  double GetScrollBarFraction(int p_Y);

private:
  std::string GetTimeString(int64_t p_TimeSent);

private:
  WINDOW* m_PaddedWin = nullptr;
  int m_PaddedH = 0;
  int m_PaddedW = 0;
  int m_HistoryShowCount = 0;

  // per padded row: offset of the message drawn there (-1 if none), and
  // whether the row is its attachment line
  std::vector<std::pair<int, bool>> m_RowHits;
};
