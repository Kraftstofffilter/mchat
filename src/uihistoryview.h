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

  // text selection by mouse drag, in screen coordinates
  void SetSelection(int p_Y1, int p_X1, int p_Y2, int p_X2);
  void ClearSelection();
  std::string GetSelectionText();

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

  // attachment lines drawn this pass that get an OSC 8 hyperlink
  struct LinkRow
  {
    int y = 0;
    std::string url;
    std::wstring text;
    bool selected = false;
  };
  std::vector<LinkRow> m_LinkRows;

  bool m_SelectionActive = false;
  int m_SelY1 = 0;
  int m_SelX1 = 0;
  int m_SelY2 = 0;
  int m_SelX2 = 0;
  std::vector<bool> m_ThumbRows;
  // selection in padded-window rows/cols, ordered; false if none
  bool GetSelectionRange(int& p_Row1, int& p_Col1, int& p_Row2, int& p_Col2);
  void DrawSelection();
  void EmitLinks();
};
