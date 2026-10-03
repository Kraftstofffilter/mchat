// uihistoryview.cpp
//
// Copyright (c) 2019-2025 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "uihistoryview.h"

#include <unistd.h>

#include <cstdio>
#include <cwctype>
#include <functional>
#include <unordered_map>

#include "appconfig.h"
#include "apputil.h"
#include "fileutil.h"
#include "log.h"
#include "protocolutil.h"
#include "strutil.h"
#include "timeutil.h"
#include "uicolorconfig.h"
#include "uiconfig.h"
#include "uiimage.h"
#include "uimodel.h"

// Publishes a downloaded attachment under attachment_link_dir (hard link,
// or copy across filesystems) and returns its URL under attachment_link_base.
// Returns an empty string when links are disabled or the file is missing.
static std::string GetAttachmentLink(const std::string& p_FilePath)
{
  static const std::string linkBase = UiConfig::GetStr("attachment_link_base");
  if (linkBase.empty() || p_FilePath.empty()) return "";

  if (!FileUtil::Exists(p_FilePath)) return "";

  // cached URL, as long as the published file still exists (Alt-k cleans)
  static std::unordered_map<std::string, std::pair<std::string, std::string>> s_Links;
  auto it = s_Links.find(p_FilePath);
  if ((it != s_Links.end()) && FileUtil::Exists(it->second.second)) return it->second.first;

  static const std::string linkDir = FileUtil::ExpandPath(UiConfig::GetStr("attachment_link_dir"));
  char id[9];
  snprintf(id, sizeof(id), "%08x", (unsigned)(std::hash<std::string>{}(p_FilePath) & 0xffffffff));
  const std::string name = FileUtil::BaseName(p_FilePath);
  const std::string dir = linkDir + "/" + id;
  const std::string dstPath = dir + "/" + name;
  if (!FileUtil::Exists(dstPath))
  {
    FileUtil::MkDir(dir);
    if (link(p_FilePath.c_str(), dstPath.c_str()) != 0)
    {
      FileUtil::CopyFile(p_FilePath, dstPath);
    }
  }

  std::string encodedName;
  for (unsigned char c : name)
  {
    if (isalnum(c) || (c == '-') || (c == '_') || (c == '.') || (c == '~'))
    {
      encodedName += c;
    }
    else
    {
      char hex[4];
      snprintf(hex, sizeof(hex), "%%%02X", c);
      encodedName += hex;
    }
  }

  const std::string url = linkBase + "/" + id + "/" + encodedName;
  s_Links[p_FilePath] = std::make_pair(url, dstPath);
  return url;
}

// Draws one message text line with WhatsApp / Telegram style formatting:
// *bold*, _italic_, ~strikethrough~ (dim; curses has no strikethrough),
// `code` and ```code``` (underlined), also **bold** and __italic__. Markers
// are hidden. A marker opens after a boundary (line start, space or
// punctuation) before a non-space, and closes after a non-space before a
// boundary on the same line, so snake_case or 2*3*4 stay as they are.
static void DrawFormattedLine(WINDOW* p_Win, int p_Y, int p_Width, const std::wstring& p_Line, attr_t p_Attr)
{
  const int n = p_Line.size();
  std::vector<attr_t> attrs(n, 0);
  std::vector<bool> hidden(n, false);
  auto isBoundary = [&](int i)
  {
    return (i < 0) || (i >= n) || iswspace(p_Line[i]) || iswpunct(p_Line[i]);
  };

  for (int i = 0; i < n; )
  {
    const wchar_t c = p_Line[i];
    if (hidden[i])
    {
      ++i;
      continue;
    }

    if (c == L'`')
    {
      const int len = ((i + 2) < n) && (p_Line[i + 1] == L'`') && (p_Line[i + 2] == L'`') ? 3 : 1;
      const size_t j = p_Line.find(p_Line.substr(i, len), i + len);
      if ((j != std::wstring::npos) && ((int)j > (i + len)))
      {
        for (int k = 0; k < len; ++k)
        {
          hidden[i + k] = true;
          hidden[j + k] = true;
        }

        for (int k = i + len; k < (int)j; ++k)
        {
          attrs[k] |= A_UNDERLINE;
        }

        i = j + len;
        continue;
      }
    }
    else if ((c == L'*') || (c == L'_') || (c == L'~'))
    {
      const int len = ((i + 1) < n) && (p_Line[i + 1] == c) && (c != L'~') ? 2 : 1;
      if (isBoundary(i - 1) && ((i + len) < n) && !iswspace(p_Line[i + len]))
      {
        for (int j = i + len + 1; (j + len) <= n; ++j)
        {
          if ((p_Line.compare(j, len, p_Line, i, len) == 0) && !iswspace(p_Line[j - 1]) && isBoundary(j + len))
          {
            const attr_t style = (c == L'*') ? A_BOLD : ((c == L'_') ? A_ITALIC : A_DIM);
            for (int k = 0; k < len; ++k)
            {
              hidden[i + k] = true;
              hidden[j + k] = true;
            }

            for (int k = i + len; k < j; ++k)
            {
              attrs[k] |= style;
            }

            break;
          }
        }

        i += len;
        continue;
      }
    }

    ++i;
  }

  wmove(p_Win, p_Y, 0);
  std::wstring shown;
  for (int i = 0; i < n; ++i)
  {
    if (hidden[i]) continue;

    wattrset(p_Win, p_Attr | attrs[i]);
    waddnwstr(p_Win, &p_Line[i], 1);
    shown += p_Line[i];
  }

  wattrset(p_Win, p_Attr);
  const int pad = p_Width - StrUtil::WStringWidth(shown);
  if (pad > 0)
  {
    const std::wstring spaces(pad, L' ');
    waddnwstr(p_Win, spaces.c_str(), spaces.size());
  }
}

UiHistoryView::UiHistoryView(const UiViewParams& p_Params)
  : UiViewBase(p_Params)
{
  if (m_Enabled)
  {
    int hpad = (m_X == 0) ? 0 : 1;
    int vpad = 1;
    int paddedY = m_Y + vpad;
    int paddedX = m_X + hpad;
    m_PaddedH = m_H - (vpad * 2);
    m_PaddedW = m_W - (hpad * 2);
    m_PaddedWin = newwin(m_PaddedH, m_PaddedW, paddedY, paddedX);

    static int attributeTextNormal = UiColorConfig::GetAttribute("history_text_attr");
    static int colorPairTextRecv = UiColorConfig::GetColorPair("history_text_recv_color");
    werase(m_Win);
    wbkgd(m_Win, attributeTextNormal | colorPairTextRecv | ' ');
    wrefresh(m_Win);
  }
}

UiHistoryView::~UiHistoryView()
{
  if (m_PaddedWin != nullptr)
  {
    delwin(m_PaddedWin);
    m_PaddedWin = nullptr;
  }
}

void UiHistoryView::Draw()
{
  if (!m_Enabled || !m_Dirty) return;
  m_Dirty = false;

  curs_set(0);

  static int colorPairTextSent = UiColorConfig::GetColorPair("history_text_sent_color");
  static int colorPairTextRecv = UiColorConfig::GetColorPair("history_text_recv_color");
  static int colorPairTextQuoted = UiColorConfig::GetColorPair("history_text_quoted_color");
  static int colorPairTextReaction = UiColorConfig::GetColorPair("history_text_reaction_color");
  static int colorPairTextAttachment = UiColorConfig::GetColorPair("history_text_attachment_color");
  static int colorPairTextAttachmentLinked = UiColorConfig::GetColorPair("history_text_attachment_linked_color");
  static int attributeTextNormal = UiColorConfig::GetAttribute("history_text_attr");
  static int attributeTextSelected = UiColorConfig::GetAttribute("history_text_attr_selected");

  static int colorPairNameSent = UiColorConfig::GetColorPair("history_name_sent_color");
  static int colorPairNameRecv = UiColorConfig::GetColorPair("history_name_recv_color");
  static int attributeNameNormal = UiColorConfig::GetAttribute("history_name_attr");
  static int attributeNameSelected = UiColorConfig::GetAttribute("history_name_attr_selected");

  static std::wstring attachmentIndicator =
    StrUtil::ToWString(UiConfig::GetStr("attachment_indicator") + " ");
  static std::wstring quoteIndicator = L"> ";

  std::pair<std::string, std::string>& currentChat = m_Model->GetCurrentChatLocked();
  const bool emojiEnabled = m_Model->GetEmojiEnabledLocked();
  static const bool developerMode = AppUtil::GetDeveloperMode();

  std::vector<std::string>& messageVec =
    m_Model->GetMessageVecLocked(currentChat.first, currentChat.second);
  std::unordered_map<std::string, ChatMessage>& messages =
    m_Model->GetMessagesLocked(currentChat.first, currentChat.second);
  const int selectedOffset = std::max(m_Model->GetMessageOffsetLocked(currentChat.first, currentChat.second), 0);
  int messageOffset = m_Model->GetHistoryViewStartLocked(currentChat.first, currentChat.second);

  werase(m_PaddedWin);
  wbkgd(m_PaddedWin, attributeTextNormal | colorPairTextRecv | ' ');

  m_HistoryShowCount = 0;
  m_RowHits.assign(std::max(m_PaddedH, 0), std::make_pair(-1, false));
  m_ThumbRows.assign(std::max(m_PaddedH, 0), false);
  m_LinkRows.clear();
  int drawMessageOffset = messageOffset;

  bool firstMessage = true;
  int y = m_PaddedH - 1;
  for (auto it = std::next(messageVec.begin(), messageOffset); it != messageVec.end(); ++it)
  {
    bool isSelectedMessage = (drawMessageOffset == selectedOffset) && m_Model->GetSelectMessageActiveLocked();

    auto msgIt = messages.find(*it);
    if (msgIt == messages.end())
    {
      LOG_WARNING("message %s missing", it->c_str());
      ++drawMessageOffset;
      continue;
    }

    ChatMessage& msg = msgIt->second;

    int attributeText = isSelectedMessage ? attributeTextSelected : attributeTextNormal;
    int colorPairText = [&]()
    {
      if (msg.isOutgoing) return colorPairTextSent;

      if (msg.senderId == currentChat.second) return colorPairTextRecv;

      static bool isUserColor = UiColorConfig::IsUserColor("history_text_recv_group_color");
      if (!isUserColor)
      {
        static int colorPairGroup = UiColorConfig::GetColorPair("history_text_recv_group_color");
        return colorPairGroup;
      }

      int colorPairGroup = UiColorConfig::GetUserColorPair("history_text_recv_group_color",
                                                           msg.senderId);
      return colorPairGroup;
    }();

    std::vector<std::wstring> wlines;
    if (!msg.text.empty())
    {
      std::string text = msg.text;
      StrUtil::SanitizeMessageStr(text);
      if (!emojiEnabled)
      {
        text = StrUtil::Textize(text);
      }

      static const bool messageFormatting = UiConfig::GetBool("message_formatting");
      wlines = StrUtil::WordWrap(StrUtil::ToWString(text), m_PaddedW - (messageFormatting ? 2 : 0), false, false,
                                 false, 2);

      // fenced code blocks: drop the ``` lines, mark the lines between them
      // (marker U+0002, drawn as a gutter without inline formatting)
      if (messageFormatting)
      {
        // a fence may share its line with code: "```code" opens a block
        // (unless the rest is only a language name like "bash") and
        // "code```" closes it
        bool inBlock = false;
        std::vector<std::wstring> blockLines;
        auto isLanguageTag = [](const std::wstring& p_Str)
        {
          if (p_Str.empty()) return true;

          for (wchar_t ch : p_Str)
          {
            if (!iswalnum(ch) && (ch != L'_') && (ch != L'-') && (ch != L'+')) return false;
          }

          return true;
        };

        for (const std::wstring& wline : wlines)
        {
          if (!inBlock)
          {
            const size_t first = wline.find_first_not_of(L' ');
            if ((first != std::wstring::npos) && (wline.compare(first, 3, L"```") == 0))
            {
              std::wstring rest = wline.substr(first + 3);
              if (rest.find(L"```") != std::wstring::npos)
              {
                blockLines.push_back(wline); // inline ```code```
                continue;
              }

              inBlock = true;
              const size_t restEnd = rest.find_last_not_of(L' ');
              rest = (restEnd == std::wstring::npos) ? std::wstring() : rest.substr(0, restEnd + 1);
              if (!isLanguageTag(rest))
              {
                blockLines.push_back(L"\u0002" + rest);
              }

              continue;
            }

            blockLines.push_back(wline);
          }
          else
          {
            const size_t last = wline.find_last_not_of(L' ');
            if ((last != std::wstring::npos) && (last >= 2) && (wline.compare(last - 2, 3, L"```") == 0))
            {
              const std::wstring code = wline.substr(0, last - 2);
              if (code.find_first_not_of(L' ') != std::wstring::npos)
              {
                blockLines.push_back(L"\u0002" + code);
              }

              inBlock = false;
              continue;
            }

            blockLines.push_back(L"\u0002" + wline);
          }
        }

        wlines.swap(blockLines);
      }
    }

    // Quoted message
    if (!msg.quotedId.empty())
    {
      std::string quotedText;
      auto quotedIt = messages.find(msg.quotedId);
      if (quotedIt != messages.end())
      {
        if (!quotedIt->second.text.empty())
        {
          quotedText = StrUtil::Split(quotedIt->second.text, '\n').at(0);
          if (!emojiEnabled)
          {
            quotedText = StrUtil::Textize(quotedText);
          }
        }
        else if (!quotedIt->second.fileInfo.empty())
        {
          FileInfo fileInfo = ProtocolUtil::FileInfoFromHex(quotedIt->second.fileInfo);
          quotedText = FileUtil::BaseName(fileInfo.filePath);
        }
      }
      else
      {
        m_Model->FetchCachedMessageLocked(currentChat.first, currentChat.second, msg.quotedId);
        quotedText = "";
      }

      int maxQuoteLen = m_PaddedW - 3;
      std::wstring quote = quoteIndicator + StrUtil::ToWString(quotedText);
      if (StrUtil::WStringWidth(quote) > maxQuoteLen)
      {
        quote = StrUtil::TrimPadWString(quote, maxQuoteLen) + L"...";
      }

      wlines.insert(wlines.begin(), quote);
    }

    // File attachment
    std::string attachmentLink;
    if (!msg.fileInfo.empty())
    {
      FileInfo fileInfo = ProtocolUtil::FileInfoFromHex(msg.fileInfo);

      // special case handling selection-triggered download, and handling cache's old setting
      static const bool isAttachmentPrefetchAll =
        (AppConfig::GetNum("attachment_prefetch") == AttachmentPrefetchAll);
      static const bool isAttachmentPrefetchSelected =
        (AppConfig::GetNum("attachment_prefetch") == AttachmentPrefetchSelected);
      if (isAttachmentPrefetchAll || (isSelectedMessage && isAttachmentPrefetchSelected))
      {
        if (!UiModel::IsAttachmentDownloaded(fileInfo) && UiModel::IsAttachmentDownloadable(fileInfo))
        {
          m_Model->DownloadAttachmentLocked(currentChat.first, currentChat.second, *it,
                                            fileInfo.fileId, DownloadFileActionNone);
          fileInfo = ProtocolUtil::FileInfoFromHex(msg.fileInfo);
        }
      }

      std::string fileName = FileUtil::BaseName(fileInfo.filePath);
      std::string fileStatus;
      if (fileInfo.fileStatus == FileStatusNone)
      {
        // should not happen
        static const std::string statusNone = " -";
        fileStatus = statusNone;
      }
      else if (fileInfo.fileStatus == FileStatusNotDownloaded)
      {
        static const std::string statusNotDownloaded = " " + UiConfig::GetStr("downloadable_indicator");
        fileStatus = statusNotDownloaded;
      }
      else if ((fileInfo.fileStatus == FileStatusDownloaded) && !FileUtil::Exists(fileInfo.filePath))
      {
        // deleted (cleaned) after download: downloadable again
        static const std::string statusCleaned = " " + UiConfig::GetStr("downloadable_indicator");
        fileStatus = statusCleaned;
      }
      else if (fileInfo.fileStatus == FileStatusDownloaded)
      {
        static const std::string statusDownloaded = "";
        fileStatus = statusDownloaded;
      }
      else if (fileInfo.fileStatus == FileStatusDownloading)
      {
        static const std::string statusDownloading = " " + UiConfig::GetStr("syncing_indicator");
        fileStatus = statusDownloading;
      }
      else if (fileInfo.fileStatus == FileStatusDownloadFailed)
      {
        static const std::string statusDownloadFailed = " " + UiConfig::GetStr("failed_indicator");
        fileStatus = statusDownloadFailed;
      }

      const bool isFilePresent = (fileInfo.fileStatus == FileStatusDownloaded) && FileUtil::Exists(fileInfo.filePath);
      if (isFilePresent)
      {
        // attachmentLink is set below; the mark depends only on links being on
        static const std::string linkedIndicator = UiConfig::GetStr("linked_indicator");
        static const bool hasLinks = !UiConfig::GetStr("attachment_link_base").empty();
        if (hasLinks && !linkedIndicator.empty())
        {
          fileStatus = " " + linkedIndicator;
        }
      }

      std::wstring fileStr = attachmentIndicator + StrUtil::ToWString(fileName + fileStatus);
      if (isFilePresent)
      {
        int thumbHandle = 0;
        int thumbCols = 0;
        int thumbRows = 0;
        if (UiImage::GetThumbnail(fileInfo.filePath, m_PaddedW - 2, thumbHandle, thumbCols, thumbRows))
        {
          // marker U+0001, thumbnail handle, row and width; drawn by UiImage
          for (int row = thumbRows - 1; row >= 0; --row)
          {
            std::wstring line = L"\u0001";
            line += (wchar_t)(thumbHandle + 1);
            line += (wchar_t)(row + 1);
            line += (wchar_t)thumbCols;
            wlines.insert(wlines.begin(), line);
          }
        }

        // the file name line becomes an OSC 8 hyperlink (see EmitLinks)
        attachmentLink = GetAttachmentLink(fileInfo.filePath);
      }

      wlines.insert(wlines.begin(), fileStr);
    }

    // Reactions
    int reactionLines = 0;
    static bool reactionsEnabled = UiConfig::GetBool("reactions_enabled");
    if (reactionsEnabled)
    {
      std::string selfEmoji;
      auto sit = msg.reactions.senderEmojis.find(s_ReactionsSelfId);
      if (sit != msg.reactions.senderEmojis.end())
      {
        selfEmoji = sit->second;
      }

      // Allow also if we have self emoji, even if not yet consolidated into count
      if (!msg.reactions.emojiCounts.empty() || !selfEmoji.empty())
      {
        bool foundSelf = false;
        std::string reactionsText;
        std::multimap<float, std::string> emojiCountsSorted;
        for (const auto& emojiCount : msg.reactions.emojiCounts)
        {
          float count = emojiCount.second;
          if (emojiCount.first == selfEmoji)
          {
            count += 0.1; // for equal count, prioritize own selected reaction
            foundSelf = true;
          }

          emojiCountsSorted.insert(std::make_pair(count, emojiCount.first));
        }

        if (!foundSelf && !selfEmoji.empty())
        {
          LOG_DEBUG("insert missing reaction for self");
          emojiCountsSorted.insert(std::make_pair(1.1, selfEmoji));
        }

        bool firstReaction = true;
        for (auto emojiCount = emojiCountsSorted.rbegin(); emojiCount != emojiCountsSorted.rend(); ++emojiCount)
        {
          reactionsText += (firstReaction ? " " : "  ");
          if (emojiCount->second == selfEmoji)
          {
            // Highlight own reaction emoji
            reactionsText += "" + emojiCount->second + "*";
          }
          else
          {
            reactionsText += emojiCount->second;
          }

          if (emojiCount->first > 1.5)
          {
            reactionsText += " " + FileUtil::GetSuffixedCount(static_cast<ssize_t>(emojiCount->first));
          }

          firstReaction = false;
        }

        if (!reactionsText.empty())
        {
          if (!emojiEnabled)
          {
            reactionsText = StrUtil::Textize(reactionsText);
          }

          const int maxReactionsLen = m_PaddedW - 4;
          std::wstring reactions = StrUtil::ToWString(reactionsText);
          if (StrUtil::WStringWidth(reactions) > maxReactionsLen)
          {
            reactions = StrUtil::TrimPadWString(reactions, maxReactionsLen) + L"... ";
          }
          else
          {
            reactions += L" ";
          }

          wlines.insert(wlines.end(), reactions);
          reactionLines = 1;
        }
      }
    }

    const int maxMessageLines = (m_PaddedH - 1);
    if (firstMessage && ((int)wlines.size() > maxMessageLines))
    {
      wlines.resize(maxMessageLines - 1);
      wlines.push_back(L"[...]");
      reactionLines = 0;
    }

    for (auto wline = wlines.rbegin(); wline != wlines.rend(); ++wline)
    {
      bool isThumbnail = (wline->size() == 4) && (wline->at(0) == L'\u0001');
      bool isCodeBlock = !wline->empty() && (wline->at(0) == L'\u0002');
      bool isAttachment = (wline->rfind(attachmentIndicator, 0) == 0);
      bool isAttachmentLink = (wline->rfind(L"  https://", 0) == 0) || (wline->rfind(L"  http://", 0) == 0);
      bool isQuote = (wline->rfind(quoteIndicator, 0) == 0);
      bool isReaction = (reactionLines == 1) && (std::distance(wline, wlines.rbegin()) == 0);

      if (isThumbnail)
      {
        UiImage::DrawRow(m_PaddedWin, y, 2, (int)wline->at(1) - 1, (int)wline->at(2) - 1, (int)wline->at(3));
        m_ThumbRows[y] = true;
        m_RowHits[y] = std::make_pair(drawMessageOffset, true);
        if (--y < 0) break;
        continue;
      }

      const int colorPairAttachment = attachmentLink.empty() ? colorPairTextAttachment : colorPairTextAttachmentLinked;
      if (isAttachment)
      {
        wattron(m_PaddedWin, attributeText | colorPairAttachment);
      }
      else if (isQuote)
      {
        wattron(m_PaddedWin, attributeText | colorPairTextQuoted);
      }
      else if (isReaction)
      {
        wattron(m_PaddedWin, attributeTextNormal | colorPairTextReaction);
      }
      else
      {
        wattron(m_PaddedWin, attributeText | colorPairText);
      }

      const std::wstring wdisp = isReaction ? *wline : StrUtil::TrimPadWString(*wline, m_PaddedW);
      static const bool messageFormatting = UiConfig::GetBool("message_formatting");
      if (isCodeBlock)
      {
        const std::wstring gutter = L"\u2502 ";
        wattrset(m_PaddedWin, attributeText | colorPairText);
        mvwaddnwstr(m_PaddedWin, y, 0, gutter.c_str(), gutter.size());
        wattrset(m_PaddedWin, attributeText | colorPairText);
        const std::wstring code = StrUtil::TrimPadWString(wline->substr(1), std::max(m_PaddedW - 2, 0));
        waddnwstr(m_PaddedWin, code.c_str(), code.size());
      }
      else if (messageFormatting && !isAttachment && !isAttachmentLink && !isQuote && !isReaction)
      {
        DrawFormattedLine(m_PaddedWin, y, m_PaddedW, wdisp, attributeText | colorPairText);
      }
      else
      {
        mvwaddnwstr(m_PaddedWin, y, 0, wdisp.c_str(), std::min((int)wdisp.size(), m_PaddedW));
      }
      m_RowHits[y] = std::make_pair(drawMessageOffset, isAttachment || isAttachmentLink);
      if (isAttachment && !attachmentLink.empty())
      {
        LinkRow linkRow;
        linkRow.y = y;
        linkRow.url = attachmentLink;
        linkRow.text = StrUtil::TrimPadWString(*wline, std::min(StrUtil::WStringWidth(*wline), m_PaddedW));
        linkRow.selected = isSelectedMessage;
        m_LinkRows.push_back(linkRow);
      }

      if (isAttachment)
      {
        wattroff(m_PaddedWin, attributeText | colorPairAttachment);
      }
      else if (isQuote)
      {
        wattroff(m_PaddedWin, attributeText | colorPairTextQuoted);
      }
      else if (isReaction)
      {
        wattroff(m_PaddedWin, attributeTextNormal | colorPairTextReaction);
      }
      else
      {
        wattroff(m_PaddedWin, attributeText | colorPairText);
      }

      if (--y < 0) break;
    }

    if (y < 0) break;

    int attributeName = isSelectedMessage ? attributeNameSelected : attributeNameNormal;
    int colorPairName = [&]()
    {
      if (msg.isOutgoing) return colorPairNameSent;

      if (msg.senderId == currentChat.second) return colorPairNameRecv;

      static bool isUserColor = UiColorConfig::IsUserColor("history_name_recv_group_color");
      if (!isUserColor)
      {
        static int colorPairGroup = UiColorConfig::GetColorPair("history_name_recv_group_color");
        return colorPairGroup;
      }

      int colorPairGroup = UiColorConfig::GetUserColorPair("history_name_recv_group_color",
                                                           msg.senderId);
      return colorPairGroup;
    }();

    wattron(m_PaddedWin, attributeName | colorPairName);
    std::string name = m_Model->GetContactNameLocked(currentChat.first, msg.senderId);
    if (!emojiEnabled)
    {
      name = StrUtil::Textize(name);
    }

    std::wstring wsender = StrUtil::ToWString(name);
    std::wstring wtime;
    if (developerMode)
    {
      wtime = L" (" + StrUtil::ToWString(std::to_string(msg.timeSent)) + L")";
    }
    else
    {
      if (msg.timeSent != std::numeric_limits<int64_t>::max())
      {
        wtime = L" (" + StrUtil::ToWString(TimeUtil::GetTimeString(msg.timeSent, false /* p_IsExport */)) + L")";
      }
    }

    m_Model->MarkReadLocked(currentChat.first, currentChat.second, *it, (!msg.isOutgoing && !msg.isRead));

    static const std::string readIndicator = " " + UiConfig::GetStr("read_indicator");
    static const std::string editedIndicator = " " + UiConfig::GetStr("edited_indicator");
    static const std::string pinnedIndicator = " " + UiConfig::GetStr("pinned_indicator");
    std::wstring wreceipt = StrUtil::ToWString(msg.isRead ? readIndicator : "");
    std::wstring wedited = StrUtil::ToWString(msg.isEdited ? editedIndicator : "");
    std::wstring wpinned = StrUtil::ToWString(msg.isPinned ? pinnedIndicator : "");
    std::wstring wheader = wsender + wtime + wreceipt + wedited + wpinned;

    if (developerMode)
    {
      wheader = wheader +
        L" msg " + StrUtil::ToWString(msg.id) +
        L" user " + StrUtil::ToWString(msg.senderId);
    }

    std::wstring wdisp = StrUtil::TrimPadWString(wheader, m_PaddedW);
    mvwaddnwstr(m_PaddedWin, y, 0, wdisp.c_str(), std::min((int)wdisp.size(), m_PaddedW));
    m_RowHits[y] = std::make_pair(drawMessageOffset, false);

    wattroff(m_PaddedWin, attributeName | colorPairName);

    ++m_HistoryShowCount;

    if (--y < 0) break;

    if (--y < 0) break;

    firstMessage = false;
    ++drawMessageOffset;
  }

  DrawSelection();
  wrefresh(m_PaddedWin);
  EmitLinks();
}

bool UiHistoryView::GetSelectionRange(int& p_Row1, int& p_Col1, int& p_Row2, int& p_Col2)
{
  if (!m_SelectionActive) return false;

  const int hpad = (m_X == 0) ? 0 : 1;
  auto toRow = [&](int y) { return std::min(std::max(y - (m_Y + 1), 0), m_PaddedH - 1); };
  auto toCol = [&](int x) { return std::min(std::max(x - (m_X + hpad), 0), m_PaddedW - 1); };
  int r1 = toRow(m_SelY1);
  int c1 = toCol(m_SelX1);
  int r2 = toRow(m_SelY2);
  int c2 = toCol(m_SelX2);
  if ((r2 < r1) || ((r2 == r1) && (c2 < c1)))
  {
    std::swap(r1, r2);
    std::swap(c1, c2);
  }

  p_Row1 = r1;
  p_Col1 = c1;
  p_Row2 = r2;
  p_Col2 = c2;
  return true;
}

void UiHistoryView::DrawSelection()
{
  int r1 = 0;
  int c1 = 0;
  int r2 = 0;
  int c2 = 0;
  if (!GetSelectionRange(r1, c1, r2, c2)) return;

  for (int row = r1; row <= r2; ++row)
  {
    if ((row < (int)m_ThumbRows.size()) && m_ThumbRows[row]) continue;

    const int from = (row == r1) ? c1 : 0;
    const int to = (row == r2) ? c2 : (m_PaddedW - 1);
    mvwchgat(m_PaddedWin, row, from, to - from + 1, A_REVERSE, 0, nullptr);
  }
}

void UiHistoryView::SetSelection(int p_Y1, int p_X1, int p_Y2, int p_X2)
{
  m_SelectionActive = true;
  m_SelY1 = p_Y1;
  m_SelX1 = p_X1;
  m_SelY2 = p_Y2;
  m_SelX2 = p_X2;
  SetDirty(true);
}

void UiHistoryView::ClearSelection()
{
  if (!m_SelectionActive) return;

  m_SelectionActive = false;
  SetDirty(true);
}

std::string UiHistoryView::GetSelectionText()
{
  int r1 = 0;
  int c1 = 0;
  int r2 = 0;
  int c2 = 0;
  if (!GetSelectionRange(r1, c1, r2, c2)) return "";

  std::wstring text;
  for (int row = r1; row <= r2; ++row)
  {
    if ((row < (int)m_ThumbRows.size()) && m_ThumbRows[row]) continue;

    // read the drawn cells back; wide characters take two columns
    std::wstring line;
    for (int col = 0; col < m_PaddedW; ++col)
    {
      cchar_t cell;
      if (mvwin_wch(m_PaddedWin, row, col, &cell) != OK) break;

      wchar_t wch[CCHARW_MAX + 1] = { 0 };
      attr_t attrs = 0;
      short pair = 0;
      getcchar(&cell, wch, &attrs, &pair, nullptr);
      const int from = (row == r1) ? c1 : 0;
      const int to = (row == r2) ? c2 : (m_PaddedW - 1);
      if ((col >= from) && (col <= to) && (wch[0] != 0))
      {
        line += wch;
      }

      const int w = wcwidth(wch[0]);
      if (w > 1) col += w - 1;
    }

    const size_t end = line.find_last_not_of(L' ');
    line = (end == std::wstring::npos) ? std::wstring() : line.substr(0, end + 1);
    if (!text.empty() || (row > r1)) text += L"\n";
    text += line;
  }

  // drop the leading newline added for an empty first line
  while (!text.empty() && (text.at(0) == L'\n'))
  {
    text.erase(0, 1);
  }

  return StrUtil::ToString(text);
}

void UiHistoryView::EmitLinks()
{
  // curses has no hyperlink support: after the refresh, rewrite each linked
  // attachment line with the same text inside an OSC 8 hyperlink, saving and
  // restoring cursor and attributes (DECSC/DECRC) around it. Curses never
  // learns about it; when it later redraws those cells the link goes away,
  // and the next history draw adds it again.
  if (m_LinkRows.empty()) return;

  static int colorPairTextAttachmentLinked = UiColorConfig::GetColorPair("history_text_attachment_linked_color");
  int fg = -1;
  int bg = -1;
  // GetColorPair returns a COLOR_PAIR() attribute; the pair number is in it
  extended_pair_content(PAIR_NUMBER(colorPairTextAttachmentLinked), &fg, &bg);

  const int hpad = (m_X == 0) ? 0 : 1;
  std::string out;
  for (const LinkRow& linkRow : m_LinkRows)
  {
    std::string sgr = "\033[0m";
    if (linkRow.selected) sgr += "\033[7m";
    if (fg >= 0) sgr += "\033[38;5;" + std::to_string(fg) + "m";
    if (bg >= 0) sgr += "\033[48;5;" + std::to_string(bg) + "m";

    out += "\0337";
    out += "\033[" + std::to_string(m_Y + 1 + linkRow.y + 1) + ";" + std::to_string(m_X + hpad + 1) + "H";
    out += sgr;
    out += "\033]8;;" + linkRow.url + "\033\\";
    out += StrUtil::ToString(linkRow.text);
    out += "\033]8;;\033\\";
    out += "\0338";
  }

  fwrite(out.data(), 1, out.size(), stdout);
  fflush(stdout);
}

int UiHistoryView::GetHistoryShowCount()
{
  return m_HistoryShowCount;
}

bool UiHistoryView::Contains(int p_Y, int p_X)
{
  return m_Enabled && (p_Y >= m_Y) && (p_Y < (m_Y + m_H)) && (p_X >= m_X) && (p_X < (m_X + m_W));
}

int UiHistoryView::GetMessageOffsetAt(int p_Y, int p_X, bool* p_IsAttachment /*= nullptr*/)
{
  if (!m_Enabled) return -1;

  const int hpad = (m_X == 0) ? 0 : 1;
  const int row = p_Y - (m_Y + 1);
  const int col = p_X - (m_X + hpad);
  if ((row < 0) || (row >= (int)m_RowHits.size()) || (col < 0) || (col >= m_PaddedW)) return -1;

  if (p_IsAttachment != nullptr)
  {
    *p_IsAttachment = m_RowHits[row].second;
  }

  return m_RowHits[row].first;
}
