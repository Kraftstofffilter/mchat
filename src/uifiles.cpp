// uifiles.cpp
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "uifiles.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <set>
#include <thread>
#include <utility>
#include <vector>

#include <ftw.h>
#include <glob.h>
#include <sys/stat.h>
#include <unistd.h>

#include "fileutil.h"
#include "uiconfig.h"

namespace
{
  std::mutex s_Mutex;
  std::condition_variable s_Cond;
  bool s_RefreshRequested = false;
  bool s_Started = false;
  std::atomic<int64_t> s_Bytes(-1);
  std::atomic<bool> s_Changed(false);

  // folders and the minimum depth of files counted in each (Telegram media
  // lives in subfolders of tdlib, whose top level holds the session database)
  std::vector<std::pair<std::string, int>> GetRoots()
  {
    std::vector<std::pair<std::string, int>> roots;
    const std::string linkDir = FileUtil::ExpandPath(UiConfig::GetStr("attachment_link_dir"));
    if (!linkDir.empty()) roots.emplace_back(linkDir, 1);

    roots.emplace_back(FileUtil::ExpandPath("~/.cache/mchat/thumbs"), 1);

    const std::string profilesDir = FileUtil::GetApplicationDir() + "/profiles";
    glob_t g;
    if (glob((profilesDir + "/WhatsAppMd_*/tmp").c_str(), 0, nullptr, &g) == 0)
    {
      for (size_t i = 0; i < g.gl_pathc; ++i) roots.emplace_back(g.gl_pathv[i], 1);
    }
    globfree(&g);

    if (glob((profilesDir + "/Telegram_*/tdlib").c_str(), 0, nullptr, &g) == 0)
    {
      for (size_t i = 0; i < g.gl_pathc; ++i) roots.emplace_back(g.gl_pathv[i], 2);
    }
    globfree(&g);

    return roots;
  }

  // nftw has no user pointer: walk state in thread-local statics
  thread_local int t_MinLevel = 1;
  thread_local bool t_Delete = false;
  thread_local uint64_t t_Bytes = 0;
  thread_local std::set<std::pair<dev_t, ino_t>>* t_Seen = nullptr;

  int Visit(const char* p_Path, const struct stat* p_Stat, int p_Flag, struct FTW* p_Ftw)
  {
    if ((p_Flag == FTW_F) && (p_Ftw->level >= t_MinLevel))
    {
      // hard links (published attachments) count once
      if (t_Seen->insert(std::make_pair(p_Stat->st_dev, p_Stat->st_ino)).second)
      {
        t_Bytes += p_Stat->st_size;
      }

      if (t_Delete)
      {
        unlink(p_Path);
      }
    }
    else if (t_Delete && (p_Flag == FTW_DP) && (p_Ftw->level >= t_MinLevel))
    {
      rmdir(p_Path); // only succeeds when empty
    }

    return 0;
  }

  uint64_t Walk(bool p_Delete)
  {
    std::set<std::pair<dev_t, ino_t>> seen;
    t_Seen = &seen;
    t_Delete = p_Delete;
    t_Bytes = 0;
    for (const auto& root : GetRoots())
    {
      t_MinLevel = root.second;
      nftw(root.first.c_str(), Visit, 16, FTW_PHYS | FTW_DEPTH);
    }

    t_Seen = nullptr;
    return t_Bytes;
  }

  void Worker()
  {
    while (true)
    {
      const int64_t bytes = Walk(false);
      if (bytes != s_Bytes.exchange(bytes))
      {
        s_Changed = true;
      }

      std::unique_lock<std::mutex> lock(s_Mutex);
      s_Cond.wait_for(lock, std::chrono::seconds(60), [] { return s_RefreshRequested; });
      s_RefreshRequested = false;
    }
  }

  void EnsureStarted()
  {
    std::unique_lock<std::mutex> lock(s_Mutex);
    if (!s_Started)
    {
      std::thread(Worker).detach();
      s_Started = true;
    }
  }

  std::string FormatSize(uint64_t p_Bytes)
  {
    char buf[32];
    if (p_Bytes >= (1024ull * 1024 * 1024))
    {
      snprintf(buf, sizeof(buf), "%.1fG", p_Bytes / (1024.0 * 1024 * 1024));
    }
    else if (p_Bytes >= (1024ull * 1024))
    {
      snprintf(buf, sizeof(buf), "%lluM", (unsigned long long)(p_Bytes / (1024 * 1024)));
    }
    else
    {
      snprintf(buf, sizeof(buf), "%lluK", (unsigned long long)(p_Bytes / 1024));
    }

    return buf;
  }
}

std::string UiFiles::SizeLabel()
{
  EnsureStarted();
  const int64_t bytes = s_Bytes;
  return (bytes < 0) ? "Files ..." : ("Files " + FormatSize(bytes));
}

void UiFiles::Refresh()
{
  EnsureStarted();
  std::unique_lock<std::mutex> lock(s_Mutex);
  s_RefreshRequested = true;
  s_Cond.notify_one();
}

uint64_t UiFiles::Clean()
{
  const uint64_t freed = Walk(true);
  Refresh();
  return freed;
}

bool UiFiles::TakeChanged()
{
  return s_Changed.exchange(false);
}
