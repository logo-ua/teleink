// Renders presentation screenshots with realistic demo data (same harness as shots.cpp).
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
  g_shotPath = "/tmp/promo/shot_" + name + ".pgm";
  needFull = true;
  drawScreen();
}


static void setPhoto(int64_t cid, const char *pgm) {
  auto c = tg.chat(cid);
  c->photo_ = make_object<td_api::chatPhotoInfo>();
  static int32_t fid = 9000;
  c->photo_->small_ = mkfile(fid++, 5000, std::string("/tmp/promo/") + pgm + ".pgm", true);
}

static object_ptr<td_api::message> post(int64_t chat, int64_t id, int date, const std::string &s) {
  auto m = textMsg(chat, id, 0, false, date, s);
  m->sender_id_ = make_object<td_api::messageSenderChat>(chat);
  return m;
}

static object_ptr<td_api::message> doc(int64_t chat, int64_t id, int64_t sender, bool out, int date, const std::string &name,
                                       int64_t size, int64_t got, bool done, const std::string &caption) {
  auto m = make_object<td_api::message>();
  m->id_ = id; m->chat_id_ = chat; m->date_ = date; m->is_outgoing_ = out;
  if (sender) m->sender_id_ = make_object<td_api::messageSenderUser>(sender);
  else m->sender_id_ = make_object<td_api::messageSenderChat>(chat);
  auto d = make_object<td_api::messageDocument>();
  d->document_ = make_object<td_api::document>();
  d->document_->file_name_ = name;
  d->document_->document_ = mkfile(static_cast<int32_t>(7000 + id), size, done ? "/tmp/promo/x" : "", done, got);
  d->caption_ = ft(caption);
  m->content_ = std::move(d);
  return m;
}

int main(int argc, char **argv) {
  int w = argc > 2 ? atoi(argv[1]) : 1072, h = argc > 2 ? atoi(argv[2]) : 1448;
  int lng = argc > 3 ? atoi(argv[3]) : i18n::EN;
  mock_set_screen(w, h);
  gDataDir = "/tmp/promo/data";
  gDownloadDir = "/tmp/promo/data/Downloads";
  util::mkdirs(gDataDir);
  initFonts();
  // a fixed afternoon so the times look natural
  struct tm t0 = {};
  t0.tm_year = 126; t0.tm_mon = 10; t0.tm_mday = 17; t0.tm_hour = 14; t0.tm_min = 32; t0.tm_isdst = -1;
  int now = static_cast<int>(mktime(&t0));
  const bool en = lng == i18n::EN, uk = lng == i18n::UK;
  auto L = [&](const char *e, const char *u, const char *r) { return std::string(en ? e : uk ? u : r); };

  scr = Scr::Login;
  settings.lang = -1;
  shot("1_language");
  settings.lang = lng;
  i18n::lang() = lng;
  tg.connection = "";
  tg.apiId = 1;
  tg.apiHash = "x";
  tg.authId = td_api::authorizationStateWaitPhoneNumber::ID;
  shot("2_login");

  tg.ready = true;
  tg.myId = 1;
  addUser(1, "Me", "");
  addUser(2, "Olivia", "Bennett");
  addUser(3, "Marco", "Rossi");
  addUser(4, "Sofia", "Lind");
  addUser(5, "Daniel", "Park");
  addUser(6, "Anna", "Kovalenko");

  addChat(100, "Olivia Bennett", 0, 1000, textMsg(100, 40, 2, false, now - 240, L("Did you finish the book I sent? Chapter 12 is wild", "Ти дочитала книжку, яку я скинула? 12 розділ — це щось", "Ты дочитала книгу, которую я скинула? 12 глава — это что-то")), 2);
  addChat(101, L("Book Club", "Книжковий клуб", "Книжный клуб"), 1, 990, textMsg(101, 70, 3, false, now - 1500, L("Ok, chapter three for Thursday", "Отже, на четвер третій розділ", "Итак, к четвергу третья глава")), 5);
  addChat(102, "E-Ink Weekly", 2, 980, post(102, 80, now - 3700, L("Firmware 6.10 is out: faster PDF rendering and new fonts", "Вийшла прошивка 6.10: швидші PDF і нові шрифти", "Вышла прошивка 6.10: быстрые PDF и новые шрифты")), 12, 3600 * 24 * 365);
  addChat(1, "Me", 0, 975, doc(1, 90, 1, true, now - 7200, "Dune - Frank Herbert.epub", 1830000, 0, true, ""), 0);
  addChat(103, "Marco Rossi", 0, 970, textMsg(103, 50, 1, true, now - 86400 + 3000, L("Thanks, got the PDF!", "Дякую, PDF отримав!", "Спасибо, PDF получил!")), 0);
  addChat(104, "Sofia Lind", 0, 960, textMsg(104, 51, 4, false, now - 86400 * 2, L("See you on Saturday at the library", "Побачимось у суботу в бібліотеці", "Увидимся в субботу в библиотеке")), 0);
  addChat(105, L("Daily Reads", "Щоденне читання", "Ежедневное чтение"), 2, 950, post(105, 52, now - 86400 * 2 - 5000, L("Today: 5 short stories you can read in 20 minutes", "Сьогодні: 5 оповідань, які можна прочитати за 20 хвилин", "Сегодня: 5 рассказов, которые можно прочитать за 20 минут")), 3, 3600 * 24 * 365);
  addChat(106, "Daniel Park", 0, 940, textMsg(106, 53, 5, false, now - 86400 * 4, L("Sending the scans tonight", "Скину скани ввечері", "Скину сканы вечером")), 0);
  addChat(107, "Anna Kovalenko", 0, 930, textMsg(107, 54, 1, true, now - 86400 * 6, L("Happy birthday! 🎉", "З днем народження! 🎉", "С днём рождения! 🎉")), 0);
  addChat(108, L("Family", "Сім'я", "Семья"), 1, 920, textMsg(108, 55, 6, false, now - 86400 * 8, L("Dinner on Sunday?", "Вечеря в неділю?", "Ужин в воскресенье?")), 0);
  for (int k = 0; k < 3; k++) {
    auto c = make_object<td_api::chat>();
    c->id_ = 300 + k;
    c->title_ = std::vector<std::string>{"Old project", "Store news", "Travel 2025"}[k];
    c->type_ = make_object<td_api::chatTypePrivate>(300 + k);
    c->last_message_ = textMsg(300 + k, 1, 5, false, now - 86400 * 40, "…");
    c->unread_count_ = k == 1 ? 4 : 0;
    c->notification_settings_ = ns(0);
    auto p = make_object<td_api::chatPosition>();
    p->list_ = make_object<td_api::chatListArchive>();
    p->order_ = 500 - k;
    c->positions_.push_back(std::move(p));
    tg.update(make_object<td_api::updateNewChat>(std::move(c)));
  }
  setPhoto(100, "ava_olivia");
  setPhoto(101, "ava_bookclub");
  setPhoto(102, "ava_einkweekly");
    setPhoto(104, "ava_flower");
  setPhoto(105, "ava_daily");
  tg.listComplete["main"] = true;
  scr = Scr::Chats;
  listKey = "main";
  listPage = 0;
  shot("3_chats");

  // group chat
  tg.openChatId = 101;
  scr = Scr::Chat;
  int64_t id = 1;
  tg.msgs[id] = textMsg(101, id, 3, false, now - 5400, L("Hi all! Next meeting is Thursday at 7 pm.", "Всім привіт! Зустріч у четвер о 19:00.", "Всем привет! Встреча в четверг в 19:00.")); id++;
  tg.msgs[id] = textMsg(101, id, 1, true, now - 5000, L("Which chapter are we reading?", "Який розділ читаємо?", "Какую главу читаем?")); id++;
  {
    auto m = textMsg(101, id, 3, false, now - 4900, L("Chapter three, up to page 120.", "Третій, до сторінки 120.", "Третью, до страницы 120."));
    auto r = make_object<td_api::messageReplyToMessage>();
    r->message_id_ = 2;
    m->reply_to_ = std::move(r);
    tg.msgs[id] = std::move(m); id++;
  }
  tg.msgs[id] = textMsg(101, id, 4, false, now - 3000, L("Perfect, I'll bring tea and cookies", "Супер, я принесу чай і печиво", "Отлично, я принесу чай и печенье")); id++;
  tg.msgs[id] = doc(101, id, 3, false, now - 1700, "The Midnight Library.epub", 2400000, 0, false, L("Here's the book if you don't have it", "Ось книжка, якщо в когось немає", "Вот книга, если у кого-то нет")); id++;
  tg.msgs[id] = doc(101, id, 6, false, now - 1600, "Discussion questions.pdf", 860000, 0, true, ""); id++;
  shot("4_chat_group");

  // channel
  tg.openChatId = 102;
  tg.msgs.clear();
  bottomMsgId = 0;
  tallOffset = 0;
  tg.msgs[9] = post(102, 9, now - 86400 * 2, L("Tip: hold the page-turn button to jump ten pages at once.", "Порада: затисніть кнопку гортання, щоб перескочити на десять сторінок.", "Совет: зажмите кнопку листания, чтобы перескочить на десять страниц."));
  tg.msgs[10] = post(102, 10, now - 86400 - 2000, L("This week we tested three 7-inch readers side by side. Full review is in the PDF below.",
                                                   "Цього тижня ми порівняли три 7-дюймові рідери. Повний огляд — у PDF нижче.",
                                                   "На этой неделе мы сравнили три 7-дюймовых ридера. Полный обзор — в PDF ниже."));
  tg.msgs[11] = doc(102, 11, 0, false, now - 86400 - 1900, "E-Ink Weekly #47.pdf", 5400000, 2300000, false, "");
  tg.msgs[12] = post(102, 12, now - 3700, L("Firmware 6.10 is out for Verse, Verse Pro and Era.\n\nWhat's new:\n• faster PDF rendering\n• two new reading fonts\n• better Wi-Fi reconnect after sleep\n\nUpdate via Settings → Software.",
                                          "Вийшла прошивка 6.10 для Verse, Verse Pro та Era.\n\nЩо нового:\n• швидше відкриття PDF\n• два нові шрифти для читання\n• стабільніший Wi-Fi після сну\n\nОновлення: Налаштування → Програмне забезпечення.",
                                          "Вышла прошивка 6.10 для Verse, Verse Pro и Era.\n\nЧто нового:\n• быстрое открытие PDF\n• два новых шрифта для чтения\n• стабильный Wi-Fi после сна\n\nОбновление: Настройки → ПО."));
  shot("5_channel");

  // search
  tg.openChatId = 0;
  scr = Scr::Chats;
  searchMode = true;
  searchQuery = L("read", "чит", "чит");
  searchResults = {105, 101, 100};
  shot("6_search");
  searchMode = false;

  // archive
  listKey = "archive";
  shot("7_archive");
  listKey = "main";
  printf("ok\n");
  return 0;
}
