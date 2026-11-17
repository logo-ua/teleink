// Registers Teleink's icon with the PocketBook launcher.
//
// Firmware 6.x lists apps from system/config/desktop/view.json. User apps ("U_" keys) may name
// an icon there (format documented in the file's own comments). We add our entry once, validate
// the result as JSON, keep a backup and replace the file atomically; on any doubt we leave it alone.
#pragma once

#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>

#include "util.h"

namespace launcher {

// Minimal strict JSON syntax check (no values are built).
class JsonCheck {
 public:
  explicit JsonCheck(const std::string &s) : s_(s) {}
  bool ok() {
    ws();
    if (!value(0)) return false;
    ws();
    return i_ == s_.size();
  }

 private:
  const std::string &s_;
  size_t i_ = 0;

  void ws() {
    while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\n' || s_[i_] == '\r')) i_++;
  }
  bool lit(const char *w) {
    size_t n = strlen(w);
    if (s_.compare(i_, n, w) != 0) return false;
    i_ += n;
    return true;
  }
  bool str() {
    if (i_ >= s_.size() || s_[i_] != '"') return false;
    i_++;
    while (i_ < s_.size()) {
      char c = s_[i_++];
      if (c == '"') return true;
      if (c == '\\') {
        if (i_ >= s_.size()) return false;
        char e = s_[i_++];
        if (e == 'u') {
          for (int k = 0; k < 4; k++, i_++)
            if (i_ >= s_.size() || !isxdigit(static_cast<unsigned char>(s_[i_]))) return false;
        } else if (!strchr("\"\\/bfnrt", e)) {
          return false;
        }
      } else if (static_cast<unsigned char>(c) < 0x20) {
        return false;
      }
    }
    return false;
  }
  bool num() {
    size_t st = i_;
    if (i_ < s_.size() && s_[i_] == '-') i_++;
    while (i_ < s_.size() && (isdigit(static_cast<unsigned char>(s_[i_])) || strchr(".eE+-", s_[i_]))) i_++;
    return i_ > st;
  }
  bool value(int depth) {
    if (depth > 64 || i_ >= s_.size()) return false;
    char c = s_[i_];
    if (c == '{') {
      i_++;
      ws();
      if (i_ < s_.size() && s_[i_] == '}') { i_++; return true; }
      for (;;) {
        ws();
        if (!str()) return false;
        ws();
        if (i_ >= s_.size() || s_[i_++] != ':') return false;
        ws();
        if (!value(depth + 1)) return false;
        ws();
        if (i_ >= s_.size()) return false;
        if (s_[i_] == ',') { i_++; continue; }
        if (s_[i_] == '}') { i_++; return true; }
        return false;
      }
    }
    if (c == '[') {
      i_++;
      ws();
      if (i_ < s_.size() && s_[i_] == ']') { i_++; return true; }
      for (;;) {
        ws();
        if (!value(depth + 1)) return false;
        ws();
        if (i_ >= s_.size()) return false;
        if (s_[i_] == ',') { i_++; continue; }
        if (s_[i_] == ']') { i_++; return true; }
        return false;
      }
    }
    if (c == '"') return str();
    if (c == 't') return lit("true");
    if (c == 'f') return lit("false");
    if (c == 'n') return lit("null");
    return num();
  }
};

// End (index past the closing brace) of the JSON object starting at json[open] == '{'.
inline size_t objectEnd(const std::string &json, size_t open) {
  int depth = 0;
  bool inStr = false;
  for (size_t k = open; k < json.size(); k++) {
    char c = json[k];
    if (inStr) {
      if (c == '\\') k++;
      else if (c == '"') inStr = false;
    } else if (c == '"') {
      inStr = true;
    } else if (c == '{') {
      depth++;
    } else if (c == '}') {
      if (--depth == 0) return k + 1;
    }
  }
  return std::string::npos;
}

inline std::string entryBody(const std::string &appPath, const std::string &title, const std::string &icon) {
  return "{\n"
         "            \"path\": \"" + appPath + "\",\n"
         "            \"title\": \"" + title + "\",\n"
         "            \"icon\": { \"path\": \"" + icon + "\" },\n"
         "            \"focused_icon\": { \"path\": \"" + icon + "\" }\n"
         "        }";
}

// Returns the patched text, or "" when nothing needs to change or the file looks unfamiliar.
inline std::string patch(const std::string &json, const std::string &key, const std::string &appPath,
                         const std::string &title, const std::string &icon) {
  std::string body = entryBody(appPath, title, icon);
  std::string out;
  size_t existing = json.find("\"" + key + "\"");
  if (existing != std::string::npos) {
    // already registered: refresh the entry if it differs (older builds used other paths)
    size_t open = json.find('{', existing);
    size_t colon = json.find(':', existing);
    if (open == std::string::npos || colon == std::string::npos || colon > open) return "";
    size_t close = objectEnd(json, open);
    if (close == std::string::npos) return "";
    if (json.compare(open, close - open, body) == 0) return "";
    out = json.substr(0, open) + body + json.substr(close);
  } else {
    // 1. entry in "applications": { ... }
    size_t a = json.find("\"applications\"");
    if (a == std::string::npos) return "";
    size_t brace = json.find('{', a);
    size_t colon = json.find(':', a);
    if (brace == std::string::npos || colon == std::string::npos || colon > brace) return "";
    for (size_t k = colon + 1; k < brace; k++)
      if (!isspace(static_cast<unsigned char>(json[k]))) return "";
    size_t next = brace + 1;
    while (next < json.size() && isspace(static_cast<unsigned char>(json[next]))) next++;
    bool emptyObject = next < json.size() && json[next] == '}';
    out = json.substr(0, brace + 1) + "\n        \"" + key + "\": " + body + (emptyObject ? "" : ",") +
          json.substr(brace + 1);
    // 2. listed in the first group ("@General" on stock firmware) so it shows with the icon
    size_t g = out.find("\"groups\"");
    if (g != std::string::npos) {
      size_t apps = out.find("\"apps\"", g);
      size_t bracket = apps == std::string::npos ? std::string::npos : out.find('[', apps);
      if (bracket != std::string::npos) {
        size_t n2 = bracket + 1;
        while (n2 < out.size() && isspace(static_cast<unsigned char>(out[n2]))) n2++;
        bool emptyArray = n2 < out.size() && out[n2] == ']';
        out = out.substr(0, bracket + 1) + "\n                    \"" + key + "\"" + (emptyArray ? "" : ",") +
              out.substr(bracket + 1);
      }
    }
  }
  if (!JsonCheck(out).ok()) return "";
  return out;
}

// Icon size matching the stock icons for this screen width.
inline int iconSize(int screenW) {
  if (screenW <= 800) return 64;
  if (screenW <= 1100) return 90;
  if (screenW <= 1300) return 107;
  return 118;
}

// macOS leaves "._name" metadata files when copying to the reader; the launcher shows
// "._Teleink.app" as a broken app, so remove ours.
inline void removeMacJunk(const std::string &root) {
  const char *junk[] = {"/applications/._Teleink.app", "/applications/._icons", "/applications/icons/._teleink_64.bmp",
                        "/applications/icons/._teleink_90.bmp", "/applications/icons/._teleink_107.bmp",
                        "/applications/icons/._teleink_118.bmp", "/applications/icons/._teleink.bmp",
                        "/applications/icons/._teleink_f.bmp"};
  for (auto j : junk) unlink((root + j).c_str());
}

// Returns true if the file was changed (the launcher picks it up after a restart).
inline bool registerIcon(const std::string &root, int screenW) {
  removeMacJunk(root);
  std::string path = root + "/system/config/desktop/view.json";
  std::string icon = "applications/icons/teleink_" + std::to_string(iconSize(screenW)) + ".bmp";
  if (!util::exists(path) || !util::exists(root + "/" + icon)) return false;
  std::string json = util::readFile(path);
  if (json.empty() || !JsonCheck(json).ok()) {
    util::log("view.json: missing or not valid JSON, not touching it");
    return false;
  }
  std::string out = patch(json, "U_Teleink", root + "/applications/Teleink.app", "Teleink", icon);
  if (out.empty()) return false;
  std::string backup = path + ".teleink-backup";
  if (!util::exists(backup) && !util::copyFile(path, backup)) return false;
  std::string tmp = path + ".teleink-tmp";
  FILE *f = fopen(tmp.c_str(), "wb");
  if (!f) return false;
  bool ok = fwrite(out.data(), 1, out.size(), f) == out.size();
  ok = (fflush(f) == 0) && ok;
  fsync(fileno(f));
  fclose(f);
  if (!ok || rename(tmp.c_str(), path.c_str()) != 0) {
    unlink(tmp.c_str());
    return false;
  }
  util::log("view.json: launcher entry updated (backup in %s)", backup.c_str());
  return true;
}

}  // namespace launcher
