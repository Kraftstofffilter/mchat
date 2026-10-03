// uiimage.cpp
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "uiimage.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include "fileutil.h"
#include "strutil.h"
#include "sysutil.h"
#include "uiconfig.h"

namespace
{
  enum State { Pending, Ready, Failed };

  struct Thumb
  {
    State state = Pending;
    int w = 0; // pixels = cells
    int h = 0; // pixels = 2 * rows
    std::vector<unsigned char> colors; // xterm-256 index per pixel
  };

  std::mutex s_Mutex;
  std::condition_variable s_Cond;
  std::vector<Thumb> s_Thumbs; // handle = index
  std::unordered_map<std::string, int> s_Handles;
  std::deque<std::pair<std::string, int>> s_Queue;
  std::atomic<bool> s_Updated(false);
  bool s_WorkerStarted = false;

  // nearest color in the xterm 256-color palette (6x6x6 cube or gray ramp)
  unsigned char ToXterm256(int p_R, int p_G, int p_B)
  {
    static const int levels[6] = { 0, 95, 135, 175, 215, 255 };
    auto cubeIndex = [&](int v)
    {
      int best = 0;
      for (int i = 1; i < 6; ++i)
      {
        if (std::abs(levels[i] - v) < std::abs(levels[best] - v)) best = i;
      }
      return best;
    };

    const int ri = cubeIndex(p_R);
    const int gi = cubeIndex(p_G);
    const int bi = cubeIndex(p_B);
    const int cr = levels[ri];
    const int cg = levels[gi];
    const int cb = levels[bi];
    const int cubeDist = (cr - p_R) * (cr - p_R) + (cg - p_G) * (cg - p_G) + (cb - p_B) * (cb - p_B);

    const int avg = (p_R + p_G + p_B) / 3;
    const int gi2 = std::min(std::max((avg - 8 + 5) / 10, 0), 23);
    const int gv = 8 + (10 * gi2);
    const int grayDist = (gv - p_R) * (gv - p_R) + (gv - p_G) * (gv - p_G) + (gv - p_B) * (gv - p_B);

    if (grayDist < cubeDist) return (unsigned char)(232 + gi2);

    return (unsigned char)(16 + (36 * ri) + (6 * gi) + bi);
  }

  bool ReadPpm(const std::string& p_Path, Thumb& p_Thumb)
  {
    const std::string data = FileUtil::ReadFile(p_Path);
    int w = 0;
    int h = 0;
    int maxval = 0;
    int consumed = 0;
    if (sscanf(data.c_str(), "P6 %d %d %d%n", &w, &h, &maxval, &consumed) != 3) return false;
    if ((w <= 0) || (h <= 0) || (maxval != 255)) return false;

    const size_t offset = consumed + 1; // single whitespace after maxval
    if (data.size() < (offset + ((size_t)w * h * 3))) return false;

    p_Thumb.w = w;
    p_Thumb.h = h;
    p_Thumb.colors.resize((size_t)w * h);
    const unsigned char* px = reinterpret_cast<const unsigned char*>(data.data() + offset);
    for (size_t i = 0; i < p_Thumb.colors.size(); ++i)
    {
      p_Thumb.colors[i] = ToXterm256(px[(i * 3)], px[(i * 3) + 1], px[(i * 3) + 2]);
    }

    return true;
  }

  void Worker()
  {
    static const std::string cmdTemplate = UiConfig::GetStr("thumbnail_command");
    static const std::string cacheDir = FileUtil::ExpandPath("~/.cache/mchat/thumbs");
    FileUtil::MkDir(cacheDir);

    while (true)
    {
      std::pair<std::string, int> job;
      {
        std::unique_lock<std::mutex> lock(s_Mutex);
        s_Cond.wait(lock, [] { return !s_Queue.empty(); });
        job = s_Queue.front();
        s_Queue.pop_front();
      }

      const std::string& path = job.first;
      char name[48];
      snprintf(name, sizeof(name), "%016zx-%d.ppm", std::hash<std::string>{}(path), UiImage::Rows());
      const std::string thumbPath = cacheDir + "/" + name;

      Thumb thumb;
      bool ok = FileUtil::Exists(thumbPath) && ReadPpm(thumbPath, thumb);
      if (!ok)
      {
        std::string cmd = cmdTemplate;
        StrUtil::ReplaceString(cmd, "%1", StrUtil::EscapeSingleQuote(path));
        StrUtil::ReplaceString(cmd, "%2", StrUtil::EscapeSingleQuote(thumbPath));
        StrUtil::ReplaceString(cmd, "%3", std::to_string(UiImage::Rows()));
        SysUtil::System(cmd + " >/dev/null 2>&1");
        ok = FileUtil::Exists(thumbPath) && ReadPpm(thumbPath, thumb);
      }

      {
        std::unique_lock<std::mutex> lock(s_Mutex);
        thumb.state = ok ? Ready : Failed;
        s_Thumbs[job.second] = std::move(thumb);
      }

      s_Updated = true;
    }
  }

  // extended color pair for a foreground/background combination, allocated
  // downwards from the top of the pair range, away from nchat's own pairs
  int PairFor(unsigned char p_Fg, unsigned char p_Bg)
  {
    static std::vector<int> s_Pairs(256 * 256, 0);
    static int s_NextPair = COLOR_PAIRS - 1;
    const int key = (p_Fg << 8) | p_Bg;
    if (s_Pairs[key] == 0)
    {
      if (s_NextPair <= 1024) return 0; // pool exhausted: default colors

      init_extended_pair(s_NextPair, p_Fg, p_Bg);
      s_Pairs[key] = s_NextPair--;
    }

    return s_Pairs[key];
  }
}

bool UiImage::Enabled()
{
  static const bool enabled = !UiConfig::GetStr("thumbnail_command").empty() && (Rows() > 0) &&
    (COLORS >= 256) && (COLOR_PAIRS > 2048);
  return enabled;
}

int UiImage::Rows()
{
  static const int rows = std::min(std::max((int)UiConfig::GetNum("thumbnail_rows"), 0), 32);
  return rows;
}

bool UiImage::GetThumbnail(const std::string& p_Path, int p_MaxCols, int& p_Handle, int& p_Cols, int& p_Rows)
{
  if (!Enabled() || p_Path.empty() || (p_MaxCols <= 0)) return false;

  std::unique_lock<std::mutex> lock(s_Mutex);
  auto it = s_Handles.find(p_Path);
  if (it == s_Handles.end())
  {
    const int handle = s_Thumbs.size();
    s_Thumbs.emplace_back();
    s_Handles[p_Path] = handle;
    s_Queue.emplace_back(p_Path, handle);
    if (!s_WorkerStarted)
    {
      std::thread(Worker).detach();
      s_WorkerStarted = true;
    }

    s_Cond.notify_one();
    return false;
  }

  const Thumb& thumb = s_Thumbs[it->second];
  if (thumb.state != Ready) return false;

  p_Handle = it->second;
  p_Cols = std::min(thumb.w, p_MaxCols);
  p_Rows = thumb.h / 2;
  return true;
}

void UiImage::DrawRow(WINDOW* p_Win, int p_Y, int p_X, int p_Handle, int p_Row, int p_Cols)
{
  std::unique_lock<std::mutex> lock(s_Mutex);
  if ((p_Handle < 0) || (p_Handle >= (int)s_Thumbs.size())) return;

  const Thumb& thumb = s_Thumbs[p_Handle];
  if ((thumb.state != Ready) || (((p_Row * 2) + 1) >= thumb.h)) return;

  const int cols = std::min(p_Cols, thumb.w);
  for (int col = 0; col < cols; ++col)
  {
    const unsigned char top = thumb.colors[((size_t)(p_Row * 2) * thumb.w) + col];
    const unsigned char bottom = thumb.colors[((size_t)((p_Row * 2) + 1) * thumb.w) + col];
    int pair = PairFor(top, bottom);
    wattr_set(p_Win, A_NORMAL, 0, &pair);
    mvwaddnwstr(p_Win, p_Y, p_X + col, L"▀", 1);
  }

  wattr_set(p_Win, A_NORMAL, 0, nullptr);
}

bool UiImage::TakeUpdated()
{
  return s_Updated.exchange(false);
}
