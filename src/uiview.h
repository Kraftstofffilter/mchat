// uiview.h
//
// Copyright (c) 2019-2025 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <memory>
#include <string>

class UiEntryView;
class UiHelpView;
class UiHistoryView;
class UiListView;
class UiListBorderView;
class UiModel;
class UiScreen;
class UiStatusView;
class UiTopView;

class UiView
{
public:
  UiView(UiModel* p_UiModel);
  virtual ~UiView();

  void TerminalBell();
  void SetEmojiEnabled(bool p_Enabled);
  bool GetEmojiEnabled();
  void SetTopEnabled(bool p_Enabled);
  bool GetTopEnabled();
  void SetHelpEnabled(bool p_Enabled);
  bool GetHelpEnabled();
  void SetListEnabled(bool p_Enabled);
  bool GetListEnabled();
  void SetListDirty(bool p_Dirty);
  void SetStatusDirty(bool p_Dirty);
  void SetHistoryDirty(bool p_Dirty);
  void SetHelpDirty(bool p_Dirty);
  void SetEntryDirty(bool p_Dirty);
  int GetHistoryShowCount();
  int GetHistoryLines();
  int GetEntryWidth();
  int GetListChatIndexAt(int p_Y, int p_X);
  int GetHistoryMessageOffsetAt(int p_Y, int p_X, bool* p_IsAttachment = nullptr);
  bool IsHistoryAt(int p_Y, int p_X);
  std::string GetHelpFuncAt(int p_Y, int p_X);
  void SetHistorySelection(int p_Y1, int p_X1, int p_Y2, int p_X2);
  void ClearHistorySelection();
  std::string GetHistorySelectionText();
  bool IsListAt(int p_Y, int p_X);
  bool IsListTopButtonAt(int p_Y, int p_X);
  bool IsListBorderAt(int p_Y, int p_X);
  void ListScrollBy(int p_Rows);
  void ListScrollToTop();
  int GetListWidth();
  void SetListWidth(int p_Width);
  int GetScreenWidth();
  int GetScreenHeight();
  void DecreaseListWidth();
  void IncreaseListWidth();

protected:
  void Init();
  void Draw();

private:
  UiModel* m_UiModel = nullptr;

  std::shared_ptr<UiScreen> m_UiScreen;

  std::shared_ptr<UiTopView> m_UiTopView;
  std::shared_ptr<UiHelpView> m_UiHelpView;
  std::shared_ptr<UiEntryView> m_UiEntryView;
  std::shared_ptr<UiStatusView> m_UiStatusView;
  std::shared_ptr<UiListView> m_UiListView;
  std::shared_ptr<UiListBorderView> m_UiListBorderView;
  std::shared_ptr<UiHistoryView> m_UiHistoryView;

  bool m_EmojiEnabled = true;
  bool m_TopEnabled = true;
  bool m_HelpEnabled = true;
  const bool m_EntryEnabled = true;
  bool m_StatusEnabled = true;
  bool m_ListEnabled = true;
  const bool m_HistoryEnabled = true;
  int m_ListWidth = 14;
  int m_EntryHeight = 4;

  friend class UiModel;
};
