// uifiles.h
//
// Size and cleanup of downloaded files: attachments downloaded by the
// protocols (WhatsApp tmp folders, Telegram media under tdlib), published
// attachment links (attachment_link_dir) and picture previews. Session data
// is never counted or deleted. The size is computed in a background thread.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <cstdint>
#include <string>

class UiFiles
{
public:
  // e.g. "Files 31M"; "Files ..." until the first computation finished
  static std::string SizeLabel();

  // recompute the size soon (after downloads or cleaning)
  static void Refresh();

  // delete the files; returns the number of bytes freed
  static uint64_t Clean();

  // true once after the size label changed
  static bool TakeChanged();
};
