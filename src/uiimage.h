// uiimage.h
//
// Inline picture thumbnails in the message history, drawn with "▀" half
// blocks: each cell shows two pixels, the upper as foreground and the lower
// as background color (xterm 256-color palette), so they appear in any
// 256-color terminal. Thumbnails are made by thumbnail_command in a
// background thread.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <string>

#include <ncurses.h>

class UiImage
{
public:
  static bool Enabled();
  static int Rows();

  // true when a thumbnail for p_Path is ready; sets a handle for DrawRow
  // and its size in cells (at most p_MaxCols wide)
  static bool GetThumbnail(const std::string& p_Path, int p_MaxCols, int& p_Handle, int& p_Cols, int& p_Rows);

  static void DrawRow(WINDOW* p_Win, int p_Y, int p_X, int p_Handle, int p_Row, int p_Cols);

  // true once after a thumbnail finished generating (history needs redraw)
  static bool TakeUpdated();
};
