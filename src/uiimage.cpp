// uiimage.cpp
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "uiimage.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <fstream>
#include <functional>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include <ncurses.h>

#include "fileutil.h"
#include "log.h"
#include "strutil.h"
#include "sysutil.h"
#include "uiconfig.h"

namespace
{
  // Kitty rowcolumn-diacritics.txt, first 128 entries
  const wchar_t s_Diacritics[] =
  {
  0x0305, 0x030D, 0x030E, 0x0310, 0x0312, 0x033D, 0x033E, 0x033F, 0x0346, 0x034A,
  0x034B, 0x034C, 0x0350, 0x0351, 0x0352, 0x0357, 0x035B, 0x0363, 0x0364, 0x0365,
  0x0366, 0x0367, 0x0368, 0x0369, 0x036A, 0x036B, 0x036C, 0x036D, 0x036E, 0x036F,
  0x0483, 0x0484, 0x0485, 0x0486, 0x0487, 0x0592, 0x0593, 0x0594, 0x0595, 0x0597,
  0x0598, 0x0599, 0x059C, 0x059D, 0x059E, 0x059F, 0x05A0, 0x05A1, 0x05A8, 0x05A9,
  0x05AB, 0x05AC, 0x05AF, 0x05C4, 0x0610, 0x0611, 0x0612, 0x0613, 0x0614, 0x0615,
  0x0616, 0x0617, 0x0657, 0x0658, 0x0659, 0x065A, 0x065B, 0x065D, 0x065E, 0x06D6,
  0x06D7, 0x06D8, 0x06D9, 0x06DA, 0x06DB, 0x06DC, 0x06DF, 0x06E0, 0x06E1, 0x06E2,
  0x06E4, 0x06E7, 0x06E8, 0x06EB, 0x06EC, 0x0730, 0x0732, 0x0733, 0x0735, 0x0736,
  0x073A, 0x073D, 0x073F, 0x0740, 0x0741, 0x0743, 0x0745, 0x0747, 0x0749, 0x074A,
  0x07EB, 0x07EC, 0x07ED, 0x07EE, 0x07EF, 0x07F0, 0x07F1, 0x07F3, 0x0816, 0x0817,
  0x0818, 0x0819, 0x081B, 0x081C, 0x081D, 0x081E, 0x081F, 0x0820, 0x0821, 0x0822,
  0x0823, 0x0825, 0x0826, 0x0827, 0x0829, 0x082A, 0x082B, 0x082C
  };
  const int s_DiacriticCount = sizeof(s_Diacritics) / sizeof(s_Diacritics[0]);
  const wchar_t s_Placeholder = 0x10EEEE;
  // ids are carried in the 256-color foreground; curses writes colors below
  // 16 as basic SGR codes (30-37, 90-97), which do not select an image
  const int s_MinImageId = 16;
  const int s_MaxImageId = 255;

  enum State { Pending, Ready, Failed };

  struct Thumb
  {
    State state = Pending;
    std::string thumbPath;
    int pxW = 0;
    int pxH = 0;
    int imageId = 0;
    bool transmitted = false;
    int cols = 0;
  };

  std::mutex s_Mutex;
  std::condition_variable s_Cond;
  std::unordered_map<std::string, Thumb> s_Thumbs;
  std::deque<std::string> s_Queue;
  std::atomic<bool> s_Updated(false);
  bool s_WorkerStarted = false;
  std::vector<std::string> s_IdOwner(s_MaxImageId + 1); // image id -> source path
  int s_NextId = s_MinImageId;

  std::string Base64Encode(const std::string& p_Data)
  {
    static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((p_Data.size() + 2) / 3) * 4);
    size_t i = 0;
    for (; (i + 2) < p_Data.size(); i += 3)
    {
      const unsigned v = ((unsigned char)p_Data[i] << 16) | ((unsigned char)p_Data[i + 1] << 8) |
        (unsigned char)p_Data[i + 2];
      out += tbl[(v >> 18) & 63];
      out += tbl[(v >> 12) & 63];
      out += tbl[(v >> 6) & 63];
      out += tbl[v & 63];
    }

    if (i < p_Data.size())
    {
      unsigned v = (unsigned char)p_Data[i] << 16;
      if ((i + 1) < p_Data.size()) v |= (unsigned char)p_Data[i + 1] << 8;
      out += tbl[(v >> 18) & 63];
      out += tbl[(v >> 12) & 63];
      out += ((i + 1) < p_Data.size()) ? tbl[(v >> 6) & 63] : '=';
      out += '=';
    }

    return out;
  }

  bool ReadPngSize(const std::string& p_Path, int& p_W, int& p_H)
  {
    std::ifstream file(p_Path, std::ios::binary);
    unsigned char hdr[24];
    if (!file.read(reinterpret_cast<char*>(hdr), sizeof(hdr))) return false;
    if ((hdr[1] != 'P') || (hdr[2] != 'N') || (hdr[3] != 'G')) return false;
    p_W = (hdr[16] << 24) | (hdr[17] << 16) | (hdr[18] << 8) | hdr[19];
    p_H = (hdr[20] << 24) | (hdr[21] << 16) | (hdr[22] << 8) | hdr[23];
    return (p_W > 0) && (p_H > 0);
  }

  void Worker()
  {
    static const std::string cmdTemplate = UiConfig::GetStr("thumbnail_command");
    static const std::string cacheDir = FileUtil::ExpandPath("~/.cache/mchat/thumbs");
    FileUtil::MkDir(cacheDir);

    while (true)
    {
      std::string path;
      {
        std::unique_lock<std::mutex> lock(s_Mutex);
        s_Cond.wait(lock, [] { return !s_Queue.empty(); });
        path = s_Queue.front();
        s_Queue.pop_front();
      }

      char name[32];
      snprintf(name, sizeof(name), "%016zx.png", std::hash<std::string>{}(path));
      const std::string thumbPath = cacheDir + "/" + name;

      int w = 0;
      int h = 0;
      if (!ReadPngSize(thumbPath, w, h))
      {
        std::string cmd = cmdTemplate;
        StrUtil::ReplaceString(cmd, "%1", StrUtil::EscapeSingleQuote(path));
        StrUtil::ReplaceString(cmd, "%2", StrUtil::EscapeSingleQuote(thumbPath));
        SysUtil::System(cmd + " >/dev/null 2>&1");
        ReadPngSize(thumbPath, w, h);
      }

      {
        std::unique_lock<std::mutex> lock(s_Mutex);
        Thumb& thumb = s_Thumbs[path];
        if ((w > 0) && (h > 0))
        {
          thumb.state = Ready;
          thumb.thumbPath = thumbPath;
          thumb.pxW = w;
          thumb.pxH = h;
        }
        else
        {
          thumb.state = Failed;
        }
      }

      s_Updated = true;
    }
  }

  int ColorPairForId(int p_Id)
  {
    // pairs at the top of the range, away from nchat's configured pairs
    const int pair = COLOR_PAIRS - 1 - p_Id;
    static std::vector<bool> s_Initialized(s_MaxImageId + 1, false);
    if (!s_Initialized[p_Id])
    {
      // pair numbers this high need the extended color API (COLOR_PAIR()
      // in attributes only holds 8 bits)
      int fg = 0;
      int bg = 0;
      extended_pair_content(0, &fg, &bg);
      init_extended_pair(pair, p_Id, bg);
      s_Initialized[p_Id] = true;
    }

    return pair;
  }

  void Transmit(Thumb& p_Thumb, int p_Cols, int p_Rows)
  {
    const std::string data = FileUtil::ReadFile(p_Thumb.thumbPath);
    const std::string b64 = Base64Encode(data);
    std::string out;
    const size_t chunk = 4096;
    for (size_t pos = 0; pos < b64.size(); pos += chunk)
    {
      const bool more = (pos + chunk) < b64.size();
      if (pos == 0)
      {
        out += "\033_Ga=T,U=1,q=2,f=100,i=" + std::to_string(p_Thumb.imageId) + ",c=" + std::to_string(p_Cols) +
          ",r=" + std::to_string(p_Rows) + ",m=" + (more ? "1" : "0") + ";";
      }
      else
      {
        out += std::string("\033_Gm=") + (more ? "1" : "0") + ";";
      }

      out += b64.substr(pos, chunk) + "\033\\";
    }

    fwrite(out.data(), 1, out.size(), stdout);
    fflush(stdout);
    p_Thumb.transmitted = true;
    p_Thumb.cols = p_Cols;
  }
}

bool UiImage::Enabled()
{
  static const bool enabled = !UiConfig::GetStr("thumbnail_command").empty() && (Rows() > 0) && (COLORS >= 256);
  return enabled;
}

int UiImage::Rows()
{
  static const int rows = std::min(std::max((int)UiConfig::GetNum("thumbnail_rows"), 0), s_DiacriticCount);
  return rows;
}

bool UiImage::GetThumbnail(const std::string& p_Path, int p_MaxCols, int& p_ColorPair, int& p_Cols, int& p_Rows)
{
  if (!Enabled() || p_Path.empty() || (p_MaxCols <= 0)) return false;

  std::unique_lock<std::mutex> lock(s_Mutex);
  auto it = s_Thumbs.find(p_Path);
  if (it == s_Thumbs.end())
  {
    s_Thumbs[p_Path] = Thumb();
    s_Queue.push_back(p_Path);
    if (!s_WorkerStarted)
    {
      std::thread(Worker).detach();
      s_WorkerStarted = true;
    }

    s_Cond.notify_one();
    return false;
  }

  Thumb& thumb = it->second;
  if (thumb.state != Ready) return false;

  // terminal cells are about twice as tall as wide
  const int rows = Rows();
  int cols = (int)(((double)rows * 2.0 * thumb.pxW) / thumb.pxH + 0.5);
  cols = std::min(std::max(cols, 1), std::min(p_MaxCols, s_DiacriticCount));

  if (thumb.imageId == 0)
  {
    // reuse ids round-robin; a reused id is retransmitted by its new owner
    const int id = s_NextId;
    s_NextId = (s_NextId >= s_MaxImageId) ? s_MinImageId : (s_NextId + 1);
    const std::string& prevOwner = s_IdOwner[id];
    if (!prevOwner.empty())
    {
      auto pit = s_Thumbs.find(prevOwner);
      if (pit != s_Thumbs.end())
      {
        pit->second.imageId = 0;
        pit->second.transmitted = false;
      }
    }

    s_IdOwner[id] = p_Path;
    thumb.imageId = id;
    thumb.transmitted = false;
  }

  if (!thumb.transmitted || (thumb.cols != cols))
  {
    Transmit(thumb, cols, rows);
  }

  p_ColorPair = ColorPairForId(thumb.imageId);
  p_Cols = cols;
  p_Rows = rows;
  return true;
}

std::wstring UiImage::PlaceholderRow(int p_Row, int p_Cols)
{
  // every cell carries row and column diacritics, so cells stay correct
  // even when curses redraws only part of a line
  std::wstring line;
  const wchar_t rowMark = s_Diacritics[std::min(p_Row, s_DiacriticCount - 1)];
  for (int col = 0; col < p_Cols; ++col)
  {
    line += s_Placeholder;
    line += rowMark;
    line += s_Diacritics[std::min(col, s_DiacriticCount - 1)];
  }

  return line;
}

bool UiImage::IsPlaceholderLine(const std::wstring& p_Line)
{
  return !p_Line.empty() && (p_Line.at(0) == s_Placeholder);
}

bool UiImage::TakeUpdated()
{
  return s_Updated.exchange(false);
}

void UiImage::InvalidateTransmitted()
{
  std::unique_lock<std::mutex> lock(s_Mutex);
  for (auto& entry : s_Thumbs)
  {
    entry.second.transmitted = false;
  }
}
