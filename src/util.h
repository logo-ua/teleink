// Text, time and file helpers for the PocketBook Telegram client.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <ctime>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>

#include "i18n.h"

namespace util {

// ---------- logging ----------
inline std::string &logPath() {
  static std::string p;
  return p;
}

inline void log(const char *fmt, ...) {
  if (logPath().empty()) return;
  FILE *f = fopen(logPath().c_str(), "a");
  if (!f) return;
  time_t t = time(nullptr);
  struct tm tmv;
  localtime_r(&t, &tmv);
  fprintf(f, "%02d:%02d:%02d ", tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
  va_list ap;
  va_start(ap, fmt);
  vfprintf(f, fmt, ap);
  va_end(ap);
  fputc('\n', f);
  fclose(f);
}

// ---------- UTF-8 ----------
// Decodes one code point starting at s[i]; advances i. Returns 0xFFFD on bad input.
inline uint32_t decode(const std::string &s, size_t &i) {
  unsigned char c = static_cast<unsigned char>(s[i]);
  uint32_t cp;
  int extra;
  if (c < 0x80) { i++; return c; }
  if ((c >> 5) == 6) { cp = c & 0x1F; extra = 1; }
  else if ((c >> 4) == 14) { cp = c & 0x0F; extra = 2; }
  else if ((c >> 3) == 30) { cp = c & 0x07; extra = 3; }
  else { i++; return 0xFFFD; }
  size_t j = i + 1;
  for (int k = 0; k < extra; k++, j++) {
    if (j >= s.size()) { i = s.size(); return 0xFFFD; }
    unsigned char cc = static_cast<unsigned char>(s[j]);
    if ((cc >> 6) != 2) { i = j; return 0xFFFD; }
    cp = (cp << 6) | (cc & 0x3F);
  }
  i = j;
  return cp;
}

inline void encode(uint32_t cp, std::string &out) {
  if (cp < 0x80) out += static_cast<char>(cp);
  else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

inline bool isEmoji(uint32_t cp) {
  return (cp >= 0x1F000 && cp <= 0x1FAFF) ||  // emoji & pictographs, flags, skin tones
         (cp >= 0x2600 && cp <= 0x27BF) ||    // misc symbols, dingbats
         (cp >= 0x2B00 && cp <= 0x2BFF) ||    // arrows/stars used as emoji
         cp == 0xFE0F || cp == 0xFE0E || cp == 0x200D || cp == 0x20E3 ||
         (cp >= 0xE0020 && cp <= 0xE007F) ||  // tag characters
         (cp >= 0xE000 && cp <= 0xF8FF);      // private use
}

// Removes emoji (e-ink fonts have no glyphs for them) and normalises odd spaces.
// If nothing but emoji is left, returns the original text so the bubble is not empty.
inline std::string clean(const std::string &s, bool singleLine = false) {
  std::string out;
  out.reserve(s.size());
  size_t i = 0;
  bool hadEmoji = false;
  while (i < s.size()) {
    uint32_t cp = decode(s, i);
    if (isEmoji(cp)) { hadEmoji = true; continue; }
    if (cp == '\r') continue;
    if (singleLine && (cp == '\n' || cp == '\t')) cp = ' ';
    if (cp == 0x00A0 || cp == 0x2009 || cp == 0x202F) cp = ' ';
    encode(cp, out);
  }
  if (singleLine) {
    std::string squashed;
    bool sp = false;
    for (char c : out) {
      if (c == ' ') { if (!sp && !squashed.empty()) squashed += c; sp = true; }
      else { squashed += c; sp = false; }
    }
    while (!squashed.empty() && squashed.back() == ' ') squashed.pop_back();
    out.swap(squashed);
  } else {
    while (!out.empty() && (out.back() == ' ' || out.back() == '\n')) out.pop_back();
  }
  bool blank = true;
  for (char c : out) if (c != ' ' && c != '\n') { blank = false; break; }
  if (blank && hadEmoji) return tr(S_EMOJI);
  return out;
}

// Truncates to at most maxChars code points, adding an ellipsis.
inline std::string truncate(const std::string &s, size_t maxChars) {
  size_t i = 0, n = 0;
  while (i < s.size() && n < maxChars) { decode(s, i); n++; }
  if (i >= s.size()) return s;
  return s.substr(0, i) + "…";
}

// ---------- time ----------
inline std::string hhmm(int64_t ts) {
  time_t t = static_cast<time_t>(ts);
  struct tm tmv;
  localtime_r(&t, &tmv);
  char b[16];
  snprintf(b, sizeof b, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
  return b;
}

inline int dayIndex(int64_t ts) {
  time_t t = static_cast<time_t>(ts);
  struct tm tmv;
  localtime_r(&t, &tmv);
  return (tmv.tm_year + 1900) * 1000 + tmv.tm_yday;
}

inline std::string shortDate(int64_t ts) {
  if (ts <= 0) return "";
  time_t now = time(nullptr);
  if (dayIndex(ts) == dayIndex(now)) return hhmm(ts);
  time_t t = static_cast<time_t>(ts);
  struct tm tmv;
  localtime_r(&t, &tmv);
  if (now - ts < 6 * 86400 && now >= ts) return i18n::weekday(tmv.tm_wday);
  char b[16];
  snprintf(b, sizeof b, "%02d.%02d.%02d", tmv.tm_mday, tmv.tm_mon + 1, tmv.tm_year % 100);
  return b;
}

inline std::string longDate(int64_t ts) {
  time_t t = static_cast<time_t>(ts);
  struct tm tmv;
  localtime_r(&t, &tmv);
  time_t now = time(nullptr);
  if (dayIndex(ts) == dayIndex(now)) return tr(S_TODAY);
  if (dayIndex(ts) == dayIndex(now - 86400)) return tr(S_YESTERDAY);
  struct tm nowtm;
  localtime_r(&now, &nowtm);
  char b[64];
  if (nowtm.tm_year == tmv.tm_year)
    snprintf(b, sizeof b, "%d %s", tmv.tm_mday, i18n::month(tmv.tm_mon));
  else
    snprintf(b, sizeof b, "%d %s %d", tmv.tm_mday, i18n::month(tmv.tm_mon), tmv.tm_year + 1900);
  return b;
}

inline std::string duration(int sec) {
  char b[32];
  if (sec >= 3600) snprintf(b, sizeof b, "%d:%02d:%02d", sec / 3600, (sec / 60) % 60, sec % 60);
  else snprintf(b, sizeof b, "%d:%02d", sec / 60, sec % 60);
  return b;
}

inline std::string fileSize(int64_t bytes) {
  char b[32];
  if (bytes < 1024) snprintf(b, sizeof b, "%d %s", static_cast<int>(bytes), i18n::sizeUnit(0));
  else if (bytes < 1024 * 1024) snprintf(b, sizeof b, "%.0f %s", bytes / 1024.0, i18n::sizeUnit(1));
  else if (bytes < 1024LL * 1024 * 1024) snprintf(b, sizeof b, "%.1f %s", bytes / (1024.0 * 1024), i18n::sizeUnit(2));
  else snprintf(b, sizeof b, "%.2f %s", bytes / (1024.0 * 1024 * 1024), i18n::sizeUnit(3));
  return b;
}

// ---------- files ----------
inline bool exists(const std::string &p) {
  struct stat st;
  return stat(p.c_str(), &st) == 0;
}

inline int64_t sizeOf(const std::string &p) {
  struct stat st;
  if (stat(p.c_str(), &st) != 0) return -1;
  return st.st_size;
}

inline void mkdirs(const std::string &path) {
  std::string cur;
  for (size_t i = 0; i < path.size(); i++) {
    cur += path[i];
    if ((path[i] == '/' && i > 0) || i + 1 == path.size()) mkdir(cur.c_str(), 0755);
  }
}

inline bool copyFile(const std::string &from, const std::string &to) {
  int in = open(from.c_str(), O_RDONLY);
  if (in < 0) return false;
  int out = open(to.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (out < 0) { close(in); return false; }
  std::vector<char> buf(1 << 16);
  bool ok = true;
  for (;;) {
    ssize_t r = read(in, buf.data(), buf.size());
    if (r == 0) break;
    if (r < 0) { ok = false; break; }
    ssize_t off = 0;
    while (off < r) {
      ssize_t w = write(out, buf.data() + off, r - off);
      if (w <= 0) { ok = false; break; }
      off += w;
    }
    if (!ok) break;
  }
  fsync(out);
  close(in);
  close(out);
  if (!ok) unlink(to.c_str());
  return ok;
}

// Makes a name safe for the FAT file system on the reader.
inline std::string safeName(const std::string &name) {
  std::string out;
  for (char c : name) {
    if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' ||
        c == '>' || c == '|' || (static_cast<unsigned char>(c) < 32))
      out += '_';
    else
      out += c;
  }
  while (!out.empty() && (out.back() == '.' || out.back() == ' ')) out.pop_back();
  if (out.empty()) out = "file";
  return out;
}

inline std::string extOf(const std::string &name) {
  size_t dot = name.rfind('.');
  if (dot == std::string::npos || dot + 1 >= name.size()) return "";
  std::string e = name.substr(dot + 1);
  for (auto &c : e) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
  return e;
}

inline bool isBookExt(const std::string &ext) {
  static const char *exts[] = {"epub", "pdf", "fb2", "djvu", "djv", "mobi", "azw", "azw3", "txt", "rtf",
                               "doc", "docx", "chm", "html", "htm", "cbz", "cbr", "acsm", "prc", "fb2.zip"};
  for (auto e : exts) if (ext == e) return true;
  return false;
}

inline bool isImageExt(const std::string &ext) {
  return ext == "jpg" || ext == "jpeg" || ext == "png" || ext == "gif" || ext == "bmp" || ext == "tif" || ext == "tiff";
}

// Picks a free path in dir for name, adding " (2)", " (3)"… when taken.
// A file of exactly the same size is treated as an earlier copy of this download.
inline std::string targetPath(const std::string &dir, const std::string &name, int64_t size, bool *alreadyThere) {
  std::string base = safeName(name), ext;
  size_t dot = base.rfind('.');
  if (dot != std::string::npos && dot > 0) { ext = base.substr(dot); base = base.substr(0, dot); }
  *alreadyThere = false;
  for (int n = 1; n < 1000; n++) {
    std::string p = dir + "/" + base + (n == 1 ? "" : " (" + std::to_string(n) + ")") + ext;
    int64_t s = sizeOf(p);
    if (s < 0) return p;
    if (s == size) { *alreadyThere = true; return p; }
  }
  return dir + "/" + base + "_" + std::to_string(time(nullptr)) + ext;
}

inline std::string readFile(const std::string &p) {
  FILE *f = fopen(p.c_str(), "rb");
  if (!f) return "";
  std::string s;
  char buf[4096];
  size_t r;
  while ((r = fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, r);
  fclose(f);
  return s;
}

inline std::string trim(const std::string &s) {
  size_t a = 0, b = s.size();
  while (a < b && isspace(static_cast<unsigned char>(s[a]))) a++;
  while (b > a && isspace(static_cast<unsigned char>(s[b - 1]))) b--;
  return s.substr(a, b - a);
}

}  // namespace util
