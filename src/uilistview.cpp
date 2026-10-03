// uilistview.cpp
//
// Copyright (c) 2019-2025 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "uilistview.h"

#include "strutil.h"
#include "uicolorconfig.h"
#include "uiconfig.h"
#include "uimodel.h"

UiListView::UiListView(const UiViewParams& p_Params)
  : UiViewBase(p_Params)
{
  if (m_Enabled)
  {
    int paddedY = m_Y + 1;
    int paddedX = m_X + 1;
    m_PaddedH = m_H - 2;
    m_PaddedW = m_W - 2;
    m_PaddedWin = newwin(m_PaddedH, m_PaddedW, paddedY, paddedX);

    static int colorPair = UiColorConfig::GetColorPair("list_color");
    static int attribute = UiColorConfig::GetAttribute("list_attr");

    werase(m_Win);
    wbkgd(m_Win, attribute | colorPair | ' ');
    wrefresh(m_Win);
  }
}

UiListView::~UiListView()
{
  if (m_PaddedWin != nullptr)
  {
    delwin(m_PaddedWin);
    m_PaddedWin = nullptr;
  }
}

void UiListView::Draw()
{
  if (!m_Enabled || !m_Dirty) return;
  m_Dirty = false;

  curs_set(0);

  static int colorPair = UiColorConfig::GetColorPair("list_color");
  static int attribute = UiColorConfig::GetAttribute("list_attr");
  static int attributeSelected = UiColorConfig::GetAttribute("list_attr_selected");
  static int colorPairUnread = UiColorConfig::GetColorPair("list_color_unread");

  int index = std::max(0, m_Model->GetCurrentChatIndexLocked());
  const std::vector<std::pair<std::string, std::string>>& p_ChatVec = m_Model->GetChatVecLocked();

  const bool emojiEnabled = m_Model->GetEmojiEnabledLocked();
  std::vector<std::string> names;
  std::vector<std::wstring> protocols;
  std::vector<bool> unreads;
  static const bool showProtocol = UiConfig::GetBool("list_show_protocol");
  for (auto& chatPair : p_ChatVec)
  {
    const std::string& name = m_Model->GetContactListNameLocked(chatPair.first, chatPair.second, true /*p_AllowId*/,
                                                                true /*p_AllowAlias*/);
    bool isUnread = m_Model->GetChatIsUnreadLocked(chatPair.first, chatPair.second);
    names.push_back(name);
    unreads.push_back(isUnread);
    if (showProtocol)
    {
      // profile ids look like WhatsAppMd_+123 or Telegram_+123
      // short tag: @W (WhatsApp), @T (Telegram), @S (Signal)
      const std::string protocol = chatPair.first.substr(0, 1);
      protocols.push_back(StrUtil::ToWString(" @" + protocol));
    }
    else
    {
      protocols.push_back(L"");
    }
  }

  werase(m_PaddedWin);
  wbkgd(m_PaddedWin, attribute | colorPair | ' ');
  wattron(m_PaddedWin, attribute | colorPair);

  const int prevDrawCount = m_DrawCount;
  m_DrawCount = 0;
  if (!names.empty())
  {
    int height = m_PaddedH;
    int count = names.size();
    int offset = std::min(std::max(0, index - ((height - 1) / 2)), std::max(0, count - height));
    // keep the previous position while the current chat stays visible, so a
    // mouse click does not scroll the list under the pointer
    if ((prevDrawCount == count) && (index >= m_DrawOffset) && (index < (m_DrawOffset + height)) &&
        (m_DrawOffset <= std::max(0, count - height)))
    {
      offset = m_DrawOffset;
    }

    // mouse scrolling may move the selected chat out of view
    if ((m_ManualOffset >= 0) && (m_ManualIndex == index))
    {
      offset = std::min(m_ManualOffset, std::max(0, count - height));
    }
    else
    {
      m_ManualOffset = -1;
    }
    int last = std::min((height + offset), count);
    m_DrawOffset = offset;
    m_DrawCount = count;
    for (int i = offset; i < last; ++i)
    {
      if (i == index)
      {
        wattroff(m_PaddedWin, attribute);
        wattron(m_PaddedWin, attributeSelected);
      }

      int y = i - offset;
      std::string name = names[i];
      if (!emojiEnabled)
      {
        name = StrUtil::Textize(name);
      }

      // name shortened so the protocol tag and unread mark stay visible
      static const std::wstring wunreadMark = StrUtil::ToWString(" " + UiConfig::GetStr("unread_indicator"));
      const std::wstring& wprotocol = protocols[i];
      const int reserved = StrUtil::WStringWidth(wprotocol) + (unreads[i] ? StrUtil::WStringWidth(wunreadMark) : 0);
      const int nameW = std::max(m_PaddedW - reserved, 1);
      std::wstring wname = StrUtil::ToWString(name).substr(0, m_PaddedW);
      if (StrUtil::WStringWidth(wname) > nameW)
      {
        wname = StrUtil::TrimPadWString(wname, nameW);
      }

      wname = StrUtil::TrimPadWString(wname + wprotocol, m_PaddedW);

      if (unreads[i])
      {
        wattron(m_PaddedWin, colorPairUnread);
      }

      mvwaddnwstr(m_PaddedWin, y, 0, wname.c_str(), wname.size());

      if (unreads[i])
      {
        static const std::string unreadIndicator = " " + UiConfig::GetStr("unread_indicator");
        static const std::wstring wunread = StrUtil::ToWString(unreadIndicator);
        mvwaddnwstr(m_PaddedWin, y, (m_PaddedW - StrUtil::WStringWidth(wunread)), wunread.c_str(), wunread.size());

        wattron(m_PaddedWin, colorPair);
      }

      if (i == index)
      {
        wattroff(m_PaddedWin, attributeSelected);
        wattron(m_PaddedWin, attribute);
      }
    }
  }

  wattroff(m_PaddedWin, attribute | colorPair);

  // top padding row: "top" button while scrolled down
  {
    static const std::wstring topLabel = L"\u25B2 top";
    std::wstring row(std::max(m_W, 0), L' ');
    if ((m_DrawOffset > 0) && ((int)topLabel.size() <= m_W))
    {
      row.replace((m_W - topLabel.size()) / 2, topLabel.size(), topLabel);
    }

    wattron(m_Win, attribute | colorPair);
    mvwaddnwstr(m_Win, 0, 0, row.c_str(), row.size());
    wattroff(m_Win, attribute | colorPair);
    wnoutrefresh(m_Win);
  }

  wrefresh(m_PaddedWin);
}

bool UiListView::IsTopButtonAt(int p_Y, int p_X)
{
  return m_Enabled && (m_DrawOffset > 0) && (p_Y == m_Y) && (p_X >= m_X) && (p_X < (m_X + m_W));
}

void UiListView::ScrollBy(int p_Rows)
{
  const int maxOffset = std::max(0, m_DrawCount - m_PaddedH);
  m_ManualOffset = std::min(std::max(m_DrawOffset + p_Rows, 0), maxOffset);
  m_ManualIndex = std::max(0, m_Model->GetCurrentChatIndexLocked());
  SetDirty(true);
}

void UiListView::ScrollToTop()
{
  m_ManualOffset = 0;
  m_ManualIndex = std::max(0, m_Model->GetCurrentChatIndexLocked());
  SetDirty(true);
}

int UiListView::GetChatIndexAt(int p_Y, int p_X)
{
  if (!m_Enabled) return -1;

  const int row = p_Y - (m_Y + 1);
  const int col = p_X - (m_X + 1);
  if ((row < 0) || (row >= m_PaddedH) || (col < 0) || (col >= m_PaddedW)) return -1;

  const int index = m_DrawOffset + row;
  return (index < m_DrawCount) ? index : -1;
}
