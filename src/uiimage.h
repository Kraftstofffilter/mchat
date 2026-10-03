// uiimage.h
//
// Inline picture thumbnails in the message history, drawn with the Kitty
// graphics protocol's Unicode placeholders: the picture is transmitted
// once per image id, and the history then draws ordinary placeholder
// characters (U+10EEEE plus row/column diacritics) whose foreground color
// selects the image. Thumbnails are made by thumbnail_command in a
// background thread.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <string>

class UiImage
{
public:
  static bool Enabled();
  static int Rows();

  // true when a thumbnail for p_Path is ready and transmitted; sets the
  // color pair to draw its placeholders with and the size in cells
  static bool GetThumbnail(const std::string& p_Path, int p_MaxCols, int& p_ColorPair, int& p_Cols, int& p_Rows);

  // placeholder characters for one thumbnail row
  static std::wstring PlaceholderRow(int p_Row, int p_Cols);
  static bool IsPlaceholderLine(const std::wstring& p_Line);

  // true once after a thumbnail finished generating (history needs redraw)
  static bool TakeUpdated();

  // forget transmitted images, e.g. after an external program cleared them
  static void InvalidateTransmitted();
};
