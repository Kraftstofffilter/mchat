// uilock.cpp
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "uilock.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#include <atomic>
#include <cstdlib>
#include <thread>

#include <sys/socket.h>
#include <sys/un.h>

#include "fileutil.h"
#include "uiconfig.h"
#include "uikeyconfig.h"

namespace
{
  // ---- SHA-256, HMAC, PBKDF2 (FIPS 180-4, RFC 2104, RFC 8018) ----------

  struct Sha256
  {
    uint32_t h[8];
    uint8_t buf[64];
    uint64_t len = 0;
    size_t used = 0;

    Sha256()
    {
      static const uint32_t init[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
      memcpy(h, init, sizeof(h));
    }

    static uint32_t Rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

    void Block(const uint8_t* p)
    {
      static const uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2 };
      uint32_t w[64];
      for (int i = 0; i < 16; ++i)
      {
        w[i] = ((uint32_t)p[i * 4] << 24) | ((uint32_t)p[i * 4 + 1] << 16) | ((uint32_t)p[i * 4 + 2] << 8) |
          p[i * 4 + 3];
      }
      for (int i = 16; i < 64; ++i)
      {
        const uint32_t s0 = Rotr(w[i - 15], 7) ^ Rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t s1 = Rotr(w[i - 2], 17) ^ Rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
      }

      uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
      for (int i = 0; i < 64; ++i)
      {
        const uint32_t t1 = hh + (Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
        const uint32_t t2 = (Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
      }

      h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }

    void Update(const uint8_t* p, size_t n)
    {
      len += n;
      while (n > 0)
      {
        const size_t take = std::min(n, 64 - used);
        memcpy(buf + used, p, take);
        used += take;
        p += take;
        n -= take;
        if (used == 64)
        {
          Block(buf);
          used = 0;
        }
      }
    }

    void Final(uint8_t out[32])
    {
      const uint64_t bits = len * 8;
      const uint8_t pad = 0x80;
      Update(&pad, 1);
      const uint8_t zero = 0;
      while (used != 56) Update(&zero, 1);
      uint8_t lenBytes[8];
      for (int i = 0; i < 8; ++i) lenBytes[i] = (uint8_t)(bits >> (56 - (i * 8)));
      Update(lenBytes, 8);
      for (int i = 0; i < 8; ++i)
      {
        out[i * 4] = (uint8_t)(h[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(h[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(h[i] >> 8);
        out[i * 4 + 3] = (uint8_t)h[i];
      }
    }
  };

  void HmacSha256(const std::string& p_Key, const uint8_t* p_Msg, size_t p_Len, uint8_t p_Out[32])
  {
    uint8_t key[64] = { 0 };
    if (p_Key.size() > 64)
    {
      Sha256 s;
      s.Update(reinterpret_cast<const uint8_t*>(p_Key.data()), p_Key.size());
      s.Final(key);
    }
    else
    {
      memcpy(key, p_Key.data(), p_Key.size());
    }

    uint8_t ipad[64];
    uint8_t opad[64];
    for (int i = 0; i < 64; ++i)
    {
      ipad[i] = key[i] ^ 0x36;
      opad[i] = key[i] ^ 0x5c;
    }

    uint8_t inner[32];
    Sha256 si;
    si.Update(ipad, 64);
    si.Update(p_Msg, p_Len);
    si.Final(inner);
    Sha256 so;
    so.Update(opad, 64);
    so.Update(inner, 32);
    so.Final(p_Out);
  }

  // PBKDF2-HMAC-SHA256, one 32-byte block
  std::vector<uint8_t> Pbkdf2(const std::string& p_Pin, const std::vector<uint8_t>& p_Salt, int p_Iterations)
  {
    std::vector<uint8_t> msg(p_Salt);
    msg.push_back(0);
    msg.push_back(0);
    msg.push_back(0);
    msg.push_back(1);
    uint8_t u[32];
    HmacSha256(p_Pin, msg.data(), msg.size(), u);
    std::vector<uint8_t> t(u, u + 32);
    for (int i = 1; i < p_Iterations; ++i)
    {
      uint8_t next[32];
      HmacSha256(p_Pin, u, 32, next);
      memcpy(u, next, 32);
      for (int j = 0; j < 32; ++j) t[j] ^= u[j];
    }

    return t;
  }

  std::string ToHex(const std::vector<uint8_t>& p_Data)
  {
    static const char* digits = "0123456789abcdef";
    std::string out;
    for (uint8_t b : p_Data)
    {
      out += digits[b >> 4];
      out += digits[b & 15];
    }

    return out;
  }

  std::vector<uint8_t> FromHex(const std::string& p_Hex)
  {
    std::vector<uint8_t> out;
    for (size_t i = 0; (i + 1) < p_Hex.size(); i += 2)
    {
      out.push_back((uint8_t)strtoul(p_Hex.substr(i, 2).c_str(), nullptr, 16));
    }

    return out;
  }

  // ---- PIN file ---------------------------------------------------------

  const int s_Iterations = 200000;

  std::string PinPath()
  {
    return FileUtil::GetApplicationDir() + "/lock.pin";
  }

  bool HasPin()
  {
    return FileUtil::Exists(PinPath());
  }

  bool StorePin(const std::string& p_Pin)
  {
    std::vector<uint8_t> salt(16);
    int fd = open("/dev/urandom", O_RDONLY);
    if ((fd < 0) || (read(fd, salt.data(), salt.size()) != (ssize_t)salt.size()))
    {
      if (fd >= 0) close(fd);
      return false;
    }
    close(fd);

    // format: v1:<iterations>:<salt hex>:<hash hex>
    const std::string line = "v1:" + std::to_string(s_Iterations) + ":" + ToHex(salt) + ":" +
      ToHex(Pbkdf2(p_Pin, salt, s_Iterations)) + "\n";
    const std::string tmpPath = PinPath() + ".tmp";
    fd = open(tmpPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return false;

    const bool ok = (write(fd, line.data(), line.size()) == (ssize_t)line.size());
    close(fd);
    return ok && (rename(tmpPath.c_str(), PinPath().c_str()) == 0);
  }

  bool CheckPin(const std::string& p_Pin)
  {
    std::ifstream file(PinPath());
    std::string line;
    if (!std::getline(file, line)) return false;

    const size_t a = line.find(':');
    const size_t b = line.find(':', a + 1);
    const size_t c = line.find(':', b + 1);
    if ((a == std::string::npos) || (b == std::string::npos) || (c == std::string::npos)) return false;
    if (line.substr(0, a) != "v1") return false;

    const int iterations = atoi(line.substr(a + 1, b - a - 1).c_str());
    const std::vector<uint8_t> salt = FromHex(line.substr(b + 1, c - b - 1));
    const std::vector<uint8_t> expected = FromHex(line.substr(c + 1));
    if ((iterations <= 0) || salt.empty() || (expected.size() != 32)) return false;

    const std::vector<uint8_t> actual = Pbkdf2(p_Pin, salt, iterations);
    uint8_t diff = 0;
    for (size_t i = 0; i < 32; ++i) diff |= actual[i] ^ expected[i];
    return diff == 0;
  }

  bool IsValidPin(const std::string& p_Pin)
  {
    if ((p_Pin.size() < 4) || (p_Pin.size() > 8)) return false;

    for (char ch : p_Pin)
    {
      if ((ch < '0') || (ch > '9')) return false;
    }

    return true;
  }

  // ---- state --------------------------------------------------------------

  enum Mode { Unlocked, EnterPin, NewPin, RepeatPin };

  Mode s_Mode = Unlocked;
  std::string s_Input;
  std::string s_NewPin;
  std::string s_Message;
  int s_Failures = 0;
  int64_t s_LockoutUntilMs = 0;
  int64_t s_LastActivityMs = 0;
  bool s_Dirty = true;
  // disguise: previous prompt lines, like an idle shell after Enter presses
  int s_PromptLines = 1;
  std::atomic<bool> s_PaneUnfocused(false);

  int64_t NowMs()
  {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
  }

  // lock screen colors: gray on black, extended pair below thumbnail range
  int LockPair()
  {
    static const int pair = []()
    {
      const int p = 1000;
      init_extended_pair(p, (COLORS > 8) ? 244 : COLOR_WHITE, COLOR_BLACK);
      return p;
    }();
    return pair;
  }
}

namespace
{
  // herdr does not forward focus changes between its panes; ask its API
  // whether this pane is still the focused one
  bool QueryHerdrPaneFocused(const std::string& p_Socket, const std::string& p_PaneId, bool& p_Focused)
  {
    const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return false;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, p_Socket.c_str(), sizeof(addr.sun_path) - 1);
    struct timeval tv = { 2, 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    if (connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0)
    {
      close(fd);
      return false;
    }

    const std::string req = "{\"id\":\"mchat-lock\",\"method\":\"pane.get\",\"params\":{\"pane_id\":\"" +
      p_PaneId + "\"}}\n";
    std::string resp;
    if (write(fd, req.data(), req.size()) == (ssize_t)req.size())
    {
      char buf[4096];
      ssize_t n = 0;
      while ((resp.find('\n') == std::string::npos) && ((n = read(fd, buf, sizeof(buf))) > 0))
      {
        resp.append(buf, n);
      }
    }

    close(fd);
    if (resp.find("\"focused\":false") != std::string::npos)
    {
      p_Focused = false;
      return true;
    }

    if (resp.find("\"focused\":true") != std::string::npos)
    {
      p_Focused = true;
      return true;
    }

    return false;
  }

  // the pane to watch: HERDR_PANE_ID when started in herdr, otherwise the
  // pane noted by the attach wrapper (mchat running detached under dtach)
  std::string CurrentPaneId(const std::string& p_EnvPaneId)
  {
    if (!p_EnvPaneId.empty()) return p_EnvPaneId;

    std::ifstream file(FileUtil::ExpandPath("~/.local/share/mchat/attached-pane"));
    std::string paneId;
    std::getline(file, paneId);
    return paneId;
  }

  void HerdrFocusWatcher(std::string p_Socket, std::string p_EnvPaneId)
  {
    bool wasFocused = true;
    std::string prevPaneId;
    while (true)
    {
      const std::string paneId = CurrentPaneId(p_EnvPaneId);
      if (paneId != prevPaneId)
      {
        wasFocused = true;
        prevPaneId = paneId;
      }

      bool focused = true;
      if (!paneId.empty() && QueryHerdrPaneFocused(p_Socket, paneId, focused))
      {
        if (wasFocused && !focused)
        {
          s_PaneUnfocused = true;
        }

        wasFocused = focused;
      }

      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  }
}

void UiLock::StartFocusWatch()
{
  static bool started = false;
  if (started) return;

  started = true;
  const char* socketPath = getenv("HERDR_SOCKET_PATH");
  const char* paneId = getenv("HERDR_PANE_ID");
  std::string socket = (socketPath != nullptr) ? socketPath : "";
  if (socket.empty())
  {
    // detached under dtach: herdr's default socket
    socket = FileUtil::ExpandPath("~/.config/herdr/herdr.sock");
  }

  if (!FileUtil::Exists(socket)) return;

  std::thread(HerdrFocusWatcher, socket, std::string((paneId != nullptr) ? paneId : "")).detach();
}

bool UiLock::TakePaneUnfocused()
{
  return s_PaneUnfocused.exchange(false);
}

bool UiLock::IsLocked()
{
  return s_Mode != Unlocked;
}

void UiLock::Lock()
{
  if (IsLocked()) return;

  s_Input.clear();
  s_NewPin.clear();
  if (HasPin())
  {
    s_Mode = EnterPin;
    s_Message.clear();
    s_PromptLines = 1;
  }
  else
  {
    s_Mode = NewPin;
    s_Message = "New PIN (4-8 digits):";
  }

  s_Dirty = true;
}

void UiLock::NoteActivity()
{
  s_LastActivityMs = NowMs();
}

int64_t UiLock::IdleSec()
{
  if (s_LastActivityMs == 0) s_LastActivityMs = NowMs();
  return (NowMs() - s_LastActivityMs) / 1000;
}

void UiLock::SetDirty()
{
  s_Dirty = true;
}

bool UiLock::Key(wint_t p_Key)
{
  static const wint_t keyOk = UiKeyConfig::GetKey("ok");
  static const wint_t keyCancel = UiKeyConfig::GetKey("cancel");
  static const wint_t keyBackspace = UiKeyConfig::GetKey("backspace");
  static const wint_t keyBackspaceAlt = UiKeyConfig::GetKey("backspace_alt");

  if (!IsLocked()) return false;

  s_Dirty = true;
  if ((p_Key >= L'0') && (p_Key <= L'9'))
  {
    if (s_Input.size() < 8) s_Input += (char)p_Key;
    return false;
  }

  if ((p_Key == keyBackspace) || (p_Key == keyBackspaceAlt) || (p_Key == 127) || (p_Key == 8))
  {
    if (!s_Input.empty()) s_Input.pop_back();
    return false;
  }

  if ((p_Key == keyCancel) || (p_Key == 27))
  {
    s_Input.clear();
    if ((s_Mode == NewPin) || (s_Mode == RepeatPin))
    {
      // no PIN yet: cancelling the setup unlocks; the next lock asks again
      s_Mode = Unlocked;
      NoteActivity();
      return true;
    }

    return false;
  }

  if ((p_Key != keyOk) && (p_Key != L'\n') && (p_Key != L'\r') && (p_Key != KEY_ENTER)) return false;

  const std::string input = s_Input;
  s_Input.clear();
  if (s_Mode == EnterPin)
  {
    // disguised: every Enter just shows a new prompt line, like a shell;
    // during the 30 s lockout input is ignored without a sign
    ++s_PromptLines;
    const int64_t now = NowMs();
    if (input.empty() || (now < s_LockoutUntilMs)) return false;

    if (CheckPin(input))
    {
      s_Failures = 0;
      s_Mode = Unlocked;
      NoteActivity();
      return true;
    }

    if (++s_Failures >= 5)
    {
      s_Failures = 0;
      s_LockoutUntilMs = now + 30000;
    }
  }
  else if (s_Mode == NewPin)
  {
    if (!IsValidPin(input))
    {
      s_Message = "New PIN (4-8 digits):";
      return false;
    }

    s_NewPin = input;
    s_Mode = RepeatPin;
    s_Message = "Repeat PIN:";
  }
  else if (s_Mode == RepeatPin)
  {
    if ((input != s_NewPin) || !StorePin(input))
    {
      s_NewPin.clear();
      s_Mode = NewPin;
      s_Message = "PINs differ - New PIN (4-8 digits):";
      return false;
    }

    s_NewPin.clear();
    s_Mode = Unlocked;
    NoteActivity();
    return true;
  }

  return false;
}

void UiLock::Draw(bool p_Force /*= false*/)
{
  if (!IsLocked() || (!s_Dirty && !p_Force)) return;

  s_Dirty = false;
  curs_set(0);

  int pair = LockPair();
  wattr_set(stdscr, A_NORMAL, 0, &pair);
  const std::string blank(std::max(COLS, 0), ' ');
  for (int y = 0; y < LINES; ++y)
  {
    mvwaddnstr(stdscr, y, 0, blank.c_str(), blank.size());
  }

  if (s_Mode == EnterPin)
  {
    // disguise: an idle shell; the PIN is typed without echo
    static const std::string prompt = []()
    {
      std::string configured = UiConfig::GetStr("lock_prompt");
      if (!configured.empty()) return configured + " ";

      const char* user = getenv("USER");
      char host[256] = { 0 };
      gethostname(host, sizeof(host) - 1);
      return std::string(user ? user : "user") + "@" + host + ":~/m$ ";
    }();

    const int lines = std::min(s_PromptLines, std::max(LINES, 1));
    for (int y = 0; y < lines; ++y)
    {
      mvwaddstr(stdscr, y, 0, prompt.c_str());
    }

    wattr_set(stdscr, A_NORMAL, 0, nullptr);
    touchwin(stdscr);
    wmove(stdscr, lines - 1, std::min((int)prompt.size(), std::max(COLS - 1, 0)));
    wrefresh(stdscr);
    curs_set(1);
    return;
  }

  // PIN setup (first lock): visible prompts, shown only to the owner
  std::string text = s_Message;
  if (!s_Input.empty())
  {
    text = s_Message + " ";
    for (size_t i = 0; i < s_Input.size(); ++i) text += "\xe2\x80\xa2"; // bullet
  }

  const int textW = (int)text.size() - (int)(s_Input.size() * 2); // bullets are 3 bytes, 1 column
  mvwaddstr(stdscr, LINES / 2, std::max((COLS - textW) / 2, 0), text.c_str());
  wattr_set(stdscr, A_NORMAL, 0, nullptr);

  // stdscr overlaps every view window; touch it so all cells are rewritten
  touchwin(stdscr);
  wrefresh(stdscr);
}

int UiLock::SetPinCli()
{
  struct termios oldTerm;
  const bool isTty = (tcgetattr(STDIN_FILENO, &oldTerm) == 0);
  auto readHidden = [&](const std::string& p_Prompt)
  {
    std::cout << p_Prompt << std::flush;
    if (isTty)
    {
      struct termios noEcho = oldTerm;
      noEcho.c_lflag &= ~ECHO;
      tcsetattr(STDIN_FILENO, TCSANOW, &noEcho);
    }

    std::string line;
    std::getline(std::cin, line);
    if (isTty) tcsetattr(STDIN_FILENO, TCSANOW, &oldTerm);
    std::cout << "\n";
    return line;
  };

  if (HasPin())
  {
    if (!CheckPin(readHidden("Current PIN: ")))
    {
      std::cerr << "wrong PIN. If it is forgotten, delete " << PinPath() << " and run again.\n";
      return 1;
    }
  }

  const std::string pin = readHidden("New PIN (4-8 digits): ");
  if (!IsValidPin(pin))
  {
    std::cerr << "a PIN has 4 to 8 digits.\n";
    return 1;
  }

  if (readHidden("Repeat PIN: ") != pin)
  {
    std::cerr << "PINs differ.\n";
    return 1;
  }

  if (!StorePin(pin))
  {
    std::cerr << "cannot write " << PinPath() << "\n";
    return 1;
  }

  std::cout << "PIN set.\n";
  return 0;
}
