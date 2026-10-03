// uihelpview.h
//
// Copyright (c) 2019-2024 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <string>
#include <vector>

#include "uiviewbase.h"

class UiHelpView : public UiViewBase
{
public:
  UiHelpView(const UiViewParams& p_Params);

  virtual void Draw();

  // key function (key.conf name) of the help item at a screen position, or ""
  std::string GetFuncAt(int p_Y, int p_X);

private:
  static std::vector<std::wstring> GetHelpViews(const int p_MaxW, const std::vector<std::wstring>& p_HelpItems,
                                                const std::wstring& p_OtherHelpItem);
  static void AppendHelpItem(const std::string& p_Func, const std::string& p_Desc,
                             std::vector<std::wstring>& p_HelpItems);
  static std::string GetKeyDisplay(const std::string& p_Func);

  std::wstring m_DrawnStr;
};
