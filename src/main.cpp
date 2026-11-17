// Teleink: an unofficial Telegram client for PocketBook e-readers (InkView + TDLib).
#include <inkview.h>

#include <dirent.h>
#include <algorithm>
#include <cmath>
#include <list>
#include <memory>

#include "launcher.h"
#include "logo.h"
#include "tg.h"
#include "util.h"

// ======================================================================
// globals
// ======================================================================

static Tg tg;

static const char *APP_DIR_REL = "/system/teleink";  // under FLASHDIR
static const char *APP_NAME = "Teleink";
static std::string gDataDir;
static std::string gDownloadDir;

static int W = 758, H = 1024, FS = 26;
static ifont *fReg, *fBold, *fSmall, *fSmallBold, *fBig, *fTiny, *fHuge;

enum class Scr { Login, Chats, Chat, Files, Found };
static Scr scr = Scr::Login;

struct Settings {
  int lang = -1;  // -1 until chosen on first run
} settings;

// chat list
static int listPage = 0;

// open chat
static int64_t bottomMsgId = 0;  // 0 = stick to the newest message
static int64_t topShownId = 0;
static int tallOffset = 0;   // scroll offset inside a message taller than the screen
static int tallMax = 0;
static bool tallShown = false;
static bool topPartial = false;  // the top message on screen is cut off
static int64_t replyTo = 0;
static std::set<int64_t> viewedIds;

// file picker
static std::string pickDir = "/mnt/ext1";
static int pickPage = 0;
struct PickEntry {
  std::string name;
  bool dir;
  int64_t size;
};
static std::vector<PickEntry> pickEntries;

// downloads the user asked for: file id -> message id / file name
struct Wanted {
  int64_t chatId;
  int64_t msgId;
  std::string name;
  bool openWhenDone;
};
static std::map<int32_t, Wanted> wanted;
static std::map<int32_t, std::string> exported;  // file id -> path in Downloads

// redraw control
static bool needDraw = true, needFull = true;
static int partialCount = 0;
static bool modalOpen = false;
static bool ignoreUp = false;
static int downX = -1, downY = -1;
static std::string toastText;
static long long toastUntil = 0;
static int32_t lastAuthPrompted = -1;
static bool exiting = false;

// keyboard buffer
static char kbuf[4096];

static long long nowMs() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<long long>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
}

static void quitApp();

static void invalidate(bool full = false) {
  needDraw = true;
  if (full) needFull = true;
}

// ======================================================================
// settings & credentials
// ======================================================================

static void saveSettings() {
  FILE *f = fopen((gDataDir + "/settings.txt").c_str(), "w");
  if (!f) return;
  fprintf(f, "lang=%d\n", settings.lang);
  fclose(f);
}

static void loadSettings() {
  std::string s = util::readFile(gDataDir + "/settings.txt");
  size_t p = s.find("lang=");
  if (p != std::string::npos) settings.lang = atoi(s.c_str() + p + 5);
  if (settings.lang < -1 || settings.lang > 2) settings.lang = -1;
  i18n::lang() = settings.lang < 0 ? i18n::EN : settings.lang;
}

static bool parseCreds(const std::string &text, int32_t &id, std::string &hash) {
  std::vector<std::string> parts;
  std::string cur;
  for (char c : text) {
    if (c == '\n' || c == '\r' || c == ' ' || c == '\t' || c == ';' || c == ',' || c == '=' || c == ':') {
      if (!cur.empty()) parts.push_back(cur);
      cur.clear();
    } else {
      cur += c;
    }
  }
  if (!cur.empty()) parts.push_back(cur);
  int32_t gotId = 0;
  std::string gotHash;
  for (auto &p : parts) {
    bool digits = !p.empty() && std::all_of(p.begin(), p.end(), [](char c) { return c >= '0' && c <= '9'; });
    bool hex = p.size() == 32 && std::all_of(p.begin(), p.end(), [](char c) { return isxdigit(static_cast<unsigned char>(c)); });
    if (digits && !gotId && p.size() < 11) gotId = static_cast<int32_t>(atol(p.c_str()));
    else if (hex) gotHash = p;
  }
  if (gotId > 0 && !gotHash.empty()) {
    id = gotId;
    hash = gotHash;
    return true;
  }
  return false;
}

static void saveCreds() {
  FILE *f = fopen((gDataDir + "/api.txt").c_str(), "w");
  if (!f) return;
  fprintf(f, "%d\n%s\n", tg.apiId, tg.apiHash.c_str());
  fclose(f);
}

static void loadCreds() {
  int32_t id;
  std::string hash;
  // a file dropped in the root of the reader over USB wins
  std::string user = util::readFile(std::string(FLASHDIR) + "/teleink-api.txt");
  if (!user.empty() && parseCreds(user, id, hash)) {
    tg.apiId = id;
    tg.apiHash = hash;
    saveCreds();
    return;
  }
  std::string stored = util::readFile(gDataDir + "/api.txt");
  if (!stored.empty() && parseCreds(stored, id, hash)) {
    tg.apiId = id;
    tg.apiHash = hash;
    return;
  }
#if defined(TG_API_ID) && defined(TG_API_HASH)
  // keys built into release binaries (passed by the build from a CI secret)
  tg.apiId = TG_API_ID;
  tg.apiHash = TG_API_HASH;
#endif
}

// ======================================================================
// drawing helpers
// ======================================================================

struct Hit {
  int x, y, w, h;
  int kind;
  int64_t a, b;
};
enum {
  H_BACK = 1, H_MENU, H_PREV, H_NEXT, H_CHAT, H_MSG, H_FILE, H_INPUT, H_REPLY_CANCEL,
  H_LOGIN1, H_PICK, H_ATTACH, H_SPONSORED, H_LANG, H_SEARCH, H_FOUND
};
static std::vector<Hit> hits;

static void addHit(int x, int y, int w, int h, int kind, int64_t a = 0, int64_t b = 0) {
  hits.push_back({x, y, w, h, kind, a, b});
}

static int lineH(ifont *f) { return f->height > 0 ? f->height : f->size * 6 / 5; }

static int textW(ifont *f, const std::string &s) {
  SetFont(f, BLACK);
  return StringWidth(s.c_str());
}

static int textH(ifont *f, int w, const std::string &s) {
  if (s.empty()) return 0;
  SetFont(f, BLACK);
  return TextRectHeight(w, s.c_str(), ALIGN_LEFT | VALIGN_TOP);
}

static void text(ifont *f, int color, int x, int y, int w, int h, const std::string &s, int flags = ALIGN_LEFT | VALIGN_TOP) {
  if (s.empty() || w <= 0 || h <= 0) return;
  SetFont(f, color);
  DrawTextRect(x, y, w, h, s.c_str(), flags);
}

static void line1(ifont *f, int color, int x, int y, int w, const std::string &s, int align = ALIGN_LEFT) {
  text(f, color, x, y, w, lineH(f), s, align | VALIGN_TOP | DOTS);
}

// ---------- shapes (the device font has no arrows/ticks, so they are drawn) ----------
static int radius() { return std::max(4, FS / 3); }

// Filled rectangle with rounded corners.
static void fillRound(int x, int y, int w, int h, int r, int color) {
  r = std::min(r, std::min(w, h) / 2);
  for (int j = 0; j < h; j++) {
    int inset = 0;
    int dy = j < r ? r - j : (j >= h - r ? j - (h - r - 1) : 0);
    if (dy > 0) inset = r - static_cast<int>(std::sqrt(static_cast<double>(r * r - (dy - 0.5) * (dy - 0.5))) + 0.5);
    if (w - 2 * inset > 0) FillArea(x + inset, y + j, w - 2 * inset, 1, color);
  }
}

// Rounded box: border of the given thickness around a fill.
static void roundBox(int x, int y, int w, int h, int fill, int border, int thick = 1) {
  int r = radius();
  if (thick > 0) {
    fillRound(x, y, w, h, r, border);
    fillRound(x + thick, y + thick, w - 2 * thick, h - 2 * thick, std::max(1, r - thick), fill);
  } else {
    fillRound(x, y, w, h, r, fill);
  }
}

static void thickLine(int x1, int y1, int x2, int y2, int t, int color) {
  for (int a = -t / 2; a <= (t - 1) / 2; a++)
    for (int b = -t / 2; b <= (t - 1) / 2; b++) DrawLine(x1 + a, y1 + b, x2 + a, y2 + b, color);
}

// Filled isosceles triangle pointing up/down/left/right, centred in the box.
enum Dir { UP, DOWN, LEFT, RIGHT };
static void triangle(int x, int y, int w, int h, Dir d, int color) {
  int s = std::min(w, h) * 2 / 5;
  int cx = x + w / 2, cy = y + h / 2;
  for (int k = 0; k <= s; k++) {
    int half = k * 6 / 10;  // slightly flat triangle
    if (d == UP) FillArea(cx - half, cy - s / 2 + k, 2 * half + 1, 1, color);
    if (d == DOWN) FillArea(cx - half, cy + s / 2 - k, 2 * half + 1, 1, color);
    if (d == LEFT) FillArea(cx - s / 2 + k, cy - half, 1, 2 * half + 1, color);
    if (d == RIGHT) FillArea(cx + s / 2 - k, cy - half, 1, 2 * half + 1, color);
  }
}

static void chevronLeft(int cx, int cy, int size, int color) {
  int t = std::max(2, FS / 10);
  thickLine(cx + size / 4, cy - size / 2, cx - size / 4, cy, t, color);
  thickLine(cx - size / 4, cy, cx + size / 4, cy + size / 2, t, color);
}

// Read receipt: one or two small ticks ending at (x, baseline y).
static int ticksWidth(bool two) { return (two ? FS * 7 / 10 : FS / 2); }
static void ticks(int x, int y, bool two, int color) {
  int s = FS / 3, t = std::max(1, FS / 14);
  auto tick = [&](int ox) {
    thickLine(ox, y - s / 2, ox + s / 3, y, t, color);
    thickLine(ox + s / 3, y, ox + s, y - s, t, color);
  };
  tick(x);
  if (two) tick(x + FS / 4);
}

// Thin scroll indicator on the right edge: start and size are fractions of the whole.
// Scrollbar geometry from the last draw, used to drag it with a finger.
struct ScrollState {
  bool visible = false;
  int y = 0, h = 0, thumbH = 0;
  double size = 1;
  bool candidate = false;  // finger went down on the scrollbar strip
  bool dragging = false;
  double frac = 0;  // 0 = top, 1 = bottom
} sb;

static int sbWidth() { return std::max(12, FS * 55 / 100); }
// Right margin for list and chat content so it never runs under the scrollbar.
static int rightPad() { return sbWidth() + FS / 2; }
static int sbX() { return W - sbWidth() - 3; }

static void drawThumb(double frac, int w, bool active = false) {
  int ty = sb.y + static_cast<int>((sb.h - sb.thumbH) * frac);
  int x = W - w - 3, r = w / 2;
  // white pill with an outline; the outline gets heavier while it is dragged
  int t = active ? 3 : 1;
  fillRound(x, ty, w, sb.thumbH, r, active ? BLACK : DGRAY);
  fillRound(x + t, ty + t, w - 2 * t, sb.thumbH - 2 * t, std::max(1, r - t), WHITE);
}

static void scrollbar(int y, int h, double start, double size) {
  if (size >= 1.0 || h <= 0) return;
  sb.visible = true;
  sb.y = y;
  sb.h = h;
  sb.size = size;
  sb.thumbH = std::max(FS * 2, static_cast<int>(h * size));
  double frac = std::min(1.0, std::max(0.0, start / std::max(1e-9, 1.0 - size)));
  drawThumb(frac, sbWidth());
}

// While dragging only the strip on the right is redrawn, with the fast e-ink waveform.
static void dragScrollbar(int y) {
  double frac = (y - sb.y - sb.thumbH / 2.0) / std::max(1, sb.h - sb.thumbH);
  sb.frac = std::min(1.0, std::max(0.0, frac));
  int stripW = sbWidth() + 6;
  FillArea(W - stripW, sb.y, stripW, sb.h, WHITE);
  drawThumb(sb.frac, sbWidth(), true);
  DynamicUpdate(W - stripW, sb.y, stripW, sb.h);
}

static void scrollTo(double frac);

static void button(int x, int y, int w, int h, const std::string &label, bool primary) {
  if (primary) {
    roundBox(x, y, w, h, BLACK, BLACK, 0);
    text(fBold, WHITE, x, y, w, h, label, ALIGN_CENTER | VALIGN_MIDDLE | DOTS);
  } else {
    roundBox(x, y, w, h, WHITE, BLACK, 2);
    text(fBold, BLACK, x, y, w, h, label, ALIGN_CENTER | VALIGN_MIDDLE | DOTS);
  }
}

static const std::string MENU_ICON = "\x01";

static int headerH() { return FS * 2 + FS / 3; }
static int footerH() { return FS * 2; }

// Header with optional back button on the left and an action on the right.
static void drawHeader(const std::string &title, bool back, const std::string &right, int rightKind = H_MENU,
                       bool searchIcon = false) {
  int hh = headerH();
  FillArea(0, 0, W, hh, WHITE);
  int x = FS / 2;
  if (back) {
    chevronLeft(FS, hh / 2, FS, BLACK);
    addHit(0, 0, FS * 3, hh, H_BACK);
    x = FS * 2;
  }
  int rw = 0;
  if (right == MENU_ICON) {
    // three bars instead of a word
    rw = FS * 2;
    int bw = FS, bx = W - FS / 2 - bw - FS / 4, th = std::max(2, FS / 9);
    for (int i = -1; i <= 1; i++) FillArea(bx, hh / 2 + i * FS / 3 - th / 2, bw, th, BLACK);
    addHit(W - rw - FS / 2, 0, rw + FS / 2, hh, rightKind);
    if (searchIcon) {
      // magnifier: ring plus handle
      int d = FS * 3 / 4, t = std::max(2, FS / 9);
      int cx = W - rw - FS / 2 - FS, cy = hh / 2 - FS / 8;
      fillRound(cx - d / 2, cy - d / 2, d, d, d / 2, BLACK);
      fillRound(cx - d / 2 + t, cy - d / 2 + t, d - 2 * t, d - 2 * t, d / 2 - t, WHITE);
      thickLine(cx + d * 3 / 10, cy + d * 3 / 10, cx + d * 3 / 5, cy + d * 3 / 5, t, BLACK);
      addHit(cx - FS, 0, FS * 2, hh, H_SEARCH);
      rw += FS * 2;
    }
  } else if (!right.empty()) {
    rw = textW(fBig, right) + FS;
    text(fBig, BLACK, W - rw, 0, rw - FS / 2, hh, right, ALIGN_RIGHT | VALIGN_MIDDLE);
    addHit(W - rw - FS / 2, 0, rw + FS / 2, hh, rightKind);
  }
  std::string status = tg.ready ? tg.connection : "";
  int tw = W - x - rw - FS / 2;
  if (status.empty()) {
    text(fBig, BLACK, x, 0, tw, hh, title, ALIGN_LEFT | VALIGN_MIDDLE | DOTS);
  } else {
    int th = lineH(fBig), sh = lineH(fTiny);
    int y0 = (hh - th - sh) / 2;
    line1(fBig, BLACK, x, y0, tw, title);
    line1(fTiny, DGRAY, x, y0 + th, tw, status);
  }
  FillArea(0, hh - 2, W, 2, BLACK);
}

static void drawPager(int page, int pages) {
  int fh = footerH(), y = H - fh;
  FillArea(0, y, W, fh, WHITE);
  FillArea(0, y, W, 1, BLACK);
  if (page > 0) {
    triangle(0, y, W / 3, fh, LEFT, BLACK);
    addHit(0, y, W / 3, fh, H_PREV);
  }
  char b[64];
  snprintf(b, sizeof b, "%d / %d", page + 1, std::max(pages, 1));
  text(fSmall, DGRAY, W / 3, y, W / 3, fh, b, ALIGN_CENTER | VALIGN_MIDDLE);
  if (page + 1 < pages) {
    triangle(W * 2 / 3, y, W / 3, fh, RIGHT, BLACK);
    addHit(W * 2 / 3, y, W / 3, fh, H_NEXT);
  }
}

static void drawToast() {
  if (toastText.empty() || nowMs() > toastUntil) return;
  int pad = FS / 2;
  int h = textH(fSmall, W - 4 * pad, toastText) + 2 * pad;
  h = std::min(h, H / 4);
  int y = headerH() + pad;
  roundBox(pad, y, W - 2 * pad, h, BLACK, BLACK, 0);
  text(fSmall, WHITE, 2 * pad, y + pad, W - 4 * pad, h - 2 * pad, toastText, ALIGN_LEFT | VALIGN_TOP | DOTS);
}

static void toast(const std::string &s, int ms = 4000) {
  toastText = s;
  toastUntil = nowMs() + ms;
  invalidate();
}

// ======================================================================
// message content
// ======================================================================

struct Media {
  int kind = 0;  // 0 none, 1 photo, 2 file
  const td_api::file *file = nullptr;  // as embedded in the message
  std::string title, sub, name, label;
  int64_t size = 0;
};

static std::string formatted(const object_ptr<td_api::formattedText> &t) {
  return t ? t->text_ : std::string();
}

static const td_api::photoSize *largestPhoto(const td_api::photo &p) {
  const td_api::photoSize *best = nullptr;
  for (auto &s : p.sizes_)
    if (s && s->photo_ && (!best || s->width_ * s->height_ > best->width_ * best->height_)) best = s.get();
  return best;
}

static void fileMedia(Media &m, const td_api::file *f, const std::string &title, const std::string &name, const std::string &extra) {
  m.kind = 2;
  m.file = f;
  m.title = title;
  m.name = name;
  m.size = f ? (f->size_ ? f->size_ : f->expected_size_) : 0;
  m.sub = extra;
  std::string ext = util::extOf(name);
  m.label = ext.empty() ? "FILE" : ext.substr(0, 4);
  for (auto &c : m.label) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
}

// Short text describing the content, used in chat list previews and replies.
static std::string contentText(const td_api::MessageContent *c, Media *media, bool *service, int photoWidth) {
  if (service) *service = false;
  if (!c) return "";
  switch (c->get_id()) {
    case td_api::messageText::ID:
      return formatted(static_cast<const td_api::messageText *>(c)->text_);
    case td_api::messagePhoto::ID: {
      auto p = static_cast<const td_api::messagePhoto *>(c);
      if (media && p->photo_) {
        // photos are handled like any other file: download, then open in the reader's gallery
        if (auto s = largestPhoto(*p->photo_)) {
          fileMedia(*media, s->photo_.get(), tr(S_PHOTO), "photo_" + std::to_string(s->photo_->id_) + ".jpg", "");
          media->label = "JPG";
        }
      }
      std::string cap = formatted(p->caption_);
      return media ? cap : (cap.empty() ? std::string(tr(S_PHOTO)) : std::string(tr(S_PHOTO)) + ", " + cap);
    }
    case td_api::messageDocument::ID: {
      auto d = static_cast<const td_api::messageDocument *>(c);
      std::string name = d->document_ ? d->document_->file_name_ : "file";
      if (media && d->document_) fileMedia(*media, d->document_->document_.get(), name, name, "");
      std::string cap = formatted(d->caption_);
      return media ? cap : std::string(tr(S_FILE)) + ": " + name + (cap.empty() ? "" : ", " + cap);
    }
    case td_api::messageVideo::ID: {
      auto v = static_cast<const td_api::messageVideo *>(c);
      std::string cap = formatted(v->caption_);
      if (media && v->video_) {
        std::string name = v->video_->file_name_.empty() ? "video.mp4" : v->video_->file_name_;
        fileMedia(*media, v->video_->video_.get(), std::string(tr(S_VIDEO)) + " " + util::duration(v->video_->duration_), name, "");
        media->label = "VID";
      }
      return media ? cap : (cap.empty() ? std::string(tr(S_VIDEO)) : std::string(tr(S_VIDEO)) + ", " + cap);
    }
    case td_api::messageAnimation::ID: {
      auto a = static_cast<const td_api::messageAnimation *>(c);
      if (media && a->animation_) {
        std::string name = a->animation_->file_name_.empty() ? "animation.mp4" : a->animation_->file_name_;
        fileMedia(*media, a->animation_->animation_.get(), "GIF", name, "");
        media->label = "GIF";
      }
      std::string cap = formatted(a->caption_);
      return media ? cap : (cap.empty() ? "GIF" : "GIF, " + cap);
    }
    case td_api::messageAudio::ID: {
      auto a = static_cast<const td_api::messageAudio *>(c);
      std::string t;
      if (a->audio_) {
        t = a->audio_->performer_;
        if (!a->audio_->title_.empty()) t += (t.empty() ? "" : " — ") + a->audio_->title_;
        if (t.empty()) t = a->audio_->file_name_;
        if (media) {
          std::string name = a->audio_->file_name_.empty() ? "audio.mp3" : a->audio_->file_name_;
          fileMedia(*media, a->audio_->audio_.get(), t + " (" + util::duration(a->audio_->duration_) + ")", name, "");
          media->label = "MP3";
        }
      }
      std::string cap = formatted(a->caption_);
      return media ? cap : std::string(tr(S_AUDIO)) + ": " + t;
    }
    case td_api::messageVoiceNote::ID: {
      auto v = static_cast<const td_api::messageVoiceNote *>(c);
      int d = v->voice_note_ ? v->voice_note_->duration_ : 0;
      if (media && v->voice_note_)
        fileMedia(*media, v->voice_note_->voice_.get(), std::string(tr(S_VOICE)) + " " + util::duration(d), "voice.ogg", "");
      if (media) media->label = "OGG";
      std::string cap = formatted(v->caption_);
      return media ? cap : std::string(tr(S_VOICE)) + " " + util::duration(d);
    }
    case td_api::messageVideoNote::ID: {
      auto v = static_cast<const td_api::messageVideoNote *>(c);
      int d = v->video_note_ ? v->video_note_->duration_ : 0;
      if (media && v->video_note_) {
        fileMedia(*media, v->video_note_->video_.get(), std::string(tr(S_VIDEO_NOTE)) + " " + util::duration(d), "video_note.mp4", "");
        media->label = "VID";
      }
      return media ? std::string() : std::string(tr(S_VIDEO_NOTE));
    }
    case td_api::messageSticker::ID: {
      auto s = static_cast<const td_api::messageSticker *>(c);
      return "[" + std::string(tr(S_STICKER)) + (s->sticker_ ? " " + s->sticker_->emoji_ : std::string()) + "]";
    }
    case td_api::messageLocation::ID: {
      auto l = static_cast<const td_api::messageLocation *>(c);
      char b[96];
      if (l->location_) snprintf(b, sizeof b, "%s: %.5f, %.5f", tr(S_LOCATION), l->location_->latitude_, l->location_->longitude_);
      else snprintf(b, sizeof b, "%s", tr(S_LOCATION));
      return b;
    }
    case td_api::messageVenue::ID: return tr(S_LOCATION);
    case td_api::messageContact::ID: {
      auto ct = static_cast<const td_api::messageContact *>(c);
      if (!ct->contact_) return tr(S_CONTACT);
      return std::string(tr(S_CONTACT)) + ": " + ct->contact_->first_name_ + " " + ct->contact_->last_name_ + " " + ct->contact_->phone_number_;
    }
    case td_api::messagePoll::ID: {
      auto p = static_cast<const td_api::messagePoll *>(c);
      if (!p->poll_) return tr(S_POLL);
      std::string s = std::string(tr(S_POLL)) + ": " + formatted(p->poll_->question_);
      if (media) {
        for (auto &o : p->poll_->options_) {
          if (!o) continue;
          s += "\n• " + formatted(o->text_);
          if (p->poll_->total_voter_count_ > 0 || o->is_chosen_) s += " — " + std::to_string(o->vote_percentage_) + "%";
        }
        s += "\n" + std::string(tr(S_VOTES)) + ": " + std::to_string(p->poll_->total_voter_count_);
      }
      return s;
    }
    case td_api::messageDice::ID: {
      auto d = static_cast<const td_api::messageDice *>(c);
      return std::string(tr(S_DICE)) + ": " + std::to_string(d->value_);
    }
    case td_api::messageCall::ID: {
      auto call = static_cast<const td_api::messageCall *>(c);
      return std::string(tr(call->is_video_ ? S_VIDEO_CALL : S_CALL)) +
             (call->duration_ > 0 ? " " + util::duration(call->duration_) : "");
    }
    case td_api::messageStory::ID: return tr(S_STORY);
    case td_api::messageGame::ID: return tr(S_GAME);
    case td_api::messageUnsupported::ID: return tr(S_UNSUPPORTED);
    default: break;
  }
  if (service) *service = true;
  switch (c->get_id()) {
    case td_api::messageChatAddMembers::ID: return tr(S_SVC_ADDED);
    case td_api::messageChatJoinByLink::ID:
    case td_api::messageChatJoinByRequest::ID: return tr(S_SVC_JOINED);
    case td_api::messageChatDeleteMember::ID: return tr(S_SVC_LEFT);
    case td_api::messageChatChangeTitle::ID:
      return std::string(tr(S_SVC_TITLE)) + ": " + static_cast<const td_api::messageChatChangeTitle *>(c)->title_;
    case td_api::messagePinMessage::ID: return tr(S_SVC_PINNED);
    case td_api::messageBasicGroupChatCreate::ID:
    case td_api::messageSupergroupChatCreate::ID: return tr(S_SVC_CREATED);
    case td_api::messageContactRegistered::ID: return tr(S_SVC_REGISTERED);
    case td_api::messageChatChangePhoto::ID: return tr(S_SVC_PHOTO);
    default: return tr(S_SVC_OTHER);
  }
}

static std::string previewOf(const td_api::message *m) {
  if (!m) return "";
  bool service = false;
  std::string s = contentText(m->content_.get(), nullptr, &service, 0);
  return util::clean(s, true);
}

// ======================================================================
// LOGIN SCREEN
// ======================================================================

static void askApiId();
static void openKeyboardFor(int32_t state);

static void centeredBlock(int &y, ifont *f, int color, const std::string &t) {
  if (t.empty()) return;
  int w = W - 4 * FS;
  int h = textH(f, w, t);
  text(f, color, 2 * FS, y, w, h + 4, t, ALIGN_CENTER | VALIGN_TOP);
  y += h + FS;
}

// The paper-plane logo, scaled once to the requested width.
static void drawLogo(int &y, int size) {
  static ibitmap *scaled = nullptr;
  static int scaledSize = 0;
  if (!scaled || scaledSize != size) {
    auto *src = static_cast<ibitmap *>(malloc(sizeof(ibitmap) + LOGO_W * LOGO_H));
    if (!src) return;
    src->width = LOGO_W;
    src->height = LOGO_H;
    src->depth = 8;
    src->scanline = LOGO_W;
    memcpy(src->data, LOGO_DATA, LOGO_W * LOGO_H);
    if (scaled) free(scaled);
    scaled = BitmapStretchProportionally(src, size, size);
    if (scaled) free(src);
    else scaled = src;
    scaledSize = size;
  }
  DrawBitmap((W - scaled->width) / 2, y, scaled);
  y += scaled->height + FS / 2;
}

// First run: pick the interface language.
static void drawLanguagePicker() {
  ClearScreen();
  hits.clear();
  int y = H / 6;
  drawLogo(y, W / 4);
  centeredBlock(y, fHuge, BLACK, APP_NAME);
  y += FS * 2;
  int bw = W - 4 * FS, bh = FS * 2 + FS / 2, x = 2 * FS;
  static const char *names[3] = {"English", "Українська", "Русский"};
  static const int langs[3] = {i18n::EN, i18n::UK, i18n::RU};
  for (int i = 0; i < 3; i++) {
    button(x, y, bw, bh, names[i], i == 0);
    addHit(x, y, bw, bh, H_LANG, langs[i]);
    y += bh + FS;
  }
}

static void drawLogin() {
  if (settings.lang < 0) {
    drawLanguagePicker();
    return;
  }
  ClearScreen();
  hits.clear();
  std::string title, body, primary;
  bool haveCreds = tg.apiId && !tg.apiHash.empty();
  switch (tg.authId) {
    case td_api::authorizationStateWaitPhoneNumber::ID:
      title = tr(S_LOGIN);
      primary = tr(S_PHONE);
      break;
    case td_api::authorizationStateWaitCode::ID:
      title = tg.codeInfo.empty() ? tr(S_CODE) : tg.codeInfo;
      primary = tr(S_CODE);
      break;
    case td_api::authorizationStateWaitPassword::ID:
      title = tr(S_PASSWORD);
      if (!tg.passwordHint.empty()) body = std::string(tr(S_HINT)) + ": " + tg.passwordHint;
      primary = tr(S_PASSWORD);
      break;
    case td_api::authorizationStateWaitEmailAddress::ID:
      title = tr(S_EMAIL);
      primary = tr(S_EMAIL);
      break;
    case td_api::authorizationStateWaitEmailCode::ID:
      title = tr(S_EMAIL);
      primary = tr(S_CODE);
      break;
    case td_api::authorizationStateWaitRegistration::ID:
      title = tr(S_NO_ACCOUNT);
      primary = tr(S_OTHER_NUMBER);
      break;
    case td_api::authorizationStateWaitOtherDeviceConfirmation::ID:
      title = tr(S_CONFIRM_OTHER);
      primary = tr(S_OTHER_NUMBER);
      break;
    case td_api::authorizationStateLoggingOut::ID:
    case td_api::authorizationStateClosing::ID:
    case td_api::authorizationStateClosed::ID:
      title = tr(S_WAIT);
      break;
    default:
      if (!haveCreds) {
        title = tr(S_API_TITLE);
        body = tr(S_API_BODY);
        primary = "api_id · api_hash";
      } else {
        title = tr(S_STARTING);
      }
  }
  if (tg.lastError.find("api_") != std::string::npos) primary = "api_id · api_hash";

  int y = H / 8;
  drawLogo(y, W / 4);
  centeredBlock(y, fHuge, BLACK, APP_NAME);
  y += FS;
  centeredBlock(y, fBold, BLACK, title);
  centeredBlock(y, fSmall, DGRAY, body);
  centeredBlock(y, fSmallBold, BLACK, tg.lastError);
  if (!primary.empty()) {
    int bw = W - 4 * FS, bh = FS * 2 + FS / 2;
    y = std::max(y + FS, H / 2);
    button(2 * FS, y, bw, bh, primary, true);
    addHit(2 * FS, y, bw, bh, H_LOGIN1);
  }
}

static void onApiHash(char *t) {
  modalOpen = false;
  if (!t) { invalidate(true); return; }
  std::string h = util::trim(t);
  bool ok = h.size() == 32 && std::all_of(h.begin(), h.end(), [](char c) { return isxdigit(static_cast<unsigned char>(c)); });
  if (!ok) {
    tg.lastError = tr(S_API_BAD_HASH);
    invalidate(true);
    return;
  }
  for (auto &c : h) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
  tg.apiHash = h;
  tg.lastError.clear();
  saveCreds();
  if (tg.waitingForParams) {
    tg.sendParameters();
  } else if (tg.authId != 0) {
    // TDLib already started with the old keys; they are applied on the next start
    modalOpen = true;
    Dialog(ICON_INFORMATION, APP_NAME, tr(S_API_RESTART), "OK", nullptr, [](int) { quitApp(); });
    return;
  }
  invalidate(true);
}

static void onApiId(char *t) {
  modalOpen = false;
  if (!t) { invalidate(true); return; }
  long v = atol(util::trim(t).c_str());
  if (v <= 0) {
    tg.lastError = tr(S_API_BAD_ID);
    invalidate(true);
    return;
  }
  tg.apiId = static_cast<int32_t>(v);
  snprintf(kbuf, sizeof kbuf, "%s", tg.apiHash.c_str());
  modalOpen = true;
  OpenKeyboard("api_hash", kbuf, 64, KBD_ENTEXT, onApiHash);
}

static void askApiId() {
  if (tg.apiId) snprintf(kbuf, sizeof kbuf, "%d", tg.apiId);
  else kbuf[0] = 0;
  modalOpen = true;
  OpenKeyboard("api_id", kbuf, 16, KBD_NUMERIC, onApiId);
}

static void onAuthInput(char *t) {
  modalOpen = false;
  if (!t) { invalidate(true); return; }
  std::string s = util::trim(t);
  if (s.empty()) { invalidate(true); return; }
  tg.lastError.clear();
  switch (tg.authId) {
    case td_api::authorizationStateWaitPhoneNumber::ID: {
      std::string phone;
      for (char c : s) if ((c >= '0' && c <= '9') || c == '+') phone += c;
      if (!phone.empty() && phone[0] != '+') phone = "+" + phone;
      tg.sendPhone(phone);
      break;
    }
    case td_api::authorizationStateWaitCode::ID: tg.sendCode(s); break;
    case td_api::authorizationStateWaitPassword::ID: tg.sendPassword(t); break;
    case td_api::authorizationStateWaitEmailAddress::ID: tg.sendEmail(s); break;
    case td_api::authorizationStateWaitEmailCode::ID: tg.sendEmailCode(s); break;
  }
  ShowHourglass();
  invalidate(true);
}

static void openKeyboardFor(int32_t state) {
  kbuf[0] = 0;
  const char *title = nullptr;
  int flags = KBD_NORMAL;
  switch (state) {
    case td_api::authorizationStateWaitPhoneNumber::ID: title = tr(S_PHONE); flags = KBD_PHONE; snprintf(kbuf, sizeof kbuf, "+"); break;
    case td_api::authorizationStateWaitCode::ID: title = tr(S_CODE); flags = KBD_NUMERIC; break;
    case td_api::authorizationStateWaitPassword::ID: title = tr(S_PASSWORD); flags = KBD_NORMAL | KBD_PASSWORD; break;
    case td_api::authorizationStateWaitEmailAddress::ID: title = tr(S_EMAIL); flags = KBD_ENTEXT; break;
    case td_api::authorizationStateWaitEmailCode::ID: title = tr(S_CODE); flags = KBD_NUMERIC; break;
    default: return;
  }
  modalOpen = true;
  OpenKeyboard(title, kbuf, 256, flags, onAuthInput);
}

static void loginPrimary() {
  if (tg.lastError.find("api_") != std::string::npos) {
    askApiId();
    return;
  }
  switch (tg.authId) {
    case td_api::authorizationStateWaitRegistration::ID:
    case td_api::authorizationStateWaitOtherDeviceConfirmation::ID:
      tg.logOut();
      break;
    case td_api::authorizationStateWaitPhoneNumber::ID:
    case td_api::authorizationStateWaitCode::ID:
    case td_api::authorizationStateWaitPassword::ID:
    case td_api::authorizationStateWaitEmailAddress::ID:
    case td_api::authorizationStateWaitEmailCode::ID:
      openKeyboardFor(tg.authId);
      break;
    default:
      if (!tg.apiId || tg.apiHash.empty()) askApiId();
  }
}

static void chooseLanguage(int l) {
  settings.lang = l;
  i18n::lang() = l;
  saveSettings();
  tg.connection = tr(S_CONNECTING);
  lastAuthPrompted = -1;  // now the phone keyboard may open
  invalidate(true);
}

// ======================================================================
// CHAT LIST SCREEN
// ======================================================================

static int rowH() { return lineH(fBold) + lineH(fSmall) + FS * 2 / 3 + FS / 3; }
static int rowsPerPage() { return std::max(1, (H - headerH() - footerH()) / rowH()); }

static bool searchMode = false;
static std::string searchQuery;
static std::vector<int64_t> searchResults;

static std::vector<int64_t> currentChatIds() { return searchMode ? searchResults : tg.chatIds("main"); }

static void addResult(int64_t id, bool front) {
  if (std::find(searchResults.begin(), searchResults.end(), id) != searchResults.end()) return;
  if (front) searchResults.insert(searchResults.begin(), id);
  else searchResults.push_back(id);
}

static void onSearch(char *t) {
  modalOpen = false;
  std::string q = t ? util::trim(t) : "";
  if (q.empty()) { invalidate(true); return; }
  searchQuery = q;
  searchMode = true;
  searchResults.clear();
  listPage = 0;
  auto chats = [](object_ptr<td_api::Object> o) {
    if (o->get_id() == td_api::chats::ID)
      for (auto id : static_cast<td_api::chats &>(*o).chat_ids_) addResult(id, false);
    invalidate();
  };
  if (q[0] == '@') {
    tg.send(make_object<td_api::searchPublicChat>(q.substr(1)), [](object_ptr<td_api::Object> o) {
      if (o->get_id() == td_api::chat::ID) addResult(static_cast<td_api::chat &>(*o).id_, true);
      invalidate();
    });
  } else {
    auto local = make_object<td_api::searchChats>();
    local->query_ = q;
    local->limit_ = 50;
    tg.send(std::move(local), chats);
    auto server = make_object<td_api::searchChatsOnServer>();
    server->query_ = q;
    server->limit_ = 50;
    tg.send(std::move(server), chats);
    // public channels and groups you have not joined yet
    auto pub = make_object<td_api::searchPublicChats>();
    pub->query_ = q;
    tg.send(std::move(pub), chats);
  }
  invalidate(true);
}

static void startSearch() {
  snprintf(kbuf, sizeof kbuf, "%s", searchMode ? searchQuery.c_str() : "");
  modalOpen = true;
  OpenKeyboard(tr(S_SEARCH), kbuf, 128, KBD_NORMAL, onSearch);
}

static void endSearch() {
  searchMode = false;
  searchResults.clear();
  listPage = 0;
  invalidate(true);
}

static void drawChatRow(const td_api::chat &c, int y) {
  int pad = FS / 2, rh = rowH();
  int x = pad, w = W - pad - rightPad();
  std::string date = c.last_message_ ? util::shortDate(c.last_message_->date_) : "";
  int dw = date.empty() ? 0 : textW(fSmall, date) + pad;
  int y1 = y + FS / 3;
  std::string title = util::clean(c.title_, true);
  if (c.id_ == tg.myId && tg.myId) title = tr(S_SAVED);
  if (tg.isChannel(c)) title = "» " + title;
  line1(fBold, BLACK, x, y1, w - dw, title);
  if (!date.empty()) line1(fSmall, DGRAY, W - rightPad() - dw, y1 + (lineH(fBold) - lineH(fSmall)) / 2, dw, date, ALIGN_RIGHT);

  int y2 = y1 + lineH(fBold) + FS / 6;
  std::string prev;
  if (c.last_message_) {
    auto &m = *c.last_message_;
    if (m.is_outgoing_) prev = std::string(tr(S_YOU)) + ": ";
    else if (tg.isGroup(c)) {
      std::string n = tg.senderName(m.sender_id_.get());
      if (!n.empty()) prev = n + ": ";
    }
    prev += previewOf(&m);
  }
  if (c.draft_message_) prev = tr(S_DRAFT);
  // unread badge
  int bw = 0;
  int unread = c.unread_count_;
  if (unread > 0 || c.is_marked_as_unread_ || c.unread_mention_count_ > 0) {
    std::string n = unread > 0 ? (unread > 999 ? "999+" : std::to_string(unread)) : " ";
    if (c.unread_mention_count_ > 0) n = "@ " + n;
    int bh = lineH(fSmallBold);
    bw = std::max(textW(fSmallBold, n) + FS * 2 / 3, bh);
    int bx = W - rightPad() - bw;
    bool muted = tg.isMuted(c);
    fillRound(bx, y2, bw, bh, bh / 2, muted ? LGRAY : BLACK);
    text(fSmallBold, muted ? BLACK : WHITE, bx, y2, bw, bh, n, ALIGN_CENTER | VALIGN_MIDDLE);
    bw += pad;
  }
  line1(fSmall, DGRAY, x, y2, w - bw, prev);
  FillArea(pad, y + rh - 1, W - pad - rightPad(), 1, LGRAY);
  addHit(0, y, W, rh, H_CHAT, c.id_);
}

static void drawChats() {
  ClearScreen();
  hits.clear();
  auto ids = currentChatIds();
  int per = rowsPerPage();
  int pages = std::max(1, static_cast<int>((ids.size() + per - 1) / per));
  if (!searchMode && !tg.listComplete["main"]) pages = std::max(pages, listPage + 2);
  if (listPage >= pages) listPage = pages - 1;
  int y = headerH();
  size_t start = static_cast<size_t>(listPage) * per;
  for (size_t i = start; i < ids.size() && i < start + per; i++) {
    auto c = tg.chat(ids[i]);
    if (!c) continue;
    drawChatRow(*c, y);
    y += rowH();
  }
  if (ids.empty()) {
    std::string msg = (searchMode || tg.listComplete["main"]) ? tr(S_NOTHING) : "…";
    text(fReg, DGRAY, 0, H / 3, W, FS * 3, msg, ALIGN_CENTER | VALIGN_MIDDLE);
  }
  // load more when the user reaches the end of what is known
  if (!searchMode && start + per * 2 >= ids.size()) tg.loadChats("main", 60);
  if (!ids.empty()) {
    double total = std::max(ids.size(), start + per);
    scrollbar(headerH() + FS / 4, H - headerH() - footerH() - FS / 2, start / total, per / total);
  }
  drawHeader(searchMode ? searchQuery : std::string(tr(S_CHATS)), searchMode, MENU_ICON, H_MENU, true);
  drawPager(listPage, pages);
}

// ---------- chat list menu ----------
static std::vector<imenu> menuItems;
static std::vector<std::string> menuStrings;
static std::vector<std::function<void()>> menuActions;

static void menuHandler(int index) {
  modalOpen = false;
  if (index >= 1 && index <= static_cast<int>(menuActions.size())) {
    auto fn = menuActions[index - 1];
    if (fn) fn();
  }
  invalidate(true);
}

static void openMenu(const std::string &header, const std::vector<std::pair<std::string, std::function<void()>>> &items, int y = -1) {
  menuStrings.clear();
  menuActions.clear();
  menuItems.clear();
  // the firmware draws the title in capitals next to its close button and does not cut it,
  // so keep it short enough never to reach the button
  menuStrings.push_back(util::truncate(util::clean(header, true), 14));
  for (auto &it : items) menuStrings.push_back(it.first);
  menuItems.push_back({ITEM_HEADER, 0, const_cast<char *>(menuStrings[0].c_str()), nullptr});
  for (size_t i = 0; i < items.size(); i++) {
    menuItems.push_back({ITEM_ACTIVE, static_cast<short>(i + 1), const_cast<char *>(menuStrings[i + 1].c_str()), nullptr});
    menuActions.push_back(items[i].second);
  }
  menuItems.push_back({0, 0, nullptr, nullptr});
  modalOpen = true;
  OpenMenu(menuItems.data(), 0, W / 4, y < 0 ? headerH() : y, menuHandler);
}

static void setScreen(Scr s) {
  scr = s;
  invalidate(true);
}

static void confirmLogout() {
  modalOpen = true;
  Dialog(ICON_QUESTION, APP_NAME, tr(S_LOG_OUT_Q), tr(S_YES), tr(S_NO),
         [](int b) {
           modalOpen = false;
           if (b == 1) {
             tg.logOut();
             scr = Scr::Login;
           }
           invalidate(true);
         });
}

static void quitApp();
void openChatScreen(int64_t chatId);

static void chatsMenu() {
  std::vector<std::pair<std::string, std::function<void()>>> items;
  items.push_back({std::string(tr(S_LANG_LABEL)) + ": " + tr(S_LANGUAGE), [] {
                     chooseLanguage((settings.lang + 1) % 3);
                     tg.connection = tg.connection.empty() ? "" : tr(S_CONNECTING);
                   }});
  items.push_back({tr(S_REFRESH), [] { invalidate(true); }});
  items.push_back({tr(S_LOG_OUT), [] { confirmLogout(); }});
  items.push_back({tr(S_QUIT), [] { quitApp(); }});
  openMenu(APP_NAME, items);
}

static void chatContextMenu(int64_t chatId) {
  auto c = tg.chat(chatId);
  if (!c) return;
  std::vector<std::pair<std::string, std::function<void()>>> items;
  items.push_back({tr(S_OPEN), [chatId] { openChatScreen(chatId); }});
  items.push_back({tr(S_MARK_READ), [chatId] { tg.markChatRead(chatId); }});
  openMenu(util::truncate(util::clean(c->title_, true), 40), items);
}

// ======================================================================
// CHAT SCREEN
// ======================================================================

static int inputH() { return FS * 2 + FS / 2 + (replyTo ? lineH(fSmall) + FS / 4 : 0); }
static int msgTop() { return headerH(); }
static int sponsorReserve = 0;  // space under the newest messages taken by a sponsored message
static int msgBottom() { return H - inputH() - sponsorReserve; }

void openChatScreen(int64_t chatId) {
  tg.openChat(chatId);
  bottomMsgId = 0;
  topShownId = 0;
  tallOffset = 0;
  replyTo = 0;
  viewedIds.clear();
  setScreen(Scr::Chat);
}

static void closeChatScreen() {
  tg.closeChat();
  replyTo = 0;
  setScreen(Scr::Chats);
}

static std::vector<int64_t> msgOrder() {
  std::vector<int64_t> v;
  v.reserve(tg.msgs.size());
  for (auto &p : tg.msgs) v.push_back(p.first);
  return v;
}

static int bubbleMaxW() { return (W - rightPad()) * 85 / 100; }

static std::string replySnippet(const td_api::message &m) {
  if (!m.reply_to_ || m.reply_to_->get_id() != td_api::messageReplyToMessage::ID) return "";
  auto &r = static_cast<const td_api::messageReplyToMessage &>(*m.reply_to_);
  std::string who, what;
  if (r.chat_id_ == 0 || r.chat_id_ == m.chat_id_) {
    if (auto orig = tg.message(r.message_id_)) {
      who = orig->is_outgoing_ ? tr(S_YOU) : tg.senderName(orig->sender_id_.get());
      what = previewOf(orig);
    }
  }
  if (what.empty() && r.content_) what = util::clean(contentText(r.content_.get(), nullptr, nullptr, 0), true);
  if (who.empty() && r.origin_) {
    switch (r.origin_->get_id()) {
      case td_api::messageOriginUser::ID:
        who = tg.userName(static_cast<const td_api::messageOriginUser &>(*r.origin_).sender_user_id_);
        break;
      case td_api::messageOriginHiddenUser::ID:
        who = static_cast<const td_api::messageOriginHiddenUser &>(*r.origin_).sender_name_;
        break;
      case td_api::messageOriginChat::ID: {
        auto c = tg.chat(static_cast<const td_api::messageOriginChat &>(*r.origin_).sender_chat_id_);
        if (c) who = c->title_;
        break;
      }
      case td_api::messageOriginChannel::ID: {
        auto c = tg.chat(static_cast<const td_api::messageOriginChannel &>(*r.origin_).chat_id_);
        if (c) who = c->title_;
        break;
      }
    }
  }
  if (who.empty() && what.empty()) return tr(S_MESSAGE);
  return util::truncate(util::clean(who, true) + (who.empty() ? "" : ": ") + what, 80);
}

static std::string forwardLabel(const td_api::message &m) {
  if (!m.forward_info_ || !m.forward_info_->origin_) return "";
  auto &o = *m.forward_info_->origin_;
  std::string from;
  switch (o.get_id()) {
    case td_api::messageOriginUser::ID: from = tg.userName(static_cast<const td_api::messageOriginUser &>(o).sender_user_id_); break;
    case td_api::messageOriginHiddenUser::ID: from = static_cast<const td_api::messageOriginHiddenUser &>(o).sender_name_; break;
    case td_api::messageOriginChat::ID: {
      auto c = tg.chat(static_cast<const td_api::messageOriginChat &>(o).sender_chat_id_);
      from = c ? c->title_ : "";
      break;
    }
    case td_api::messageOriginChannel::ID: {
      auto c = tg.chat(static_cast<const td_api::messageOriginChannel &>(o).chat_id_);
      from = c ? c->title_ : tr(S_CHANNEL);
      break;
    }
  }
  return std::string(tr(S_FORWARDED)) + (from.empty() ? "" : ": " + util::clean(from, true));
}

static std::string fileStatus(const td_api::file *f, int64_t size, int32_t fileId) {
  std::string sz = size > 0 ? util::fileSize(size) : "";
  if (Tg::done(f)) return sz + "  ·  " + tr(S_OPEN);
  if (f && f->local_ && (f->local_->is_downloading_active_ || wanted.count(fileId))) {
    int pct = size > 0 ? static_cast<int>(f->local_->downloaded_size_ * 100 / size) : 0;
    return std::to_string(std::min(pct, 99)) + "% · " + sz;
  }
  return sz + "  ↓";
}

// Lays out one message. With draw=false only measures. Returns height including spacing.
static int layoutMessage(const td_api::message &m, int prevDate, bool draw, int y) {
  int pad = FS / 2, gap = FS / 2;
  int total = 0;
  auto chat = tg.chat(m.chat_id_);
  bool group = chat && tg.isGroup(*chat);
  bool channel = chat && tg.isChannel(*chat);

  // date separator
  if (prevDate == 0 || util::dayIndex(prevDate) != util::dayIndex(m.date_)) {
    int dh = lineH(fSmallBold) + FS / 2;
    if (draw) text(fSmallBold, DGRAY, 0, y, W, dh, util::longDate(m.date_), ALIGN_CENTER | VALIGN_MIDDLE);
    total += dh;
    y += dh;
  }

  bool service = false;
  Media media;
  int maxW = channel ? W - pad - rightPad() : bubbleMaxW();
  int cwMax = maxW - 2 * pad;
  std::string body = util::clean(contentText(m.content_.get(), &media, &service, cwMax));

  if (service) {
    std::string who = m.is_outgoing_ ? tr(S_YOU) : tg.senderName(m.sender_id_.get());
    std::string s = who + " — " + body;
    int sw = W - 2 * pad - rightPad();
    int sh = textH(fSmall, sw, s) + FS / 3;
    if (draw) {
      text(fSmall, DGRAY, 2 * pad, y, sw, sh, s, ALIGN_CENTER | VALIGN_TOP);
      addHit(0, y, W, sh, H_MSG, m.id_);
    }
    return total + sh + gap;
  }

  std::string sender = (!m.is_outgoing_ && group) ? tg.senderName(m.sender_id_.get()) : "";
  if (channel && !m.author_signature_.empty()) sender = util::clean(m.author_signature_, true);
  std::string fwd = forwardLabel(m);
  std::string reply = replySnippet(m);

  std::string foot = util::hhmm(m.date_);
  int tickCount = 0;
  if (m.edit_date_ > 0) foot = std::string(tr(S_EDITED)) + " " + foot;
  if (m.is_outgoing_) {
    if (m.sending_state_ && m.sending_state_->get_id() == td_api::messageSendingStatePending::ID) foot += " •";
    else if (m.sending_state_ && m.sending_state_->get_id() == td_api::messageSendingStateFailed::ID) foot += " (!)";
    else tickCount = (chat && chat->last_read_outbox_message_id_ >= m.id_) ? 2 : 1;
  }

  // width of the bubble
  int cw;
  if (media.kind != 0 || channel) {
    cw = cwMax;
  } else {
    int need = std::max(textW(fTiny, foot) + (tickCount ? ticksWidth(tickCount == 2) + FS / 6 : 0), 1);
    if (!body.empty()) {
      // widest line of the text, capped at the bubble limit
      int mw = 0;
      size_t startPos = 0;
      while (startPos <= body.size() && mw < cwMax) {
        size_t nl = body.find('\n', startPos);
        std::string ln = body.substr(startPos, nl == std::string::npos ? std::string::npos : nl - startPos);
        mw = std::max(mw, textW(fReg, ln));
        if (nl == std::string::npos) break;
        startPos = nl + 1;
      }
      need = std::max(need, std::min(mw + 4, cwMax));
    }
    if (!sender.empty()) need = std::max(need, std::min(textW(fSmallBold, sender), cwMax));
    if (!fwd.empty()) need = std::max(need, std::min(textW(fSmall, fwd), cwMax));
    if (!reply.empty()) need = std::max(need, std::min(textW(fSmall, reply) + pad, cwMax));
    cw = std::min(need, cwMax);
  }
  int bw = cw + 2 * pad;
  int bx = m.is_outgoing_ && !channel ? W - rightPad() - bw : pad;
  int cx = bx + pad;

  // measure parts
  int hSender = sender.empty() ? 0 : lineH(fSmallBold);
  int hFwd = fwd.empty() ? 0 : textH(fSmall, cw, fwd);
  int hReply = reply.empty() ? 0 : std::min(textH(fSmall, cw - pad, reply), lineH(fSmall) * 2) + FS / 4;
  int mediaH = media.kind == 2 ? lineH(fSmallBold) + lineH(fTiny) * 2 + FS / 3 : 0;
  int hBody = body.empty() ? 0 : textH(fReg, cw, body);
  int hFoot = lineH(fTiny);
  int inner = hSender + hFwd + hReply + (mediaH ? mediaH + FS / 4 : 0) + hBody + hFoot + FS / 6;
  int bh = inner + 2 * (pad / 2) + pad / 2;

  if (draw) {
    if (m.is_outgoing_ && !channel) roundBox(bx, y, bw, bh, 0xE0E0E0, DGRAY);
    else roundBox(bx, y, bw, bh, WHITE, BLACK);
    addHit(bx, y, bw, bh, H_MSG, m.id_);
    int cy = y + pad / 2;
    if (hSender) {
      line1(fSmallBold, BLACK, cx, cy, cw, sender);
      cy += hSender;
    }
    if (hFwd) {
      text(fSmall, DGRAY, cx, cy, cw, hFwd, fwd);
      cy += hFwd;
    }
    if (hReply) {
      FillArea(cx, cy + 2, 3, hReply - FS / 4 - 2, BLACK);
      text(fSmall, DGRAY, cx + pad, cy, cw - pad, hReply - FS / 4, reply, ALIGN_LEFT | VALIGN_TOP | DOTS);
      cy += hReply;
    }
    if (media.kind == 2) {
      auto f = tg.file(media.file);
      int iconS = mediaH - FS / 6;
      fillRound(cx, cy, iconS, iconS, radius(), BLACK);
      text(fTiny, WHITE, cx, cy, iconS, iconS, media.label, ALIGN_CENTER | VALIGN_MIDDLE);
      int tx = cx + iconS + pad, tw = cw - iconS - pad;
      line1(fSmallBold, BLACK, tx, cy, tw, util::clean(media.title, true));
      std::string st = fileStatus(f, media.size, f ? f->id_ : 0);
      text(fTiny, DGRAY, tx, cy + lineH(fSmallBold), tw, lineH(fTiny) * 2, st, ALIGN_LEFT | VALIGN_TOP | DOTS);
      addHit(cx, cy, cw, mediaH, H_FILE, m.id_, f ? f->id_ : 0);
      cy += mediaH + FS / 4;
    }
    if (hBody) {
      text(fReg, BLACK, cx, cy, cw, hBody + 2, body);
      cy += hBody;
    }
    int tw = tickCount ? ticksWidth(tickCount == 2) + FS / 6 : 0;
    line1(fTiny, DGRAY, cx, cy + FS / 6, cw - tw, foot, ALIGN_RIGHT);
    if (tickCount) ticks(cx + cw - tw + FS / 6, cy + FS / 6 + lineH(fTiny) * 3 / 4, tickCount == 2, DGRAY);
  }
  return total + bh + gap;
}

struct Placed {
  size_t idx;
  int y, h;
};

static int prevDateOf(const std::vector<int64_t> &order, size_t i) {
  if (i == 0) return 0;
  auto m = tg.message(order[i - 1]);
  return m ? m->date_ : 0;
}

// Fills the area upwards from the message at index bottom. Returns placed messages, top first.
static std::vector<Placed> placeFromBottom(const std::vector<int64_t> &order, size_t bottom) {
  std::vector<Placed> out;
  int y = msgBottom() - FS / 3;
  for (size_t k = bottom + 1; k-- > 0;) {
    auto m = tg.message(order[k]);
    if (!m) continue;
    int h = layoutMessage(*m, prevDateOf(order, k), false, 0);
    // the message that does not fit completely is shown cut off at the top
    y -= h;
    out.push_back({k, y, h});
    if (y < msgTop()) break;
  }
  std::reverse(out.begin(), out.end());
  return out;
}

static size_t indexOf(const std::vector<int64_t> &order, int64_t id) {
  auto it = std::lower_bound(order.begin(), order.end(), id);
  if (it == order.end()) return order.size() - 1;
  if (*it != id && it != order.begin()) --it;
  return static_cast<size_t>(it - order.begin());
}

// Sponsored message shown under the newest channel post. Returns its height.
static int layoutSponsored(const td_api::sponsoredMessage &sp, bool draw, int y) {
  int pad = FS / 2;
  int bw = W - pad - rightPad(), cw = bw - 2 * pad;
  std::string label = tr(sp.is_recommended_ ? S_RECOMMENDED : S_SPONSORED);
  std::string title = util::clean(sp.title_, true);
  std::string body = util::clean(contentText(sp.content_.get(), nullptr, nullptr, 0));
  std::string btn = util::clean(sp.button_text_, true);
  int hLabel = lineH(fTiny);
  int hTitle = title.empty() ? 0 : lineH(fSmallBold);
  int hBody = body.empty() ? 0 : std::min(textH(fSmall, cw, body), lineH(fSmall) * 4);
  int hBtn = btn.empty() ? 0 : lineH(fSmallBold) + FS / 2;
  int bh = pad + hLabel + hTitle + hBody + (hBtn ? hBtn + FS / 4 : 0) + pad;
  if (draw) {
    roundBox(pad, y, bw, bh, WHITE, DGRAY);
    int cx = pad * 2, cy = y + pad;
    line1(fTiny, DGRAY, cx, cy, cw, label);
    cy += hLabel;
    if (hTitle) { line1(fSmallBold, BLACK, cx, cy, cw, title); cy += hTitle; }
    if (hBody) { text(fSmall, BLACK, cx, cy, cw, hBody, body, ALIGN_LEFT | VALIGN_TOP | DOTS); cy += hBody; }
    if (hBtn) {
      cy += FS / 4;
      roundBox(cx, cy, cw, hBtn, WHITE, BLACK);
      text(fSmallBold, BLACK, cx, cy, cw, hBtn, btn, ALIGN_CENTER | VALIGN_MIDDLE | DOTS);
    }
    addHit(pad, y, bw, bh, H_SPONSORED, sp.message_id_);
  }
  return bh + FS / 3;
}

static void sponsoredTapped(int64_t id) {
  const td_api::sponsoredMessage *sp = nullptr;
  for (auto &s : tg.sponsored) if (s && s->message_id_ == id) sp = s.get();
  if (!sp) return;
  static std::string info;
  info.clear();
  if (sp->sponsor_) {
    if (!sp->sponsor_->url_.empty()) info += sp->sponsor_->url_ + "\n";
    if (!sp->sponsor_->info_.empty()) info += util::clean(sp->sponsor_->info_) + "\n";
  }
  if (!sp->additional_info_.empty()) info += util::clean(sp->additional_info_) + "\n";
  info += std::string("\n") + tr(S_SPONSORED_INFO);
  tg.clickSponsored(id);
  Message(ICON_INFORMATION, tr(sp->is_recommended_ ? S_RECOMMENDED : S_SPONSORED), info.c_str(), 20000);
}

static void drawChat() {
  ClearScreen();
  hits.clear();
  // reserve room for a sponsored message when the newest messages are on screen
  const td_api::sponsoredMessage *sp = (bottomMsgId == 0 && !tg.sponsored.empty()) ? tg.sponsored.front().get() : nullptr;
  sponsorReserve = 0;
  if (sp) sponsorReserve = std::min(layoutSponsored(*sp, false, 0), (H - headerH() - inputH()) / 3);
  auto chat = tg.chat(tg.openChatId);
  auto order = msgOrder();
  std::vector<int64_t> toView;

  double sbStart = 0, sbSize = 1;
  SetClip(0, msgTop(), W, msgBottom() - msgTop());
  if (!order.empty()) {
    size_t bottom = bottomMsgId ? indexOf(order, bottomMsgId) : order.size() - 1;
    auto placed = placeFromBottom(order, bottom);
    tallShown = false;
    topPartial = false;
    if (!placed.empty()) {
      topShownId = order[placed.front().idx];
      // a single message taller than the screen is paged through in screen-sized steps
      int area = msgBottom() - msgTop();
      if (placed.size() == 1 && placed[0].h > area) {
        tallShown = true;
        tallMax = placed[0].h - area + FS / 2;
        tallOffset = std::max(0, std::min(tallOffset, tallMax));
        placed[0].y = msgTop() - tallOffset;
      } else if (placed.size() > 1 && placed[0].y < msgTop()) {
        topPartial = true;
      }
    }
    for (auto &p : placed) {
      auto m = tg.message(order[p.idx]);
      if (!m) continue;
      layoutMessage(*m, prevDateOf(order, p.idx), true, p.y);
      if (!m->is_outgoing_ && !viewedIds.count(m->id_)) {
        viewedIds.insert(m->id_);
        toView.push_back(m->id_);
      }
    }
    if (!placed.empty() && placed.front().idx < 15) tg.loadOlder();
    if (!placed.empty()) {
      double n = static_cast<double>(order.size());
      sbStart = placed.front().idx / n;
      sbSize = (placed.back().idx - placed.front().idx + 1) / n;
    }
  } else {
    std::string s = tg.historyLoading ? "…" : tr(S_NOTHING);
    text(fReg, DGRAY, 0, H / 3, W, FS * 3, s, ALIGN_CENTER | VALIGN_MIDDLE);
  }
  SetClip(0, 0, W, H);
  scrollbar(msgTop() + FS / 4, msgBottom() - msgTop() - FS / 2, sbStart, sbSize);
  if (sp) {
    SetClip(0, msgBottom(), W, sponsorReserve);
    layoutSponsored(*sp, true, msgBottom());
    SetClip(0, 0, W, H);
    tg.viewSponsored(sp->message_id_);
  }

  tg.viewMessages(toView);

  // header
  std::string title = chat ? util::clean(chat->title_, true) : "";
  if (chat && chat->id_ == tg.myId) title = tr(S_SAVED);
  drawHeader(title, true, MENU_ICON, H_MENU, true);

  // input bar
  int ih = inputH(), y = H - ih, pad = FS / 2;
  FillArea(0, y, W, ih, WHITE);
  FillArea(0, y, W, 2, BLACK);
  int cy = y + pad / 2;
  if (replyTo) {
    std::string r;
    if (auto rm = tg.message(replyTo)) r = (rm->is_outgoing_ ? std::string(tr(S_YOU)) : tg.senderName(rm->sender_id_.get())) + ": " + previewOf(rm);
    FillArea(pad, cy, 3, lineH(fSmall), BLACK);
    line1(fSmall, DGRAY, pad * 2, cy, W - 3 * pad - FS * 2, r);
    text(fBold, BLACK, W - FS * 2, cy, FS * 2, lineH(fSmall), "×", ALIGN_CENTER | VALIGN_MIDDLE);
    addHit(W - FS * 3, y, FS * 3, lineH(fSmall) + pad, H_REPLY_CANCEL);
    cy += lineH(fSmall) + FS / 4;
  }
  int bh = y + ih - cy - pad / 2;
  // paging buttons
  int arrowW = FS * 2;
  bool canOlder = !order.empty() && (topShownId != order.front() || topPartial || !tg.historyEnd || (tallShown && tallOffset > 0));
  bool canNewer = bottomMsgId != 0 || tg.detached || (tallShown && tallOffset < tallMax);
  triangle(pad, cy, arrowW, bh, UP, canOlder ? BLACK : LGRAY);
  if (canOlder) addHit(0, cy, arrowW + pad, bh, H_PREV);
  triangle(pad + arrowW, cy, arrowW, bh, DOWN, canNewer ? BLACK : LGRAY);
  if (canNewer) addHit(pad + arrowW, cy, arrowW, bh, H_NEXT);
  int ix = pad + arrowW * 2 + pad;
  int attachW = FS * 2;
  int iw = W - ix - pad - attachW - pad;
  roundBox(ix, cy, iw, bh, WHITE, BLACK);
  text(fReg, DGRAY, ix + pad, cy, iw - 2 * pad, bh, tr(S_MESSAGE), ALIGN_LEFT | VALIGN_MIDDLE | DOTS);
  addHit(ix, cy, iw, bh, H_INPUT);
  // attach button: a paper clip is not in the font, so a plus sign
  roundBox(W - pad - attachW, cy, attachW, bh, WHITE, BLACK);
  text(fBig, BLACK, W - pad - attachW, cy, attachW, bh, "+", ALIGN_CENTER | VALIGN_MIDDLE);
  addHit(W - pad - attachW, cy, attachW + pad, bh, H_ATTACH);
}

static int pageStep() { return (msgBottom() - msgTop()) * 85 / 100; }

static void chatOlder() {
  auto order = msgOrder();
  if (order.empty()) return;
  if (tallShown && tallOffset > 0) {
    tallOffset = std::max(0, tallOffset - pageStep());
    invalidate(true);
    return;
  }
  size_t top = indexOf(order, topShownId);
  if (topPartial) {
    // the cut-off message becomes the bottom of the next page
    bottomMsgId = order[top];
    tallOffset = 1 << 30;
    invalidate(true);
    return;
  }
  if (top == 0) {
    tg.loadOlder();
    if (tg.historyEnd) toast(tr(S_START_OF_CHAT), 1500);
    return;
  }
  bottomMsgId = order[top - 1];
  tallOffset = 1 << 30;  // if the previous message is tall, start from its end
  invalidate(true);
}

// Index of the last message that fits on screen when message startIdx is at the top.
static size_t fillDown(const std::vector<int64_t> &order, size_t startIdx) {
  int avail = msgBottom() - msgTop() - FS / 3;
  int used = 0;
  size_t last = startIdx;
  for (size_t k = startIdx; k < order.size(); k++) {
    auto m = tg.message(order[k]);
    if (!m) continue;
    int h = layoutMessage(*m, prevDateOf(order, k), false, 0);
    if (used + h > avail && k != startIdx) break;
    used += h;
    last = k;
  }
  return last;
}

// Shows the screen ending at index last; at the end of the loaded window either sticks to the
// newest message or, after a jump, asks for newer ones.
static void showUntil(const std::vector<int64_t> &order, size_t last) {
  if (last + 1 >= order.size()) {
    if (tg.detached) {
      bottomMsgId = order[last];
      tg.loadNewer();
    } else {
      bottomMsgId = 0;
    }
  } else {
    bottomMsgId = order[last];
  }
  tallOffset = 0;
  invalidate(true);
}

static void chatNewer() {
  auto order = msgOrder();
  if (tallShown && tallOffset < tallMax) {
    tallOffset = std::min(tallMax, tallOffset + pageStep());
    invalidate(true);
    return;
  }
  if (order.empty() || bottomMsgId == 0) return;
  size_t bottom = indexOf(order, bottomMsgId);
  if (bottom + 1 >= order.size()) {
    tg.loadNewer();  // only does something after a jump
    return;
  }
  showUntil(order, fillDown(order, bottom + 1));
}

static void onSendText(char *t) {
  modalOpen = false;
  if (t) {
    std::string s = t;
    while (!s.empty() && (s.back() == ' ' || s.back() == '\n')) s.pop_back();
    if (!s.empty() && tg.openChatId) {
      tg.reattach();
      tg.sendText(tg.openChatId, s, replyTo);
      replyTo = 0;
      bottomMsgId = 0;
      tallOffset = 0;
    }
  }
  invalidate(true);
}

static void composeMessage() {
  kbuf[0] = 0;
  modalOpen = true;
  OpenKeyboard(tr(replyTo ? S_REPLY : S_MESSAGE), kbuf, sizeof(kbuf) - 1, KBD_NORMAL, onSendText);
}

// ---------- files: save & open ----------
static void openPath(const std::string &path) {
  std::string ext = util::extOf(path);
  if (util::isImageExt(ext)) {
    OpenBook(path.c_str(), "", OB_WITHRETURN);
    return;
  }
  if (util::isBookExt(ext)) {
    OpenBook(path.c_str(), "", OB_WITHRETURN);
    return;
  }
  toast(tr(S_NOT_SUPPORTED_FILE), 4000);
}

// Copies a finished TDLib download into Downloads/Teleink.
static std::string exportFile(const td_api::file &f, const std::string &name) {
  auto ex = exported.find(f.id_);
  if (ex != exported.end() && util::exists(ex->second)) return ex->second;
  util::mkdirs(gDownloadDir);
  bool already = false;
  std::string target = util::targetPath(gDownloadDir, name, f.size_ ? f.size_ : util::sizeOf(f.local_->path_), &already);
  if (!already && !util::copyFile(f.local_->path_, target)) {
    util::log("copy failed %s -> %s", f.local_->path_.c_str(), target.c_str());
    toast(tr(S_ERR_SAVE));
    return "";
  }
  exported[f.id_] = target;
  util::log("exported %s", target.c_str());
  return target;
}

static void onFileDone(const td_api::file &f) {
  auto it = wanted.find(f.id_);
  if (it == wanted.end()) return;
  Wanted w = it->second;
  wanted.erase(it);
  std::string path = exportFile(f, w.name);
  if (path.empty()) return;
  std::string shortName = path.substr(path.rfind('/') + 1);
  toast(std::string(tr(S_SAVED_TO)) + ": " + shortName, 4000);
  if (w.openWhenDone && scr == Scr::Chat && !modalOpen) {
    std::string ext = util::extOf(path);
    if (util::isBookExt(ext)) {
      modalOpen = true;
      static std::string pending;
      pending = path;
      static std::string dialogTitle;
      dialogTitle = util::truncate(shortName, 20);
      Dialog(ICON_QUESTION, dialogTitle.c_str(), tr(S_OPEN), tr(S_YES), tr(S_NO), [](int b) {
        modalOpen = false;
        if (b == 1) openPath(pending);
        invalidate(true);
      });
    }
  }
}

static void fileTapped(int64_t msgId, int32_t fileId) {
  auto m = tg.message(msgId);
  if (!m || !fileId) return;
  Media media;
  contentText(m->content_.get(), &media, nullptr, bubbleMaxW());
  auto f = tg.file(media.file);
  if (!f) return;
  std::string name = media.name.empty() ? "file" : media.name;
  if (Tg::done(f)) {
    std::string path = exportFile(*f, name);
    if (!path.empty()) openPath(path);
    return;
  }
  if (Tg::downloading(f) || wanted.count(fileId)) {
    modalOpen = true;
    static int32_t cancelId;
    cancelId = fileId;
    Dialog(ICON_QUESTION, APP_NAME, tr(S_CANCEL_DL_Q), tr(S_YES), tr(S_NO), [](int b) {
      modalOpen = false;
      if (b == 1) {
        tg.cancelDownload(cancelId);
        wanted.erase(cancelId);
      }
      invalidate(true);
    });
    return;
  }
  wanted[fileId] = {m->chat_id_, msgId, name, true};
  tg.download(fileId, 16);
  invalidate();
}

// ---------- file picker (sending files) ----------
static void readPickDir() {
  pickEntries.clear();
  DIR *d = opendir(pickDir.c_str());
  if (d) {
    while (auto e = readdir(d)) {
      std::string n = e->d_name;
      if (n == "." || n == ".." || n.empty() || n[0] == '.') continue;
      std::string full = pickDir + "/" + n;
      struct stat st;
      if (stat(full.c_str(), &st) != 0) continue;
      pickEntries.push_back({n, S_ISDIR(st.st_mode), static_cast<int64_t>(st.st_size)});
    }
    closedir(d);
  }
  std::sort(pickEntries.begin(), pickEntries.end(), [](const PickEntry &a, const PickEntry &b) {
    if (a.dir != b.dir) return a.dir;
    return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
  });
  if (pickDir != "/mnt/ext1" && pickDir != "/mnt/ext2" && pickDir != "/") pickEntries.insert(pickEntries.begin(), {"..", true, 0});
  pickPage = 0;
}

static int pickRowH() { return lineH(fReg) + FS; }
static int pickPerPage() { return std::max(1, (H - headerH() - footerH()) / pickRowH()); }

static void drawFiles() {
  ClearScreen();
  hits.clear();
  int per = pickPerPage();
  int pages = std::max(1, static_cast<int>((pickEntries.size() + per - 1) / per));
  if (pickPage >= pages) pickPage = pages - 1;
  int y = headerH(), pad = FS / 2;
  for (size_t i = pickPage * per; i < pickEntries.size() && i < static_cast<size_t>(pickPage + 1) * per; i++) {
    auto &e = pickEntries[i];
    int rh = pickRowH();
    std::string label = e.dir ? (e.name == ".." ? std::string("..") : e.name + "/") : e.name;
    std::string size = e.dir ? "" : util::fileSize(e.size);
    int sw = size.empty() ? 0 : textW(fSmall, size) + pad;
    line1(e.dir ? fBold : fReg, BLACK, pad, y + FS / 2, W - 2 * pad - sw, label);
    if (sw) line1(fSmall, DGRAY, W - pad - sw, y + FS / 2, sw, size, ALIGN_RIGHT);
    FillArea(pad, y + rh - 1, W - 2 * pad, 1, LGRAY);
    addHit(0, y, W, rh, H_PICK, static_cast<int64_t>(i));
    y += rh;
  }
  if (pickEntries.empty()) text(fReg, DGRAY, 0, H / 3, W, FS * 3, tr(S_NOTHING), ALIGN_CENTER | VALIGN_MIDDLE);
  std::string title = pickDir == "/mnt/ext1" ? std::string(tr(S_STORAGE)) : pickDir == "/mnt/ext2" ? std::string(tr(S_SD)) : pickDir.substr(pickDir.rfind('/') + 1);
  drawHeader(title, true, util::exists("/mnt/ext2") ? (pickDir.rfind("/mnt/ext2", 0) == 0 ? tr(S_STORAGE) : tr(S_SD)) : "", H_MENU);
  drawPager(pickPage, pages);
}

static void openPicker() {
  if (!util::exists(pickDir)) pickDir = "/mnt/ext1";
  readPickDir();
  setScreen(Scr::Files);
}

static void pickTapped(size_t i) {
  if (i >= pickEntries.size()) return;
  auto e = pickEntries[i];
  if (e.dir) {
    if (e.name == "..") pickDir = pickDir.substr(0, pickDir.rfind('/'));
    else pickDir += "/" + e.name;
    if (pickDir.empty()) pickDir = "/";
    readPickDir();
    invalidate(true);
    return;
  }
  static std::string pending;
  pending = pickDir + "/" + e.name;
  modalOpen = true;
  std::string q = e.name + "\n" + util::fileSize(e.size);
  Dialog(ICON_QUESTION, tr(S_SEND_Q), q.c_str(), tr(S_YES), tr(S_NO), [](int b) {
    modalOpen = false;
    if (b == 1 && tg.openChatId) {
      tg.sendDocument(tg.openChatId, pending, replyTo);
      replyTo = 0;
      bottomMsgId = 0;
      scr = Scr::Chat;
      toast(tr(S_SENDING), 2000);
    }
    invalidate(true);
  });
}

// ---------- search inside a chat ----------
static std::vector<object_ptr<td_api::message>> found;
static std::string foundQuery;
static bool foundLoading = false;
static int foundPage = 0;

static void onChatSearch(char *t) {
  modalOpen = false;
  std::string q = t ? util::trim(t) : "";
  if (q.empty() || !tg.openChatId) { invalidate(true); return; }
  foundQuery = q;
  found.clear();
  foundPage = 0;
  foundLoading = true;
  int64_t chatId = tg.openChatId;
  tg.searchMessages(q, [chatId](std::vector<object_ptr<td_api::message>> res) {
    if (chatId != tg.openChatId) return;
    found = std::move(res);
    foundLoading = false;
    invalidate(true);
  });
  setScreen(Scr::Found);
}

static void startChatSearch() {
  snprintf(kbuf, sizeof kbuf, "%s", foundQuery.c_str());
  modalOpen = true;
  OpenKeyboard(tr(S_SEARCH), kbuf, 128, KBD_NORMAL, onChatSearch);
}

static void drawFound() {
  ClearScreen();
  hits.clear();
  int per = rowsPerPage();
  int pages = std::max(1, static_cast<int>((found.size() + per - 1) / per));
  if (foundPage >= pages) foundPage = pages - 1;
  int y = headerH(), pad = FS / 2, rh = rowH();
  size_t start = static_cast<size_t>(foundPage) * per;
  for (size_t i = start; i < found.size() && i < start + per; i++) {
    auto &m = *found[i];
    std::string who = m.is_outgoing_ ? std::string(tr(S_YOU)) : tg.senderName(m.sender_id_.get());
    std::string date = util::shortDate(m.date_);
    int dw = textW(fSmall, date) + pad;
    int y1 = y + FS / 3;
    line1(fBold, BLACK, pad, y1, W - pad - rightPad() - dw, who);
    line1(fSmall, DGRAY, W - rightPad() - dw, y1 + (lineH(fBold) - lineH(fSmall)) / 2, dw, date, ALIGN_RIGHT);
    line1(fSmall, DGRAY, pad, y1 + lineH(fBold) + FS / 6, W - pad - rightPad(), previewOf(&m));
    FillArea(pad, y + rh - 1, W - pad - rightPad(), 1, LGRAY);
    addHit(0, y, W, rh, H_FOUND, m.id_);
    y += rh;
  }
  if (found.empty())
    text(fReg, DGRAY, 0, H / 3, W, FS * 3, foundLoading ? "…" : tr(S_NOTHING), ALIGN_CENTER | VALIGN_MIDDLE);
  if (!found.empty())
    scrollbar(headerH() + FS / 4, H - headerH() - footerH() - FS / 2, start / static_cast<double>(found.size()),
              per / static_cast<double>(std::max<size_t>(found.size(), per)));
  drawHeader(foundQuery, true, "", H_MENU, false);
  drawPager(foundPage, pages);
}

// Opens the chat at a found message: it is shown at the top, newer messages below it.
static void openFound(int64_t msgId) {
  setScreen(Scr::Chat);
  bottomMsgId = msgId;
  tallOffset = 0;
  tg.jumpTo(msgId, [msgId] {
    if (scr != Scr::Chat) return;
    auto order = msgOrder();
    if (order.empty()) return;
    showUntil(order, fillDown(order, indexOf(order, msgId)));
  });
}

// ---------- chat menus ----------
static void messageMenu(int64_t msgId) {
  auto m = tg.message(msgId);
  if (!m) return;
  std::vector<std::pair<std::string, std::function<void()>>> items;
  items.push_back({tr(S_REPLY), [msgId] {
                     replyTo = msgId;
                     composeMessage();
                   }});
  Media media;
  contentText(m->content_.get(), &media, nullptr, bubbleMaxW());
  if (media.kind == 2 && media.file) {
    int32_t fid = media.file->id_;
    auto f = tg.file(media.file);
    items.push_back({tr(Tg::done(f) ? S_OPEN : S_DOWNLOAD), [msgId, fid] { fileTapped(msgId, fid); }});
  }
  bool out = m->is_outgoing_;
  items.push_back({tr(S_DELETE), [msgId, out] {
                     modalOpen = true;
                     static int64_t delId;
                     static bool delRevoke;
                     delId = msgId;
                     delRevoke = out;
                     Dialog(ICON_QUESTION, APP_NAME, tr(S_DELETE_Q), tr(S_YES), tr(S_NO), [](int b) {
                       modalOpen = false;
                       if (b == 1) tg.deleteMessage(tg.openChatId, delId, delRevoke);
                       invalidate(true);
                     });
                   }});
  openMenu(util::truncate(previewOf(m), 30), items, H / 3);
}

static void chatMenu() {
  std::vector<std::pair<std::string, std::function<void()>>> items;
  items.push_back({tr(S_SEND_FILE), [] { openPicker(); }});
  int64_t cid = tg.openChatId;
  items.push_back({tr(S_MARK_READ), [cid] { tg.markChatRead(cid); }});
  items.push_back({tr(S_REFRESH), [] { invalidate(true); }});
  auto c = tg.chat(cid);
  openMenu(c ? util::truncate(util::clean(c->title_, true), 30) : "", items);
}

// ======================================================================
// main loop
// ======================================================================

static void drawScreen() {
  sb.visible = false;
  switch (scr) {
    case Scr::Login: drawLogin(); break;
    case Scr::Chats: drawChats(); break;
    case Scr::Chat: drawChat(); break;
    case Scr::Files: drawFiles(); break;
    case Scr::Found: drawFound(); break;
  }
  drawToast();
  if (needFull || partialCount >= 12) {
    FullUpdate();
    partialCount = 0;
  } else {
    PartialUpdate(0, 0, W, H);
    partialCount++;
  }
  needDraw = needFull = false;
}

static void quitApp() {
  exiting = true;
  util::log("closing");
  tg.close();
  long long until = nowMs() + 4000;
  while (!tg.closed && nowMs() < until) tg.poll(0.2);
  CloseApp();
}

static void pollTimer() {
  if (exiting) return;
  tg.poll();

  if (tg.closed && !exiting) {
    // after log out TDLib closes the instance; start a clean one for the next login
    tg.restart();
    scr = Scr::Login;
    invalidate(true);
  }

  // auth state transitions
  if (tg.dirtyAuth) {
    tg.dirtyAuth = false;
    if (tg.ready && scr == Scr::Login) {
      tg.lastError.clear();
      scr = Scr::Chats;
      listPage = 0;
      invalidate(true);
    } else if (!tg.ready && scr != Scr::Login) {
      scr = Scr::Login;
      invalidate(true);
    } else if (scr == Scr::Login) {
      invalidate(true);
    }
    HideHourglass();
  }

  switch (scr) {
    case Scr::Chats:
      if (tg.dirtyChats) invalidate();
      break;
    case Scr::Found:
      if (tg.dirtyChats) invalidate();
      break;
    case Scr::Chat:
      if (tg.dirtyMsgs) invalidate();
      if (!tg.dirtyFiles.empty()) invalidate();
      break;
    default:
      break;
  }
  if (tg.dirtyStatus && (scr == Scr::Chats || scr == Scr::Chat)) invalidate();
  tg.dirtyChats = tg.dirtyMsgs = tg.dirtyStatus = false;
  tg.dirtyFiles.clear();

  if (!toastText.empty() && nowMs() > toastUntil) {
    toastText.clear();
    invalidate();
  }

  bool canDraw = !modalOpen && !IsKeyboardOpened();
  static long long lastDraw = 0;
  long long now = nowMs();
  if (needDraw && canDraw && (needFull || now - lastDraw > 700)) {
    drawScreen();
    lastDraw = nowMs();
    // ask for the next login field automatically once per state
    if (scr == Scr::Login && settings.lang >= 0 && tg.authId != lastAuthPrompted && tg.lastError.empty()) {
      lastAuthPrompted = tg.authId;
      if (tg.authId == td_api::authorizationStateWaitPhoneNumber::ID ||
          tg.authId == td_api::authorizationStateWaitCode::ID ||
          tg.authId == td_api::authorizationStateWaitPassword::ID)
        openKeyboardFor(tg.authId);
    }
  }
  SetHardTimer("tg_poll", pollTimer, 150);
}

// Jumps to the position the scrollbar was dragged to.
static void scrollTo(double frac) {
  if (scr == Scr::Chats) {
    auto ids = currentChatIds();
    int per = rowsPerPage();
    int pages = std::max(1, static_cast<int>((ids.size() + per - 1) / per));
    listPage = static_cast<int>(frac * (pages - 1) + 0.5);
  } else if (scr == Scr::Found) {
    int per = rowsPerPage();
    int pages = std::max(1, static_cast<int>((found.size() + per - 1) / per));
    foundPage = static_cast<int>(frac * (pages - 1) + 0.5);
  } else if (scr == Scr::Chat) {
    auto order = msgOrder();
    if (order.empty()) return;
    // frac is where the top of the screen lands
    size_t top = static_cast<size_t>(frac * (order.size() - 1) + 0.5);
    if (top == 0) tg.loadOlder();
    showUntil(order, fillDown(order, top));
    return;
  }
  invalidate(true);
}

static void foundTap(const Hit &h);

static void handleTap(int x, int y) {
  const Hit *hit = nullptr;
  for (auto it = hits.rbegin(); it != hits.rend(); ++it) {
    if (x >= it->x && x < it->x + it->w && y >= it->y && y < it->y + it->h) {
      hit = &*it;
      break;
    }
  }
  // tapping the toast dismisses it
  if (!toastText.empty() && y > headerH() && y < headerH() + H / 4 && !hit) {
    toastText.clear();
    invalidate();
    return;
  }
  if (!hit) return;
  Hit h = *hit;
  switch (scr) {
    case Scr::Login:
      if (h.kind == H_LOGIN1) loginPrimary();
      else if (h.kind == H_LANG) chooseLanguage(static_cast<int>(h.a));
      break;
    case Scr::Chats:
      if (h.kind == H_CHAT) openChatScreen(h.a);
      else if (h.kind == H_MENU) chatsMenu();
      else if (h.kind == H_PREV) { listPage = std::max(0, listPage - 1); invalidate(); }
      else if (h.kind == H_NEXT) { listPage++; invalidate(); }
      else if (h.kind == H_SEARCH) startSearch();
      else if (h.kind == H_BACK) endSearch();
      break;
    case Scr::Chat:
      if (h.kind == H_BACK) closeChatScreen();
      else if (h.kind == H_SEARCH) startChatSearch();
      else if (h.kind == H_MENU) chatMenu();
      else if (h.kind == H_INPUT) composeMessage();
      else if (h.kind == H_ATTACH) openPicker();
      else if (h.kind == H_REPLY_CANCEL) { replyTo = 0; invalidate(); }
      else if (h.kind == H_PREV) chatOlder();
      else if (h.kind == H_NEXT) chatNewer();
      else if (h.kind == H_FILE) fileTapped(h.a, static_cast<int32_t>(h.b));
      else if (h.kind == H_MSG) messageMenu(h.a);
      else if (h.kind == H_SPONSORED) sponsoredTapped(h.a);
      break;
    case Scr::Found:
      foundTap(h);
      break;
    case Scr::Files:
      if (h.kind == H_BACK) setScreen(Scr::Chat);
      else if (h.kind == H_PICK) pickTapped(static_cast<size_t>(h.a));
      else if (h.kind == H_PREV) { pickPage = std::max(0, pickPage - 1); invalidate(); }
      else if (h.kind == H_NEXT) { pickPage++; invalidate(); }
      else if (h.kind == H_MENU) {
        pickDir = pickDir.rfind("/mnt/ext2", 0) == 0 ? "/mnt/ext1" : "/mnt/ext2";
        readPickDir();
        invalidate(true);
      }
      break;
  }
}

static void foundTap(const Hit &h) {
  if (h.kind == H_BACK) setScreen(Scr::Chat);
  else if (h.kind == H_FOUND) openFound(h.a);
  else if (h.kind == H_PREV) { foundPage = std::max(0, foundPage - 1); invalidate(); }
  else if (h.kind == H_NEXT) { foundPage++; invalidate(); }
}

static void handleLongTap(int x, int y) {
  for (auto it = hits.rbegin(); it != hits.rend(); ++it) {
    if (x >= it->x && x < it->x + it->w && y >= it->y && y < it->y + it->h) {
      if (scr == Scr::Chats && it->kind == H_CHAT) { chatContextMenu(it->a); return; }
      if (scr == Scr::Chat && (it->kind == H_MSG || it->kind == H_FILE)) { messageMenu(it->a); return; }
    }
  }
}

static void pageBack() {
  switch (scr) {
    case Scr::Chats: if (listPage > 0) { listPage--; invalidate(); } break;
    case Scr::Chat: chatOlder(); break;
    case Scr::Files: if (pickPage > 0) { pickPage--; invalidate(); } break;
    case Scr::Found: if (foundPage > 0) { foundPage--; invalidate(); } break;
    default: break;
  }
}

static void pageForward() {
  switch (scr) {
    case Scr::Chats: listPage++; invalidate(); break;
    case Scr::Chat: chatNewer(); break;
    case Scr::Files: pickPage++; invalidate(); break;
    case Scr::Found: foundPage++; invalidate(); break;
    default: break;
  }
}

static void goBack() {
  switch (scr) {
    case Scr::Chat: closeChatScreen(); break;
    case Scr::Files: setScreen(Scr::Chat); break;
    case Scr::Found: setScreen(Scr::Chat); break;
    case Scr::Chats:
      if (searchMode) endSearch();
      break;
    default: break;
  }
}

static void initFonts() {
  W = ScreenWidth();
  H = ScreenHeight();
  FS = std::max(20, std::min(W, H) / 27);
  const char *reg = iv_get_default_font(FONT_STD);
  const char *bold = iv_get_default_font(FONT_BOLD);
  std::string r = (reg && *reg) ? reg : "LiberationSans";
  std::string b = (bold && *bold) ? bold : "LiberationSans-Bold";
  fReg = OpenFont(r.c_str(), FS, 1);
  fBold = OpenFont(b.c_str(), FS, 1);
  fSmall = OpenFont(r.c_str(), FS * 82 / 100, 1);
  fSmallBold = OpenFont(b.c_str(), FS * 82 / 100, 1);
  fTiny = OpenFont(r.c_str(), FS * 68 / 100, 1);
  fBig = OpenFont(b.c_str(), FS * 115 / 100, 1);
  fHuge = OpenFont(b.c_str(), FS * 2, 1);
  // fall back to the regular face if a bold one is missing on this firmware
  ifont **all[] = {&fReg, &fBold, &fSmall, &fSmallBold, &fTiny, &fBig, &fHuge};
  for (auto pf : all)
    if (!*pf) *pf = OpenFont("LiberationSans", FS, 1);
  if (!fBold) fBold = fReg;
  util::log("screen %dx%d font %d (%s / %s)", W, H, FS, r.c_str(), b.c_str());
}

static int mainHandler(int type, int par1, int par2) {
  switch (type) {
    case EVT_INIT: {
      gDataDir = std::string(FLASHDIR) + APP_DIR_REL;
      gDownloadDir = std::string(FLASHDIR) + "/Downloads/Teleink";
      util::mkdirs(gDataDir);
      util::logPath() = gDataDir + "/app.log";
      if (util::sizeOf(util::logPath()) > (1 << 20)) unlink(util::logPath().c_str());
      util::log("=== start, model %s, firmware %s", GetDeviceModel(), GetSoftwareVersion());
      SetPanelType(PANEL_DISABLED);
      initFonts();
      loadSettings();
      loadCreds();
      if (launcher::registerIcon(FLASHDIR, ScreenWidth())) toast(tr(S_ICON_RESTART), 8000);
      tg.dataDir = gDataDir;
      tg.onFileDone = onFileDone;
      tg.onError = [](const std::string &e) {
        if (scr != Scr::Login) toast(e, 4000);
        else invalidate(true);
      };
      tg.start();
      SetHardTimer("tg_poll", pollTimer, 100);
      // make sure Wi-Fi is up; TDLib reconnects by itself once it is
      SetHardTimer("tg_net", [] {
        int r = NetConnect2(nullptr, 0);
        util::log("NetConnect: %d", r);
        tg.send(make_object<td_api::setNetworkType>(make_object<td_api::networkTypeWiFi>()));
      }, 500);
      break;
    }
    case EVT_SHOW:
      modalOpen = false;
      invalidate(true);
      drawScreen();
      break;
    case EVT_FOREGROUND:
      invalidate(true);
      if (!(QueryNetwork() & NET_CONNECTED)) {
        int r = NetConnect2(nullptr, 0);
        util::log("NetConnect on foreground: %d", r);
      }
      break;
    case EVT_NET_CONNECTED:
      tg.send(make_object<td_api::setNetworkType>(make_object<td_api::networkTypeWiFi>()));
      break;
    case EVT_NET_DISCONNECTED:
      tg.send(make_object<td_api::setNetworkType>(make_object<td_api::networkTypeNone>()));
      break;
    case EVT_POINTERDOWN:
      modalOpen = false;  // pointer events reach us only when no menu or dialog is open
      downX = par1;
      downY = par2;
      ignoreUp = false;
      sb.candidate = sb.visible && (scr == Scr::Chats || scr == Scr::Chat || scr == Scr::Found) && par1 >= W - FS * 2 &&
                     par2 >= sb.y - FS / 2 && par2 <= sb.y + sb.h + FS / 2;
      sb.dragging = false;
      return 1;
    case EVT_POINTERMOVE:
    case EVT_POINTERDRAG:
      if (sb.candidate && (sb.dragging || std::abs(par2 - downY) > FS / 3)) {
        sb.dragging = true;
        dragScrollbar(par2);
      }
      return 1;
    case EVT_POINTERLONG:
      if (sb.candidate) return 1;  // finger resting on the scrollbar is not a long tap
      ignoreUp = true;
      handleLongTap(par1, par2);
      return 1;
    case EVT_POINTERUP: {
      if (sb.dragging) {
        sb.dragging = sb.candidate = false;
        dragScrollbar(par2);
        scrollTo(sb.frac);
        downX = downY = -1;
        return 1;
      }
      sb.candidate = false;
      if (ignoreUp) { ignoreUp = false; return 1; }
      int dx = par1 - downX, dy = par2 - downY;
      if (downX >= 0 && std::abs(dy) > H / 8 && std::abs(dy) > std::abs(dx)) {
        // swipe: in lists finger up = next page; in a chat finger down = older messages
        if (scr == Scr::Chat) { if (dy > 0) chatOlder(); else chatNewer(); }
        else { if (dy < 0) pageForward(); else pageBack(); }
      } else if (downX >= 0 && std::abs(dx) > W / 4 && std::abs(dx) > std::abs(dy)) {
        if (scr == Scr::Chat || scr == Scr::Files || scr == Scr::Found) { if (dx > 0) goBack(); }
        else { if (dx < 0) pageForward(); else pageBack(); }
      } else {
        handleTap(par1, par2);
      }
      downX = downY = -1;
      return 1;
    }
    case EVT_KEYPRESS:
      switch (par1) {
        case IV_KEY_PREV:
        case IV_KEY_LEFT:
        case IV_KEY_UP:
          pageBack();
          return 1;
        case IV_KEY_NEXT:
        case IV_KEY_RIGHT:
        case IV_KEY_DOWN:
          pageForward();
          return 1;
        case IV_KEY_BACK:
          goBack();
          return 1;
        case IV_KEY_MENU:
          if (scr == Scr::Chats) chatsMenu();
          else if (scr == Scr::Chat) chatMenu();
          return 1;
        case IV_KEY_OK:
          if (scr == Scr::Chat) composeMessage();
          return 1;
      }
      break;
    case EVT_EXIT:
      if (!exiting) {
        exiting = true;
        tg.close();
        long long until = nowMs() + 3000;
        while (!tg.closed && nowMs() < until) tg.poll(0.2);
      }
          break;
  }
  return 0;
}

int main() {
  InkViewMain(mainHandler);
  return 0;
}
