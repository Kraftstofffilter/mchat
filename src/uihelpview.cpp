// uihelpview.cpp
//
// Copyright (c) 2019-2026 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "uihelpview.h"

#include <unordered_map>

#include <algorithm>

#include "strutil.h"
#include "uicolorconfig.h"
#include "uiconfig.h"
#include "uikeyconfig.h"
#include "uifiles.h"
#include "uimodel.h"

UiHelpView::UiHelpView(const UiViewParams& p_Params)
  : UiViewBase(p_Params)
{
}

void UiHelpView::Draw()
{
  if (!m_Enabled || !m_Dirty) return;
  m_Dirty = false;

  curs_set(0);

  static std::wstring otherHelpItem = []()
  {
    std::vector<std::wstring> helpItems;
    AppendHelpItem("other_commands_help", "OtherCmd", helpItems);
    return !helpItems.empty() ? L" | " + helpItems.at(0) : std::wstring();
  }();

  static std::vector<std::wstring> listDialogHelpItems = []()
  {
    std::vector<std::wstring> helpItems;
    AppendHelpItem("ok", "Select", helpItems);
    AppendHelpItem("cancel", "Cancel", helpItems);
    AppendHelpItem("abc", "AddFiltr", helpItems);
    AppendHelpItem("backspace", "DelFiltr", helpItems);
    return helpItems;
  }();

  static std::vector<std::wstring> fileListDialogHelpItems = []()
  {
    std::vector<std::wstring> helpItems;
    AppendHelpItem("ok", "Select", helpItems);
    AppendHelpItem("cancel", "Cancel", helpItems);
    AppendHelpItem("abc", "AddFiltr", helpItems);
    AppendHelpItem("backspace", "DelFiltr", helpItems);
    AppendHelpItem("left", "Parent", helpItems);
    return helpItems;
  }();

  static std::vector<std::wstring> messageDialogHelpItems = []()
  {
    std::vector<std::wstring> helpItems;
    AppendHelpItem("ok", "OK", helpItems);
    AppendHelpItem("cancel", "Cancel", helpItems);
    return helpItems;
  }();

  static std::vector<std::wstring> editMessageHelpItems = []()
  {
    std::vector<std::wstring> helpItems;
    AppendHelpItem("send_msg", "Save", helpItems);
    AppendHelpItem("cancel", "Cancel", helpItems);
    return helpItems;
  }();

  static std::vector<std::wstring> mainPreHelpItems = []()
  {
    std::vector<std::wstring> helpItems;
    AppendHelpItem("send_msg", "SendMsg", helpItems);
    AppendHelpItem("next_chat", "NextChat", helpItems);
    AppendHelpItem("unread_chat", "JumpUnrd", helpItems);

    AppendHelpItem("quit", "Quit", helpItems);
    AppendHelpItem("select_emoji", "AddEmoji", helpItems);
    AppendHelpItem("goto_chat", "GotoChat", helpItems);
    AppendHelpItem("transfer", "SendFile", helpItems);

    return helpItems;
  }();

  static std::vector<std::wstring> mainPostHelpItems = []()
  {
    std::vector<std::wstring> helpItems;
    AppendHelpItem("ext_edit", "ExtEdit", helpItems);
    AppendHelpItem("ext_call", "ExtCall", helpItems);
    AppendHelpItem("find", "Find", helpItems);
    AppendHelpItem("find_next", "FindNext", helpItems);
    AppendHelpItem("select_contact", "AddrBook", helpItems);
    AppendHelpItem("select_mention", "Mention", helpItems);
    AppendHelpItem("spell", "ExtSpell", helpItems);
    AppendHelpItem("auto_compose", "AutoComp", helpItems);
    AppendHelpItem("decrease_list_width", "DecListW", helpItems);
    AppendHelpItem("increase_list_width", "IncListW", helpItems);

    AppendHelpItem("cut", "Cut", helpItems);
    AppendHelpItem("copy", "Copy", helpItems);
    AppendHelpItem("paste", "Paste", helpItems);

    AppendHelpItem("toggle_emoji", "TgEmoji", helpItems);
    AppendHelpItem("toggle_list", "TgList", helpItems);
    AppendHelpItem("toggle_top", "TgTop", helpItems);
    AppendHelpItem("toggle_help", "TgHelp", helpItems);

    return helpItems;
  }();

  static std::vector<std::wstring> mainSelectHelpItems = []()
  {
    std::vector<std::wstring> helpItems;
    helpItems.insert(std::end(helpItems), std::begin(mainPreHelpItems), std::end(mainPreHelpItems));

    AppendHelpItem("up", "PrevMsg", helpItems);
    AppendHelpItem("down", "NextMsg", helpItems);

    AppendHelpItem("delete_msg", "DelMsg", helpItems);
    AppendHelpItem("edit_msg", "EditMsg", helpItems);
    AppendHelpItem("open", "OpenFile", helpItems);
    AppendHelpItem("save", "SaveFile", helpItems);
    AppendHelpItem("open_link", "OpenLink", helpItems);

    AppendHelpItem("jump_quoted", "JumpQuoted", helpItems);
    AppendHelpItem("react", "AddReact", helpItems);
    AppendHelpItem("open_msg", "ExtView", helpItems);
    AppendHelpItem("forward_msg", "FwdMsg", helpItems);

    helpItems.insert(std::end(helpItems), std::begin(mainPostHelpItems), std::end(mainPostHelpItems));
    return helpItems;
  }();

  static std::vector<std::wstring> mainDefaultHelpItems = []()
  {
    std::vector<std::wstring> helpItems;
    helpItems.insert(std::end(helpItems), std::begin(mainPreHelpItems), std::end(mainPreHelpItems));

    AppendHelpItem("up", "SelectMsg", helpItems);
    AppendHelpItem("delete_chat", "DelChat", helpItems);
    AppendHelpItem("archive_chat", "ArcChat", helpItems);
    AppendHelpItem("pin", "Pin", helpItems);

    helpItems.insert(std::end(helpItems), std::begin(mainPostHelpItems), std::end(mainPostHelpItems));
    return helpItems;
  }();

  static std::vector<std::wstring> listDialogHelpViews;
  static std::vector<std::wstring> fileListDialogHelpViews;
  static std::vector<std::wstring> messageDialogHelpViews;
  static std::vector<std::wstring> editMessageHelpViews;
  static std::vector<std::wstring> selectHelpViews;
  static std::vector<std::wstring> defaultHelpViews;

  static int prevW = 0;
  static std::string prevFilesLabel;
  const std::string filesLabel = UiFiles::SizeLabel();
  if ((m_W != prevW) || (filesLabel != prevFilesLabel))
  {
    prevW = m_W;
    prevFilesLabel = filesLabel;

    // last command: size of downloaded files, cleaned on click or key
    std::vector<std::wstring> selectItems = mainSelectHelpItems;
    std::vector<std::wstring> defaultItems = mainDefaultHelpItems;
    AppendHelpItem("clean_files", filesLabel, selectItems);
    AppendHelpItem("clean_files", filesLabel, defaultItems);

    const int maxW = m_W - 2;
    listDialogHelpViews = GetHelpViews(maxW, listDialogHelpItems, otherHelpItem);
    fileListDialogHelpViews = GetHelpViews(maxW, fileListDialogHelpItems, otherHelpItem);
    messageDialogHelpViews = GetHelpViews(maxW, messageDialogHelpItems, otherHelpItem);
    editMessageHelpViews = GetHelpViews(maxW, editMessageHelpItems, otherHelpItem);
    selectHelpViews = GetHelpViews(maxW, selectItems, otherHelpItem);
    defaultHelpViews = GetHelpViews(maxW, defaultItems, otherHelpItem);
  }

  static int colorPair = UiColorConfig::GetColorPair("help_color");
  static int attribute = UiColorConfig::GetAttribute("help_attr");

  werase(m_Win);
  wbkgd(m_Win, attribute | colorPair | ' ');
  wattron(m_Win, attribute | colorPair);

  std::wstring wstr;
  if (m_Model->GetFileListDialogActiveLocked())
  {
    wstr = fileListDialogHelpViews.at(m_Model->GetHelpOffsetLocked() % fileListDialogHelpViews.size());
  }
  else if (m_Model->GetListDialogActiveLocked())
  {
    wstr = listDialogHelpViews.at(m_Model->GetHelpOffsetLocked() % listDialogHelpViews.size());
  }
  else if (m_Model->GetMessageDialogActiveLocked())
  {
    wstr = messageDialogHelpViews.at(m_Model->GetHelpOffsetLocked() % messageDialogHelpViews.size());
  }
  else if (m_Model->GetEditMessageActiveLocked())
  {
    wstr = editMessageHelpViews.at(m_Model->GetHelpOffsetLocked() % editMessageHelpViews.size());
  }
  else if (m_Model->GetSelectMessageActiveLocked())
  {
    wstr = selectHelpViews.at(m_Model->GetHelpOffsetLocked() % selectHelpViews.size());
  }
  else
  {
    wstr = defaultHelpViews.at(m_Model->GetHelpOffsetLocked() % defaultHelpViews.size());
  }

  m_DrawnStr = wstr;
  wstr = L" " + wstr + std::wstring(std::max(m_W - (int)wstr.size(), 0), L' ');
  mvwaddnwstr(m_Win, 0, 0, wstr.c_str(), std::min((int)wstr.size(), m_W));

  wattroff(m_Win, attribute | colorPair);
  wrefresh(m_Win);
}

std::vector<std::wstring> UiHelpView::GetHelpViews(const int p_MaxW, const std::vector<std::wstring>& p_HelpItems,
                                                   const std::wstring& p_OtherHelpItem)
{
  std::vector<std::wstring> helpViews;
  std::wstring helpView = StrUtil::Join(p_HelpItems, L" | ");
  if (StrUtil::WStringWidth(helpView) <= p_MaxW)
  {
    helpViews.push_back(helpView);
  }
  else
  {
    helpView.clear();
    for (int i = 0; i < (int)p_HelpItems.size(); ++i)
    {
      if (helpView.empty())
      {
        helpView = p_HelpItems.at(i);
      }
      else
      {
        if (StrUtil::WStringWidth(helpView + L" | " + p_HelpItems.at(i) + p_OtherHelpItem) < p_MaxW)
        {
          helpView += L" | " + p_HelpItems.at(i);
        }
        else
        {
          helpView += p_OtherHelpItem;
          helpViews.push_back(helpView);
          helpView = p_HelpItems.at(i);
        }
      }
    }

    if (!helpView.empty())
    {
      helpView += p_OtherHelpItem;
      helpViews.push_back(helpView);
    }
  }

  return helpViews;
}

// help item text -> key function, for clicking on items
static std::unordered_map<std::wstring, std::string>& HelpItemFuncs()
{
  static std::unordered_map<std::wstring, std::string> s_HelpItemFuncs;
  return s_HelpItemFuncs;
}

void UiHelpView::AppendHelpItem(const std::string& p_Func, const std::string& p_Desc,
                                std::vector<std::wstring>& p_HelpItems)
{
  const std::string keyDisplay = GetKeyDisplay(p_Func);
  if (!keyDisplay.empty())
  {
    const std::string helpItem = keyDisplay + " " + p_Desc;
    p_HelpItems.push_back(StrUtil::ToWString(helpItem));
    HelpItemFuncs()[p_HelpItems.back()] = p_Func;
  }
}

std::string UiHelpView::GetFuncAt(int p_Y, int p_X)
{
  if (!m_Enabled || (p_Y != m_Y) || m_DrawnStr.empty()) return "";

  // items are drawn after one leading space, joined by " | "
  static const std::wstring separator = L" | ";
  int col = m_X + 1;
  size_t pos = 0;
  while (pos <= m_DrawnStr.size())
  {
    size_t end = m_DrawnStr.find(separator, pos);
    if (end == std::wstring::npos) end = m_DrawnStr.size();

    const std::wstring item = m_DrawnStr.substr(pos, end - pos);
    const int width = StrUtil::WStringWidth(item);
    if ((p_X >= col) && (p_X < (col + width)))
    {
      auto it = HelpItemFuncs().find(item);
      return (it != HelpItemFuncs().end()) ? it->second : "";
    }

    col += width + StrUtil::WStringWidth(separator);
    if (end == m_DrawnStr.size()) break;

    pos = end + separator.size();
  }

  return "";
}

std::string UiHelpView::GetKeyDisplay(const std::string& p_Func)
{
  if (p_Func == "abc") return "abc";

  const std::string keyName = UiKeyConfig::GetStr(p_Func);
  if ((keyName.size() == 9) && (keyName >= "KEY_CTRLA") && (keyName <= "KEY_CTRLZ"))
  {
    static const std::string helpPrefixCtrl = UiConfig::GetStr("help_prefix_ctrl");
    return helpPrefixCtrl + keyName.substr(8, 1);
  }
  else if (std::count(keyName.begin(), keyName.end(), '\\') == 2)
  {
    const std::string keyStr = StrUtil::StrFromOct(keyName);
    if ((keyStr.size() == 2) && (keyStr.at(0) == '\33') && StrUtil::IsValidTextKey(keyStr.at(1)))
    {
      static const std::string helpPrefixMeta = UiConfig::GetStr("help_prefix_meta");
      return helpPrefixMeta + keyStr.substr(1);
    }
  }
  else if (keyName == "KEY_RETURN")
  {
    return "\xe2\x8f\x8e";
  }
  else if (keyName == "KEY_TAB")
  {
    return "Tab";
  }
  else if (keyName == "KEY_BTAB")
  {
    return "STab";
  }
  else if (keyName == "KEY_UP")
  {
    return "\xe2\x86\x91";
  }
  else if (keyName == "KEY_DOWN")
  {
    return "\xe2\x86\x93";
  }
  else if (keyName == "KEY_LEFT")
  {
    return "\xe2\x86\x90";
  }
  else if (keyName == "KEY_RIGHT")
  {
    return "\xe2\x86\x92";
  }
  else if (keyName == "KEY_BACKSPACE")
  {
    return "\xe2\x8c\xab";
  }

  return "";
}
