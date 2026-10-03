// uiviewbase.cpp
//
// Copyright (c) 2019-2021 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "uiviewbase.h"

#include <algorithm>

UiViewBase::UiViewBase(const UiViewParams& p_Params)
  : m_X(p_Params.x)
  , m_Y(p_Params.y)
  , m_W(p_Params.w)
  , m_H(p_Params.h)
  , m_Enabled(p_Params.enabled)
  , m_Model(p_Params.model)
  , m_Dirty(true)
{
  if (!m_Enabled) return;

  m_Win = newwin(m_H, m_W, m_Y, m_X);
}

UiViewBase::~UiViewBase()
{
  if (!m_Enabled) return;

  delwin(m_Win);
  m_Win = nullptr;
}

int UiViewBase::W()
{
  return m_Enabled ? m_W : 0;
}

int UiViewBase::H()
{
  return m_Enabled ? m_H : 0;
}

int UiViewBase::X()
{
  return m_Enabled ? m_X : 0;
}

int UiViewBase::Y()
{
  return m_Enabled ? m_Y : 0;
}

void UiViewBase::SetDirty(bool p_Dirty)
{
  m_Dirty = p_Dirty;
}

// Draws a vertical scroll bar in column p_X of p_Win, rows p_Y..p_Y+p_H-1.
// p_Total items, p_Shown visible, p_FirstShown = index of the first visible
// item counted from the top. Draws nothing when everything fits.
void UiViewBase::DrawScrollBar(WINDOW* p_Win, int p_Y, int p_X, int p_H, int p_Total, int p_Shown, int p_FirstShown)
{
  if ((p_Win == nullptr) || (p_H <= 0)) return;

  for (int i = 0; i < p_H; ++i)
  {
    mvwaddnwstr(p_Win, p_Y + i, p_X, L" ", 1);
  }

  if ((p_Total <= 0) || (p_Shown >= p_Total)) return;

  const int thumbH = std::max(1, (p_H * p_Shown) / p_Total);
  int thumbY = (p_H * p_FirstShown) / p_Total;
  thumbY = std::min(std::max(thumbY, 0), p_H - thumbH);

  for (int i = 0; i < p_H; ++i)
  {
    const bool isThumb = (i >= thumbY) && (i < (thumbY + thumbH));
    mvwaddnwstr(p_Win, p_Y + i, p_X, isThumb ? L"\u2588" : L"\u2502", 1);
  }
}
