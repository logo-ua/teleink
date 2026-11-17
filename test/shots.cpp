// Renders the main screens with fake data so the layout can be checked on the build machine.
#define TG_MOCK 1
#define main app_main
#include "../src/main.cpp"
#undef main

extern std::string g_shotPath;
void mock_set_screen(int w, int h);

static object_ptr<td_api::chatNotificationSettings> ns(int muteFor = 0) {
  auto n = make_object<td_api::chatNotificationSettings>();
  n->use_default_mute_for_ = muteFor == 0;
  n->mute_for_ = muteFor;
  return n;
}

static object_ptr<td_api::formattedText> ft(const std::string &s) {
  auto t = make_object<td_api::formattedText>();
  t->text_ = s;
  return t;
}

static object_ptr<td_api::message> textMsg(int64_t chat, int64_t id, int64_t sender, bool out, int date, const std::string &s) {
  auto m = make_object<td_api::message>();
  m->id_ = id;
  m->chat_id_ = chat;
  m->is_outgoing_ = out;
  m->date_ = date;
  m->sender_id_ = make_object<td_api::messageSenderUser>(sender);
  auto c = make_object<td_api::messageText>();
  c->text_ = ft(s);
  m->content_ = std::move(c);
  return m;
}

static void addUser(int64_t id, const std::string &first, const std::string &last) {
  auto u = make_object<td_api::user>();
  u->id_ = id;
  u->first_name_ = first;
  u->last_name_ = last;
  tg.update(make_object<td_api::updateUser>(std::move(u)));
}

static void addChat(int64_t id, const std::string &title, int kind, int64_t order, object_ptr<td_api::message> last, int unread,
                    int muteFor = 0) {
  auto c = make_object<td_api::chat>();
  c->id_ = id;
  c->title_ = title;
  if (kind == 0) c->type_ = make_object<td_api::chatTypePrivate>(id);
  else if (kind == 1) c->type_ = make_object<td_api::chatTypeSupergroup>(id, false);
  else c->type_ = make_object<td_api::chatTypeSupergroup>(id, true);
  c->last_message_ = std::move(last);
  c->unread_count_ = unread;
  c->notification_settings_ = ns(muteFor);
  auto p = make_object<td_api::chatPosition>();
  p->list_ = make_object<td_api::chatListMain>();
  p->order_ = order;
  c->positions_.push_back(std::move(p));
  tg.update(make_object<td_api::updateNewChat>(std::move(c)));
}

static void writePgm(const std::string &path, int w, int h) {
  FILE *f = fopen(path.c_str(), "wb");
  fprintf(f, "P5 %d %d 255\n", w, h);
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      int v = 90 + (x * 120 / w) + ((y / 40 + x / 40) % 2) * 30;
      fputc(std::min(v, 255), f);
    }
  fclose(f);
}

static object_ptr<td_api::file> mkfile(int32_t id, int64_t size, const std::string &path, bool done, int64_t downloaded = 0) {
  auto f = make_object<td_api::file>();
  f->id_ = id;
  f->size_ = size;
  f->local_ = make_object<td_api::localFile>();
  f->local_->path_ = path;
  f->local_->is_downloading_completed_ = done;
  f->local_->is_downloading_active_ = !done && downloaded > 0;
  f->local_->downloaded_size_ = done ? size : downloaded;
  f->remote_ = make_object<td_api::remoteFile>();
  return f;
}

static void shot(const std::string &name) {
  g_shotPath = "/tmp/shots/" + name + ".pgm";
  needFull = true;
  drawScreen();
}

int main(int argc, char **argv) {
  int w = argc > 2 ? atoi(argv[1]) : 758, h = argc > 2 ? atoi(argv[2]) : 1024;
  mock_set_screen(w, h);
  mkdir("/tmp/shots", 0755);
  gDataDir = "/tmp/tgtest";
  gDownloadDir = "/tmp/tgtest/Downloads";
  util::mkdirs(gDataDir);
  initFonts();
  int now = static_cast<int>(time(nullptr));

  // --- first run: language ---
  scr = Scr::Login;
  settings.lang = -1;
  shot("00_language");
  int lng = argc > 3 ? atoi(argv[3]) : i18n::EN;
  settings.lang = lng;
  i18n::lang() = lng;
  tg.connection = "";

  // --- login screens ---
  shot("01_login_keys");
  tg.apiId = 123;
  tg.apiHash = "0123456789abcdef0123456789abcdef";
  tg.authId = td_api::authorizationStateWaitPhoneNumber::ID;
  shot("02_login_phone");
  tg.authId = td_api::authorizationStateWaitCode::ID;
  tg.codeInfo = tr(S_CODE_APP);
  tg.lastError = tr(S_ERR_CODE);
  shot("03_login_code");
  tg.lastError.clear();

  // --- chat list ---
  tg.ready = true;
  tg.myId = 1;
  tg.connection = "";
  addUser(1, "Я", "");
  addUser(2, "Олена", "Коваль");
  addUser(3, "Андрій", "Шевченко");
  addUser(4, "Марта", "");
  addChat(100, "Олена Коваль", 0, 1000, textMsg(100, 50, 2, false, now - 300, "Привіт! Ти вже дочитала ту книжку, яку я радила? 📚"), 2);
  addChat(101, "Книжковий клуб", 1, 990, textMsg(101, 70, 3, false, now - 3600, "Зустрічаємось у четвер о 19:00, не забудьте третій розділ"), 15);
  addChat(102, "Новини технологій", 2, 980, textMsg(102, 80, 0, false, now - 86400 * 2, "Вийшла нова версія прошивки для рідерів"), 134, 3600 * 24 * 365);
  addChat(1, "Я", 0, 970, textMsg(1, 90, 1, true, now - 86400 * 10, "нотатка: купити молоко"), 0);
  for (int i = 0; i < 14; i++)
    addChat(200 + i, "Чат номер " + std::to_string(i + 1), i % 2, 900 - i,
            textMsg(200 + i, 1, 4, i % 3 == 0, now - 86400 * (i + 3), "Повідомлення " + std::to_string(i)), i % 4 == 0 ? i : 0);
  tg.listComplete["main"] = true;
  scr = Scr::Chats;
  shot("04_chats");
  searchMode = true;
  searchQuery = "клуб";
  searchResults = {101};
  shot("04b_search");
  searchMode = false;
  listPage = 1;
  shot("05_chats_page2");
  listPage = 0;

  // --- a group chat ---
  tg.openChatId = 101;
  scr = Scr::Chat;
  int64_t id = 1;
  int base = now - 86400 - 3000;
  tg.msgs[id] = textMsg(101, id, 3, false, base, "Всім привіт! Нагадую, що наступна зустріч клубу в четвер.");
  id++;
  tg.msgs[id] = textMsg(101, id, 4, false, base + 60, "Супер, буду 👍");
  id++;
  {
    auto m = textMsg(101, id, 1, true, now - 4000, "А який розділ читаємо?");
    tg.msgs[id] = std::move(m);
    id++;
  }
  {
    auto m = textMsg(101, id, 3, false, now - 3900, "Третій, до сторінки 120.");
    auto r = make_object<td_api::messageReplyToMessage>();
    r->message_id_ = 3;
    m->reply_to_ = std::move(r);
    tg.msgs[id] = std::move(m);
    id++;
  }
  {
    // photo
    writePgm("/tmp/tgtest/photo.pgm", 800, 600);
    auto m = make_object<td_api::message>();
    m->id_ = id; m->chat_id_ = 101; m->date_ = now - 3800; m->sender_id_ = make_object<td_api::messageSenderUser>(4);
    auto p = make_object<td_api::messagePhoto>();
    p->photo_ = make_object<td_api::photo>();
    auto sz = make_object<td_api::photoSize>();
    sz->type_ = "x"; sz->width_ = 800; sz->height_ = 600;
    sz->photo_ = mkfile(500, 120000, "/tmp/tgtest/photo.pgm", true);
    p->photo_->sizes_.push_back(std::move(sz));
    p->caption_ = ft("Обкладинка нового видання");
    m->content_ = std::move(p);
    tg.msgs[id] = std::move(m);
    id++;
  }
  {
    // document
    auto m = make_object<td_api::message>();
    m->id_ = id; m->chat_id_ = 101; m->date_ = now - 3700; m->sender_id_ = make_object<td_api::messageSenderUser>(3);
    auto d = make_object<td_api::messageDocument>();
    d->document_ = make_object<td_api::document>();
    d->document_->file_name_ = "Розділ_3.epub";
    d->document_->document_ = mkfile(501, 2400000, "", false);
    d->caption_ = ft("Ось файл, якщо в когось немає книжки");
    m->content_ = std::move(d);
    tg.msgs[id] = std::move(m);
    id++;
  }
  {
    // document being downloaded
    auto m = make_object<td_api::message>();
    m->id_ = id; m->chat_id_ = 101; m->date_ = now - 3600; m->sender_id_ = make_object<td_api::messageSenderUser>(4);
    auto d = make_object<td_api::messageDocument>();
    d->document_ = make_object<td_api::document>();
    d->document_->file_name_ = "notes.pdf";
    d->document_->document_ = mkfile(502, 5000000, "", false, 2100000);
    m->content_ = std::move(d);
    tg.msgs[id] = std::move(m);
    id++;
  }
  shot("06_chat_group");
  chatOlder();
  shot("06b_chat_group_older");
  chatOlder();
  shot("06c_chat_group_older2");
  bottomMsgId = 0;

  // --- long channel post (taller than the screen) ---
  tg.openChatId = 102;
  tg.msgs.clear();
  std::string longText;
  for (int i = 0; i < 40; i++)
    longText += "Це абзац номер " + std::to_string(i + 1) +
                " довгого допису в каналі, щоб перевірити, як клієнт гортає повідомлення, вищі за екран.\n";
  tg.msgs[10] = textMsg(102, 10, 0, false, now - 7200, "Короткий допис перед довгим.");
  tg.msgs[10]->sender_id_ = make_object<td_api::messageSenderChat>(102);
  tg.msgs[11] = textMsg(102, 11, 0, false, now - 3600, longText);
  tg.msgs[11]->sender_id_ = make_object<td_api::messageSenderChat>(102);
  bottomMsgId = 0;
  tallOffset = 0;
  shot("07_channel_long_top");
  chatNewer();
  shot("08_channel_long_next");

  // --- channel with a sponsored message ---
  tg.msgs.clear();
  tg.msgs[10] = textMsg(102, 10, 0, false, now - 7200, "Оновлення прошивки 6.10 вже доступне для Verse та Era. Що нового: швидше відкриття PDF, нові шрифти.");
  tg.msgs[10]->sender_id_ = make_object<td_api::messageSenderChat>(102);
  tg.msgs[12] = textMsg(102, 12, 0, false, now - 600, "Огляд нових рідерів цього тижня — у наступному дописі.");
  tg.msgs[12]->sender_id_ = make_object<td_api::messageSenderChat>(102);
  {
    auto sp = make_object<td_api::sponsoredMessage>();
    sp->message_id_ = 999;
    sp->title_ = "Книгарня «Сторінка»";
    sp->button_text_ = "Відкрити канал";
    auto c = make_object<td_api::messageText>();
    c->text_ = ft("Нові надходження щотижня: художня та науково-популярна література українською.");
    sp->content_ = std::move(c);
    tg.sponsored.push_back(std::move(sp));
  }
  bottomMsgId = 0;
  tallOffset = 0;
  shot("08b_channel_sponsored");
  tg.sponsored.clear();

  // --- file picker ---
  pickDir = "/tmp/tgtest";
  FILE *f = fopen("/tmp/tgtest/Книга.fb2", "w");
  fputs("x", f);
  fclose(f);
  readPickDir();
  scr = Scr::Files;
  shot("10_files");

  // --- toast over chat list ---
  scr = Scr::Chats;
  toast("Олена Коваль: Ти вже вдома?", 100000);
  shot("11_toast");
  printf("ok\n");
  return 0;
}
