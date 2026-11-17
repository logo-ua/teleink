// UI strings in Ukrainian, English and Russian.
#pragma once

#include <cstring>
#include <string>

// X(id, Ukrainian, English, Russian)
#define TELEINK_STRINGS(X)                                                                  \
  X(CONNECTING, "Підключення…", "Connecting…", "Подключение…")                              \
  X(WAITING_NET, "Немає мережі", "No network", "Нет сети")                                   \
  X(UPDATING, "Оновлення…", "Updating…", "Обновление…")                                     \
  X(ERR_PHONE, "Невірний номер", "Invalid number", "Неверный номер")                        \
  X(ERR_CODE, "Невірний код", "Invalid code", "Неверный код")                               \
  X(ERR_CODE_EXPIRED, "Код застарів", "Code expired", "Код устарел")                        \
  X(ERR_PASSWORD, "Невірний пароль", "Wrong password", "Неверный пароль")                   \
  X(ERR_BANNED, "Номер заблоковано", "Number banned", "Номер заблокирован")                 \
  X(ERR_API, "Невірні api_id / api_hash", "Invalid api_id / api_hash", "Неверные api_id / api_hash") \
  X(ERR_FLOOD, "Забагато спроб, зачекайте", "Too many attempts, wait", "Слишком много попыток, подождите") \
  X(ERR_WRITE, "Писати заборонено", "Can't write here", "Писать запрещено")                 \
  X(ERR_DELETE, "Не можна видалити", "Can't delete", "Нельзя удалить")                      \
  X(ERR_SAVE, "Не вдалося зберегти", "Couldn't save", "Не удалось сохранить")               \
  X(ERR_IMAGE, "Не вдалося відкрити", "Couldn't open", "Не удалось открыть")                \
  X(USER, "Користувач", "User", "Пользователь")                                             \
  X(DELETED_ACCOUNT, "Видалений акаунт", "Deleted account", "Удалённый аккаунт")            \
  X(CHANNEL, "Канал", "Channel", "Канал")                                                   \
  X(FOLDER, "Папка", "Folder", "Папка")                                                     \
  X(CODE_APP, "Код у Telegram", "Code in Telegram", "Код в Telegram")                       \
  X(CODE_SMS, "Код в SMS", "Code by SMS", "Код в SMS")                                      \
  X(CODE_CALL, "Код дзвінком", "Code by call", "Код звонком")                               \
  X(CODE, "Код", "Code", "Код")                                                             \
  X(PHOTO, "Фото", "Photo", "Фото")                                                         \
  X(FILE, "Файл", "File", "Файл")                                                           \
  X(VIDEO, "Відео", "Video", "Видео")                                                       \
  X(GIF, "GIF", "GIF", "GIF")                                                               \
  X(AUDIO, "Аудіо", "Audio", "Аудио")                                                       \
  X(VOICE, "Голосове", "Voice", "Голосовое")                                                \
  X(VIDEO_NOTE, "Відеоповідомлення", "Video message", "Видеосообщение")                     \
  X(STICKER, "Стікер", "Sticker", "Стикер")                                                 \
  X(LOCATION, "Геопозиція", "Location", "Геопозиция")                                       \
  X(CONTACT, "Контакт", "Contact", "Контакт")                                               \
  X(POLL, "Опитування", "Poll", "Опрос")                                                    \
  X(VOTES, "Голосів", "Votes", "Голосов")                                                   \
  X(DICE, "Кубик", "Dice", "Кубик")                                                         \
  X(CALL, "Дзвінок", "Call", "Звонок")                                                      \
  X(VIDEO_CALL, "Відеодзвінок", "Video call", "Видеозвонок")                                \
  X(STORY, "Історія", "Story", "История")                                                   \
  X(GAME, "Гра", "Game", "Игра")                                                            \
  X(UNSUPPORTED, "Не підтримується", "Not supported", "Не поддерживается")                  \
  X(SVC_ADDED, "додано учасників", "added members", "добавлены участники")                  \
  X(SVC_JOINED, "приєднується", "joined", "присоединяется")                                 \
  X(SVC_LEFT, "виходить", "left", "выходит")                                                \
  X(SVC_TITLE, "нова назва", "new title", "новое название")                                 \
  X(SVC_PINNED, "закріплює повідомлення", "pinned a message", "закрепляет сообщение")       \
  X(SVC_CREATED, "створює групу", "created the group", "создаёт группу")                    \
  X(SVC_REGISTERED, "тепер у Telegram", "joined Telegram", "теперь в Telegram")             \
  X(SVC_PHOTO, "змінює фото", "changed the photo", "меняет фото")                           \
  X(SVC_OTHER, "службове повідомлення", "service message", "служебное сообщение")           \
  X(LOGIN, "Вхід", "Sign in", "Вход")                                                       \
  X(PHONE, "Номер телефону", "Phone number", "Номер телефона")                              \
  X(PASSWORD, "Пароль", "Password", "Пароль")                                               \
  X(HINT, "Підказка", "Hint", "Подсказка")                                                  \
  X(EMAIL, "E-mail", "E-mail", "E-mail")                                                    \
  X(NO_ACCOUNT, "Акаунта немає. Створіть його в Telegram на телефоні.", "No account. Create one in Telegram on your phone.", "Аккаунта нет. Создайте его в Telegram на телефоне.") \
  X(OTHER_NUMBER, "Інший номер", "Another number", "Другой номер")                          \
  X(CONFIRM_OTHER, "Підтвердіть вхід на іншому пристрої", "Confirm on another device", "Подтвердите вход на другом устройстве") \
  X(WAIT, "Зачекайте…", "Please wait…", "Подождите…")                                       \
  X(API_TITLE, "Ключі API", "API keys", "Ключи API")                                        \
  X(API_BODY, "my.telegram.org → API development tools\nабо файл teleink-api.txt у корені рідера", "my.telegram.org → API development tools\nor teleink-api.txt in the reader's root", "my.telegram.org → API development tools\nили файл teleink-api.txt в корне ридера") \
  X(API_BAD_HASH, "api_hash: 32 символи 0-9, a-f", "api_hash: 32 chars 0-9, a-f", "api_hash: 32 символа 0-9, a-f") \
  X(API_BAD_ID, "api_id — число", "api_id is a number", "api_id — число")                   \
  X(API_RESTART, "Збережено. Відкрийте програму знову.", "Saved. Open the app again.", "Сохранено. Откройте программу снова.") \
  X(STARTING, "Запуск…", "Starting…", "Запуск…")                                            \
  X(CHATS, "Чати", "Chats", "Чаты")                                                         \
  X(ARCHIVE, "Архів", "Archive", "Архив")                                                   \
  X(ALL_CHATS, "Усі чати", "All chats", "Все чаты")                                         \
  X(SAVED, "Збережене", "Saved Messages", "Избранное")                                      \
  X(YOU, "Ви", "You", "Вы")                                                                 \
  X(DRAFT, "Чернетка", "Draft", "Черновик")                                                 \
  X(NOTHING, "Порожньо", "Empty", "Пусто")                                                  \
  X(SEARCH, "Пошук", "Search", "Поиск")                                                     \
  X(NOTIFICATIONS, "Сповіщення", "Notifications", "Уведомления")                            \
  X(AUTO_PHOTOS, "Фото автоматично", "Auto-load photos", "Фото автоматически")              \
  X(LANGUAGE, "Українська", "English", "Русский")                                           \
  X(LANG_LABEL, "Мова", "Language", "Язык")                                           \
  X(REFRESH, "Оновити екран", "Refresh screen", "Обновить экран")                           \
  X(ABOUT, "Про програму", "About", "О программе")                                          \
  X(ABOUT_TEXT, "Неофіційний клієнт Telegram.\nTelegram API · TDLib.\n\nФайли: Downloads/Teleink", "Unofficial Telegram client.\nTelegram API · TDLib.\n\nFiles: Downloads/Teleink", "Неофициальный клиент Telegram.\nTelegram API · TDLib.\n\nФайлы: Downloads/Teleink") \
  X(LOG_OUT, "Вийти з акаунта", "Log out", "Выйти из аккаунта")                             \
  X(LOG_OUT_Q, "Вийти з акаунта?", "Log out?", "Выйти из аккаунта?")                        \
  X(QUIT, "Закрити", "Close", "Закрыть")                                                    \
  X(YES, "Так", "Yes", "Да")                                                                \
  X(NO, "Ні", "No", "Нет")                                                                  \
  X(MARK_READ, "Прочитано", "Mark as read", "Прочитано")                                    \
  X(MUTE, "Без звуку", "Mute", "Без звука")                                                 \
  X(UNMUTE, "Зі звуком", "Unmute", "Со звуком")                                             \
  X(FORWARDED, "Переслано", "Forwarded", "Переслано")                                       \
  X(EDITED, "ред.", "edited", "ред.")                                                       \
  X(SPONSORED, "Реклама", "Ad", "Реклама")                                                  \
  X(RECOMMENDED, "Рекомендовано", "Recommended", "Рекомендовано")                           \
  X(SPONSORED_INFO, "Спонсоровано Telegram", "Sponsored by Telegram", "Спонсировано Telegram") \
  X(MESSAGE, "Повідомлення", "Message", "Сообщение")                                        \
  X(START_OF_CHAT, "Початок чату", "Start of chat", "Начало чата")                          \
  X(REPLY, "Відповісти", "Reply", "Ответить")                                               \
  X(DOWNLOAD, "Завантажити", "Download", "Скачать")                                         \
  X(OPEN, "Відкрити", "Open", "Открыть")                                                    \
  X(DELETE, "Видалити", "Delete", "Удалить")                                                \
  X(DELETE_Q, "Видалити?", "Delete?", "Удалить?")                                           \
  X(CANCEL_DL_Q, "Скасувати завантаження?", "Cancel download?", "Отменить загрузку?")       \
  X(SAVED_TO, "Збережено", "Saved", "Сохранено")                                            \
  X(NOT_SUPPORTED_FILE, "Збережено в Downloads/Teleink", "Saved to Downloads/Teleink", "Сохранено в Downloads/Teleink") \
  X(SEND_FILE, "Надіслати файл", "Send file", "Отправить файл")                             \
  X(SEND_Q, "Надіслати?", "Send?", "Отправить?")                                            \
  X(SENDING, "Надсилання…", "Sending…", "Отправка…")                                        \
  X(STORAGE, "Пам'ять", "Storage", "Память")                                                \
  X(SD, "SD", "SD", "SD")                                                                   \
  X(TODAY, "Сьогодні", "Today", "Сегодня")                                                  \
  X(YESTERDAY, "Вчора", "Yesterday", "Вчера")                                               \
  X(ICON_RESTART, "Іконка з'явиться після перезавантаження рідера", "The icon appears after restarting the reader", "Иконка появится после перезагрузки ридера") \
  X(READER_HINT, "Файл відкриється в читалці рідера. Щоб повернутися в Teleink, торкніться середини сторінки (або кнопки «Меню») і виберіть «Назад».", "The file opens in the reader. To come back to Teleink, tap the middle of the page (or the Menu button) and choose Back.", "Файл откроется в читалке ридера. Чтобы вернуться в Teleink, коснитесь середины страницы (или кнопки «Меню») и выберите «Назад».") \
  X(CLEAR_CACHE, "Очистити кеш", "Clear cache", "Очистить кэш") \
  X(CLEAR_CACHE_Q, "Очистити кеш? Збережені файли в Downloads/Teleink залишаться.", "Clear the cache? Files saved in Downloads/Teleink stay.", "Очистить кэш? Сохранённые файлы в Downloads/Teleink останутся.") \
  X(CACHE_FREED, "Звільнено", "Freed", "Освобождено") \
  X(ON, "увімк.", "on", "вкл.") \
  X(OFF, "вимк.", "off", "выкл.") \
  X(EMOJI, "(емодзі)", "(emoji)", "(эмодзи)")

#define TELEINK_ENUM(id, uk, en, ru) S_##id,
enum Str { TELEINK_STRINGS(TELEINK_ENUM) S_COUNT };
#undef TELEINK_ENUM

namespace i18n {

enum Lang { UK = 0, EN = 1, RU = 2 };

inline int &lang() {
  static int l = UK;
  return l;
}

inline const char *tr(::Str s) {
#define TELEINK_ROW(id, uk, en, ru) {uk, en, ru},
  static const char *table[][3] = {TELEINK_STRINGS(TELEINK_ROW)};
#undef TELEINK_ROW
  return table[s][lang()];
}

// Maps the firmware language code to one of ours.
inline int fromSystem(const char *code) {
  if (!code) return EN;
  if (!strncmp(code, "uk", 2) || !strncmp(code, "ua", 2)) return UK;
  if (!strncmp(code, "ru", 2)) return RU;
  return EN;
}

inline const char *weekday(int wday) {
  static const char *d[3][7] = {{"Нд", "Пн", "Вт", "Ср", "Чт", "Пт", "Сб"},
                                {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"},
                                {"Вс", "Пн", "Вт", "Ср", "Чт", "Пт", "Сб"}};
  return d[lang()][wday % 7];
}

inline const char *month(int m) {
  static const char *ms[3][12] = {
      {"січня", "лютого", "березня", "квітня", "травня", "червня", "липня", "серпня", "вересня", "жовтня", "листопада", "грудня"},
      {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"},
      {"января", "февраля", "марта", "апреля", "мая", "июня", "июля", "августа", "сентября", "октября", "ноября", "декабря"}};
  return ms[lang()][m % 12];
}

inline const char *sizeUnit(int i) {  // 0 B, 1 KB, 2 MB, 3 GB
  static const char *u[3][4] = {{"Б", "КБ", "МБ", "ГБ"}, {"B", "KB", "MB", "GB"}, {"Б", "КБ", "МБ", "ГБ"}};
  return u[lang()][i];
}

}  // namespace i18n

using i18n::tr;
