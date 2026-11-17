// Minimal desktop stand-in for InkView, used only to render screenshots of the UI on the build machine.
#include <inkview.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <vector>

static int SW = 758, SH = 1024;
static std::vector<unsigned char> fb;
static FT_Library ft;
static const ifont *cur = nullptr;
static int curColor = 0;
static int clipX = 0, clipY = 0, clipW = 0, clipH = 0;
static std::map<const ifont *, FT_Face> faces;
std::string g_shotPath = "shot.pgm";

void mock_set_screen(int w, int h) { SW = w; SH = h; }

static void ensure() {
  if (fb.empty()) {
    fb.assign(SW * SH, 255);
    clipW = SW;
    clipH = SH;
    FT_Init_FreeType(&ft);
  }
}

static inline unsigned char gray(int c) {
  return static_cast<unsigned char>(((c >> 16 & 255) * 30 + (c >> 8 & 255) * 59 + (c & 255) * 11) / 100);
}
static inline void px(int x, int y, unsigned char v) {
  if (x < clipX || y < clipY || x >= clipX + clipW || y >= clipY + clipH || x < 0 || y < 0 || x >= SW || y >= SH) return;
  fb[y * SW + x] = v;
}

int ScreenWidth() { return SW; }
int ScreenHeight() { return SH; }
void ClearScreen() { ensure(); std::fill(fb.begin(), fb.end(), 255); }
void FillArea(int x, int y, int w, int h, int color) {
  ensure();
  unsigned char g = gray(color);
  for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++) px(i, j, g);
}
void DrawRect(int x, int y, int w, int h, int color) {
  FillArea(x, y, w, 1, color);
  FillArea(x, y + h - 1, w, 1, color);
  FillArea(x, y, 1, h, color);
  FillArea(x + w - 1, y, 1, h, color);
}
void DrawLine(int x1, int y1, int x2, int y2, int color) {
  ensure();
  int dx = std::abs(x2 - x1), sx = x1 < x2 ? 1 : -1, dy = -std::abs(y2 - y1), sy = y1 < y2 ? 1 : -1, err = dx + dy;
  for (;;) {
    px(x1, y1, gray(color));
    if (x1 == x2 && y1 == y2) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x1 += sx; }
    if (e2 <= dx) { err += dx; y1 += sy; }
  }
}
void SetClip(int x, int y, int w, int h) { clipX = x; clipY = y; clipW = w; clipH = h; }

ifont *OpenFont(const char *name, int size, int aa) {
  ensure();
  auto f = new ifont();
  memset(f, 0, sizeof(*f));
  f->name = strdup(name);
  f->size = size;
  std::string n = name;
  const char *path = n.find("Bold") != std::string::npos ? "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf"
                                                          : "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf";
  FT_Face face;
  FT_New_Face(ft, path, 0, &face);
  FT_Set_Pixel_Sizes(face, 0, size);
  faces[f] = face;
  f->height = static_cast<int>(face->size->metrics.height >> 6);
  f->baseline = static_cast<int>(face->size->metrics.ascender >> 6);
  return f;
}
void CloseFont(ifont *) {}
void SetFont(const ifont *font, int color) { cur = font; curColor = color; }

static std::vector<uint32_t> cps(const std::string &s) {
  std::vector<uint32_t> out;
  for (size_t i = 0; i < s.size();) {
    unsigned char c = s[i];
    uint32_t cp;
    int n;
    if (c < 0x80) { cp = c; n = 1; }
    else if ((c >> 5) == 6) { cp = c & 31; n = 2; }
    else if ((c >> 4) == 14) { cp = c & 15; n = 3; }
    else { cp = c & 7; n = 4; }
    for (int k = 1; k < n && i + k < s.size(); k++) cp = (cp << 6) | (s[i + k] & 63);
    i += n;
    out.push_back(cp);
  }
  return out;
}

static int advance(uint32_t cp) {
  FT_Face f = faces[cur];
  if (FT_Load_Char(f, cp, FT_LOAD_DEFAULT)) return 0;
  return static_cast<int>(f->glyph->advance.x >> 6);
}
static int width(const std::vector<uint32_t> &v, size_t a, size_t b) {
  int w = 0;
  for (size_t i = a; i < b; i++) w += advance(v[i]);
  return w;
}
static void drawRun(int x, int y, const std::vector<uint32_t> &v, size_t a, size_t b) {
  FT_Face f = faces[cur];
  unsigned char col = gray(curColor);
  int pen = x;
  for (size_t i = a; i < b; i++) {
    if (FT_Load_Char(f, v[i], FT_LOAD_RENDER)) continue;
    auto &bm = f->glyph->bitmap;
    int gx = pen + f->glyph->bitmap_left, gy = y + cur->baseline - f->glyph->bitmap_top;
    for (unsigned r = 0; r < bm.rows; r++)
      for (unsigned c = 0; c < bm.width; c++) {
        int a2 = bm.buffer[r * bm.pitch + c];
        if (!a2) continue;
        int X = gx + c, Y = gy + r;
        if (X < clipX || Y < clipY || X >= clipX + clipW || Y >= clipY + clipH || X < 0 || Y < 0 || X >= SW || Y >= SH) continue;
        unsigned char &d = fb[Y * SW + X];
        d = static_cast<unsigned char>((d * (255 - a2) + col * a2) / 255);
      }
    pen += static_cast<int>(f->glyph->advance.x >> 6);
  }
}

int StringWidth(const char *s) { auto v = cps(s); return width(v, 0, v.size()); }
void DrawString(int x, int y, const char *s) { auto v = cps(s); drawRun(x, y, v, 0, v.size()); }

// word wrap into lines of width w
static std::vector<std::pair<size_t, size_t>> wrap(const std::vector<uint32_t> &v, int w) {
  std::vector<std::pair<size_t, size_t>> lines;
  size_t start = 0;
  while (start <= v.size()) {
    size_t i = start, lastSpace = std::string::npos;
    int cw = 0;
    while (i < v.size() && v[i] != '\n') {
      int a = advance(v[i]);
      if (cw + a > w && i > start) break;
      if (v[i] == ' ') lastSpace = i;
      cw += a;
      i++;
    }
    if (i < v.size() && v[i] != '\n' && lastSpace != std::string::npos && lastSpace > start) {
      lines.push_back({start, lastSpace});
      start = lastSpace + 1;
    } else {
      lines.push_back({start, i});
      if (i < v.size() && v[i] == '\n') start = i + 1;
      else start = i;
      if (i >= v.size()) break;
    }
  }
  return lines;
}

int TextRectHeight(int w, const char *s, int flags) {
  auto v = cps(s);
  return static_cast<int>(wrap(v, w).size()) * cur->height;
}
int MinimalTextRectWidth(int w, const char *s) {
  auto v = cps(s);
  int m = 0;
  for (auto &l : wrap(v, w)) m = std::max(m, width(v, l.first, l.second));
  return m;
}
char *DrawTextRect(int x, int y, int w, int h, const char *s, int flags) {
  auto v = cps(s);
  auto lines = wrap(v, w);
  int lh = cur->height;
  int maxLines = std::max(1, h / lh);
  bool cut = static_cast<int>(lines.size()) > maxLines;
  if (cut) lines.resize(maxLines);
  int total = static_cast<int>(lines.size()) * lh;
  int yy = y;
  if (flags & VALIGN_MIDDLE) yy = y + (h - total) / 2;
  if (flags & VALIGN_BOTTOM) yy = y + h - total;
  for (size_t k = 0; k < lines.size(); k++) {
    auto l = lines[k];
    std::vector<uint32_t> run(v.begin() + l.first, v.begin() + l.second);
    if (cut && k + 1 == lines.size() && (flags & DOTS)) {
      while (!run.empty() && width(run, 0, run.size()) + advance(0x2026) > w) run.pop_back();
      run.push_back(0x2026);
    }
    int lw = width(run, 0, run.size());
    int xx = x;
    if (flags & ALIGN_CENTER) xx = x + (w - lw) / 2;
    if (flags & ALIGN_RIGHT) xx = x + w - lw;
    drawRun(xx, yy, run, 0, run.size());
    yy += lh;
  }
  return nullptr;
}

// bitmaps: 8-bit gray only
ibitmap *NewBitmap(int w, int h) {
  auto b = static_cast<ibitmap *>(malloc(sizeof(ibitmap) + w * h));
  b->width = w; b->height = h; b->depth = 8; b->scanline = w;
  memset(b->data, 128, w * h);
  return b;
}
ibitmap *LoadImageToFormat(const char *path, PixelFormat) {
  // test images are raw PGM
  FILE *f = fopen(path, "rb");
  if (!f) return nullptr;
  int w, h, mx;
  if (fscanf(f, "P5 %d %d %d", &w, &h, &mx) != 3) { fclose(f); return nullptr; }
  fgetc(f);
  auto b = NewBitmap(w, h);
  fread(b->data, 1, w * h, f);
  fclose(f);
  return b;
}
ibitmap *LoadJPEG(const char *, int, int, int, int, int) { return nullptr; }
ibitmap *BitmapStretchProportionally(const ibitmap *s, int w, int h) {
  double k = std::min(static_cast<double>(w) / s->width, static_cast<double>(h) / s->height);
  int nw = std::max(1, static_cast<int>(s->width * k)), nh = std::max(1, static_cast<int>(s->height * k));
  auto b = NewBitmap(nw, nh);
  for (int y = 0; y < nh; y++)
    for (int x = 0; x < nw; x++) b->data[y * nw + x] = s->data[(y * s->height / nh) * s->scanline + x * s->width / nw];
  return b;
}
void DrawBitmap(int x, int y, const ibitmap *b) {
  for (int j = 0; j < b->height; j++)
    for (int i = 0; i < b->width; i++) px(x + i, y + j, b->data[j * b->scanline + i]);
}

static void dump() {
  FILE *f = fopen(g_shotPath.c_str(), "wb");
  fprintf(f, "P5 %d %d 255\n", SW, SH);
  fwrite(fb.data(), 1, fb.size(), f);
  fclose(f);
}
void FullUpdate() { dump(); }
void PartialUpdate(int, int, int, int) { dump(); }
void DynamicUpdate(int, int, int, int) { dump(); }

// no-op stubs
void Message(int, const char *, const char *, int) {}
void Dialog(int, const char *, const char *, const char *, const char *, iv_dialoghandler) {}
void OpenMenu(imenu *, int, int, int, iv_menuhandler) {}
void OpenKeyboard(const char *, char *, int, int, iv_keyboardhandler) {}
int IsKeyboardOpened() { return 0; }
void SetHardTimer(const char *, iv_timerproc, int) {}
void CloseApp() {}
void InkViewMain(iv_handler) {}
void SetPanelType(int) {}
char *iv_get_default_font(FONT_TYPE t) { return const_cast<char *>(t == FONT_BOLD ? "LiberationSans-Bold" : "LiberationSans"); }
char *GetDeviceModel() { return const_cast<char *>("Mock"); }
char *GetSoftwareVersion() { return const_cast<char *>("0"); }
int NetConnect2(const char *, int) { return 0; }
int QueryNetwork() { return NET_CONNECTED; }
int OpenBook(const char *, const char *, int) { return 0; }
void ShowHourglass() {}
void HideHourglass() {}
