// Telegram state kept in sync with TDLib updates.
#pragma once

#include <td/telegram/Client.h>
#include <td/telegram/td_api.h>
#include <td/telegram/td_api.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "util.h"

namespace td_api = td::td_api;
using td_api::object_ptr;
using td_api::make_object;
using td_api::move_object_as;

struct FolderInfo {
  int32_t id;
  std::string name;
};

class Tg {
 public:
  using Handler = std::function<void(object_ptr<td_api::Object>)>;

  // configuration (filled by the app before start())
  std::string dataDir;
  int32_t apiId = 0;
  std::string apiHash;

  // auth
  int32_t authId = 0;  // td_api::AuthorizationState::ID of the current state
  std::string passwordHint;
  std::string codeInfo;
  std::string qrLink;
  bool ready = false;
  bool closed = false;
  bool closing = false;
  bool waitingForParams = false;
  int64_t myId = 0;

  std::string connection = tr(S_CONNECTING);
  std::string lastError;

  // data
  std::unordered_map<int64_t, object_ptr<td_api::chat>> chats;
  std::unordered_map<int64_t, object_ptr<td_api::user>> users;
  std::map<std::string, std::set<std::pair<int64_t, int64_t>>> lists;  // list key -> {(order, chat id)}
  std::map<std::string, bool> listComplete;
  std::map<std::string, bool> listLoading;
  std::unordered_map<int32_t, object_ptr<td_api::file>> files;
  std::vector<FolderInfo> folders;

  // currently open chat
  int64_t openChatId = 0;
  std::map<int64_t, object_ptr<td_api::message>> msgs;
  bool historyLoading = false;
  bool historyEnd = false;
  // after jumping to a found message the loaded window may not reach the newest messages
  bool detached = false;
  bool newerLoading = false;
  // sponsored messages of the open channel (required by the Telegram API terms)
  std::vector<object_ptr<td_api::sponsoredMessage>> sponsored;
  std::set<int64_t> sponsoredViewed;

  // change flags for the UI
  bool dirtyChats = false;
  bool dirtyMsgs = false;
  bool dirtyAuth = false;
  bool dirtyStatus = false;
  std::set<int32_t> dirtyFiles;

  std::function<void(const td_api::message &)> onIncoming;
  std::function<void(const td_api::file &)> onFileDone;
  std::function<void(const std::string &)> onError;

  void start() {
#ifndef TG_MOCK
    td::ClientManager::execute(make_object<td_api::setLogVerbosityLevel>(1));
    auto stream = make_object<td_api::setLogStream>();
    auto file = make_object<td_api::logStreamFile>();
    file->path_ = dataDir + "/tdlib.log";
    file->max_file_size_ = 1 << 20;
    file->redirect_stderr_ = false;
    stream->log_stream_ = std::move(file);
    td::ClientManager::execute(std::move(stream));
    mgr_ = std::make_unique<td::ClientManager>();
    client_ = mgr_->create_client_id();
    send(make_object<td_api::getOption>("version"));
#endif
  }

  // Creates a fresh client after log out (TDLib closes the old instance).
  void restart() {
#ifdef TG_MOCK
    return;
#endif
    if (!mgr_) return;
    authId = 0;
    ready = closed = closing = waitingForParams = false;
    myId = 0;
    chats.clear();
    users.clear();
    lists.clear();
    listComplete.clear();
    listLoading.clear();
    files.clear();
    folders.clear();
    openChatId = 0;
    msgs.clear();
    handlers_.clear();
    client_ = mgr_->create_client_id();
    send(make_object<td_api::getOption>("version"));
    dirtyAuth = dirtyChats = true;
  }

  void send(object_ptr<td_api::Function> f, Handler h = nullptr) {
#ifdef TG_MOCK
    return;
#endif
    if (!mgr_) return;
    uint64_t id = nextId_++;
    if (h) handlers_.emplace(id, std::move(h));
    mgr_->send(client_, id, std::move(f));
  }

  // Processes everything TDLib has ready. Returns number of processed responses.
  int poll(double timeout = 0.0, int maxCount = 300) {
#ifdef TG_MOCK
    return 0;
#endif
    if (!mgr_) return 0;
    int n = 0;
    while (n < maxCount) {
      auto r = mgr_->receive(n == 0 ? timeout : 0.0);
      if (!r.object) break;
      n++;
      if (r.request_id == 0) {
        update(std::move(r.object));
        continue;
      }
      auto it = handlers_.find(r.request_id);
      if (it != handlers_.end()) {
        auto h = std::move(it->second);
        handlers_.erase(it);
        h(std::move(r.object));
      } else if (r.object->get_id() == td_api::error::ID) {
        auto &e = static_cast<td_api::error &>(*r.object);
        reportError(e);
      }
    }
    return n;
  }

  void reportError(const td_api::error &e) {
    util::log("TDLib error %d: %s", e.code_, e.message_.c_str());
    lastError = humanError(e.message_);
    if (onError) onError(lastError);
  }

  static std::string humanError(const std::string &m) {
    if (m == "PHONE_NUMBER_INVALID") return tr(S_ERR_PHONE);
    if (m == "PHONE_CODE_INVALID") return tr(S_ERR_CODE);
    if (m == "PHONE_CODE_EXPIRED") return tr(S_ERR_CODE_EXPIRED);
    if (m == "PASSWORD_HASH_INVALID") return tr(S_ERR_PASSWORD);
    if (m == "PHONE_NUMBER_BANNED") return tr(S_ERR_BANNED);
    if (m == "API_ID_INVALID" || m == "API_ID_PUBLISHED_FLOOD") return tr(S_ERR_API);
    if (m.rfind("FLOOD_WAIT", 0) == 0 || m.find("Too Many Requests") != std::string::npos)
      return tr(S_ERR_FLOOD);
    if (m == "CHAT_WRITE_FORBIDDEN") return tr(S_ERR_WRITE);
    if (m == "MESSAGE_DELETE_FORBIDDEN") return tr(S_ERR_DELETE);
    return m;
  }

  void close() {
    if (!mgr_ || closing) return;
    closing = true;
    send(make_object<td_api::close>());
  }

  // ---- auth ----
  void sendParameters() {
    auto p = make_object<td_api::setTdlibParameters>();
    p->use_test_dc_ = false;
    p->database_directory_ = dataDir + "/db";
    p->files_directory_ = dataDir + "/files";
    p->use_file_database_ = true;
    p->use_chat_info_database_ = true;
    p->use_message_database_ = true;
    p->use_secret_chats_ = false;
    p->api_id_ = apiId;
    p->api_hash_ = apiHash;
    p->system_language_code_ = "uk";
    p->device_model_ = "PocketBook";
    p->system_version_ = "Linux";
    p->application_version_ = "Teleink 1.0";
    waitingForParams = false;
    send(std::move(p), [this](object_ptr<td_api::Object> o) {
      if (o->get_id() == td_api::error::ID) reportError(static_cast<td_api::error &>(*o));
    });
  }

  void sendPhone(const std::string &phone) {
    auto f = make_object<td_api::setAuthenticationPhoneNumber>();
    f->phone_number_ = phone;
    send(std::move(f), authHandler());
  }
  void sendCode(const std::string &code) {
    send(make_object<td_api::checkAuthenticationCode>(code), authHandler());
  }
  void sendPassword(const std::string &pw) {
    send(make_object<td_api::checkAuthenticationPassword>(pw), authHandler());
  }
  void sendEmail(const std::string &email) {
    send(make_object<td_api::setAuthenticationEmailAddress>(email), authHandler());
  }
  void sendEmailCode(const std::string &code) {
    auto f = make_object<td_api::checkAuthenticationEmailCode>();
    f->code_ = make_object<td_api::emailAddressAuthenticationCode>(code);
    send(std::move(f), authHandler());
  }
  void requestQr() {
    send(make_object<td_api::requestQrCodeAuthentication>(), authHandler());
  }
  void logOut() { send(make_object<td_api::logOut>()); }

  // ---- chats ----
  static std::string listKey(const td_api::ChatList *l) {
    if (!l) return "";
    switch (l->get_id()) {
      case td_api::chatListMain::ID: return "main";
      case td_api::chatListArchive::ID: return "archive";
      case td_api::chatListFolder::ID:
        return "f" + std::to_string(static_cast<const td_api::chatListFolder *>(l)->chat_folder_id_);
    }
    return "";
  }

  static object_ptr<td_api::ChatList> makeList(const std::string &key) {
    if (key == "archive") return make_object<td_api::chatListArchive>();
    if (!key.empty() && key[0] == 'f') return make_object<td_api::chatListFolder>(std::stoi(key.substr(1)));
    return make_object<td_api::chatListMain>();
  }

  void loadChats(const std::string &key, int limit = 40) {
    if (listLoading[key] || listComplete[key] || !ready) return;
    listLoading[key] = true;
    auto f = make_object<td_api::loadChats>();
    f->chat_list_ = makeList(key);
    f->limit_ = limit;
    send(std::move(f), [this, key](object_ptr<td_api::Object> o) {
      listLoading[key] = false;
      if (o->get_id() == td_api::error::ID) {
        auto &e = static_cast<td_api::error &>(*o);
        if (e.code_ == 404) listComplete[key] = true;
        else reportError(e);
      }
      dirtyChats = true;
    });
  }

  std::vector<int64_t> chatIds(const std::string &key) const {
    std::vector<int64_t> out;
    auto it = lists.find(key);
    if (it == lists.end()) return out;
    for (auto r = it->second.rbegin(); r != it->second.rend(); ++r) out.push_back(r->second);
    return out;
  }

  td_api::chat *chat(int64_t id) {
    auto it = chats.find(id);
    return it == chats.end() ? nullptr : it->second.get();
  }
  td_api::user *user(int64_t id) {
    auto it = users.find(id);
    return it == users.end() ? nullptr : it->second.get();
  }

  std::string userName(int64_t id) {
    auto u = user(id);
    if (!u) return tr(S_USER);
    std::string n = u->first_name_;
    if (!u->last_name_.empty()) n += (n.empty() ? "" : " ") + u->last_name_;
    if (n.empty()) n = tr(S_DELETED_ACCOUNT);
    return util::clean(n, true);
  }

  std::string senderName(const td_api::MessageSender *s) {
    if (!s) return "";
    if (s->get_id() == td_api::messageSenderUser::ID)
      return userName(static_cast<const td_api::messageSenderUser *>(s)->user_id_);
    auto c = chat(static_cast<const td_api::messageSenderChat *>(s)->chat_id_);
    return c ? util::clean(c->title_, true) : tr(S_CHANNEL);
  }

  int64_t senderUserId(const td_api::MessageSender *s) {
    if (s && s->get_id() == td_api::messageSenderUser::ID)
      return static_cast<const td_api::messageSenderUser *>(s)->user_id_;
    return 0;
  }

  bool isMuted(const td_api::chat &c) {
    return c.notification_settings_ && !c.notification_settings_->use_default_mute_for_ &&
           c.notification_settings_->mute_for_ > 0;
  }

  bool isGroup(const td_api::chat &c) {
    if (!c.type_) return false;
    if (c.type_->get_id() == td_api::chatTypeBasicGroup::ID) return true;
    if (c.type_->get_id() == td_api::chatTypeSupergroup::ID)
      return !static_cast<const td_api::chatTypeSupergroup &>(*c.type_).is_channel_;
    return false;
  }

  bool isChannel(const td_api::chat &c) {
    return c.type_ && c.type_->get_id() == td_api::chatTypeSupergroup::ID &&
           static_cast<const td_api::chatTypeSupergroup &>(*c.type_).is_channel_;
  }

  // ---- open chat & history ----
  void openChat(int64_t id) {
    if (openChatId) closeChat();
    openChatId = id;
    msgs.clear();
    historyEnd = false;
    historyLoading = false;
    detached = newerLoading = false;
    send(make_object<td_api::openChat>(id));
    loadHistory(0, 30);
    loadSponsored();
  }

  void loadSponsored() {
    sponsored.clear();
    sponsoredViewed.clear();
    auto c = chat(openChatId);
    if (!c || !c->type_) return;
    bool eligible = isChannel(*c) || c->type_->get_id() == td_api::chatTypePrivate::ID;  // channels and bots
    if (!eligible) return;
    int64_t chatId = openChatId;
    send(make_object<td_api::getChatSponsoredMessages>(chatId), [this, chatId](object_ptr<td_api::Object> o) {
      if (chatId != openChatId || o->get_id() != td_api::sponsoredMessages::ID) return;
      auto sm = move_object_as<td_api::sponsoredMessages>(o);
      sponsored = std::move(sm->messages_);
      if (!sponsored.empty()) dirtyMsgs = true;
    });
  }

  void viewSponsored(int64_t messageId) {
    if (!openChatId || sponsoredViewed.count(messageId)) return;
    sponsoredViewed.insert(messageId);
    viewMessages({messageId});
  }

  void clickSponsored(int64_t messageId) {
    auto f = make_object<td_api::clickChatSponsoredMessage>();
    f->chat_id_ = openChatId;
    f->message_id_ = messageId;
    send(std::move(f));
  }

  void closeChat() {
    if (!openChatId) return;
    send(make_object<td_api::closeChat>(openChatId));
    openChatId = 0;
    msgs.clear();
    sponsored.clear();
  }

  void loadOlder() {
    if (historyLoading || historyEnd || !openChatId) return;
    int64_t from = msgs.empty() ? 0 : msgs.begin()->first;
    loadHistory(from, 40);
  }

  void loadHistory(int64_t fromId, int limit, int attempt = 0) {
    if (!openChatId) return;
    historyLoading = true;
    int64_t chatId = openChatId;
    auto f = make_object<td_api::getChatHistory>();
    f->chat_id_ = chatId;
    f->from_message_id_ = fromId;
    f->offset_ = 0;
    f->limit_ = limit;
    f->only_local_ = false;
    send(std::move(f), [this, chatId, fromId, limit, attempt](object_ptr<td_api::Object> o) {
      if (chatId != openChatId) return;
      historyLoading = false;
      if (o->get_id() == td_api::error::ID) {
        reportError(static_cast<td_api::error &>(*o));
        return;
      }
      auto m = move_object_as<td_api::messages>(o);
      size_t got = m->messages_.size();
      int64_t oldest = fromId;
      for (auto &msg : m->messages_) {
        if (!msg) continue;
        if (oldest == 0 || msg->id_ < oldest) oldest = msg->id_;
        msgs[msg->id_] = std::move(msg);
      }
      dirtyMsgs = true;
      if (got == 0) {
        historyEnd = true;
        return;
      }
      // TDLib often returns only what is cached locally at first; keep asking until the page is full.
      if (static_cast<int>(got) < limit && attempt < 4) loadHistory(oldest, limit - static_cast<int>(got), attempt + 1);
    });
  }

  // Loads the messages around msgId (replacing what is loaded) and calls done when they are in.
  void jumpTo(int64_t msgId, std::function<void()> done) {
    if (!openChatId) return;
    int64_t chatId = openChatId;
    msgs.clear();
    historyEnd = false;
    detached = true;
    historyLoading = true;
    auto f = make_object<td_api::getChatHistory>();
    f->chat_id_ = chatId;
    f->from_message_id_ = msgId;
    f->offset_ = -15;
    f->limit_ = 30;
    send(std::move(f), [this, chatId, done](object_ptr<td_api::Object> o) {
      if (chatId != openChatId) return;
      historyLoading = false;
      if (o->get_id() == td_api::messages::ID) {
        for (auto &m : static_cast<td_api::messages &>(*o).messages_)
          if (m) msgs[m->id_] = std::move(m);
      }
      checkAttached();
      dirtyMsgs = true;
      if (done) done();
    });
  }

  // Back to the newest messages after a jump (e.g. before sending).
  void reattach() {
    if (!detached || !openChatId) return;
    msgs.clear();
    detached = newerLoading = false;
    historyEnd = false;
    loadHistory(0, 30);
  }

  // Loads messages newer than the newest loaded one (only needed after a jump).
  void loadNewer() {
    if (!openChatId || !detached || newerLoading || msgs.empty()) return;
    newerLoading = true;
    int64_t chatId = openChatId, from = msgs.rbegin()->first;
    auto f = make_object<td_api::getChatHistory>();
    f->chat_id_ = chatId;
    f->from_message_id_ = from;
    f->offset_ = -30;
    f->limit_ = 30;
    send(std::move(f), [this, chatId, from](object_ptr<td_api::Object> o) {
      if (chatId != openChatId) return;
      newerLoading = false;
      bool gotNewer = false;
      if (o->get_id() == td_api::messages::ID) {
        for (auto &m : static_cast<td_api::messages &>(*o).messages_) {
          if (!m) continue;
          if (m->id_ > from) gotNewer = true;
          msgs[m->id_] = std::move(m);
        }
      }
      if (!gotNewer) detached = false;
      checkAttached();
      dirtyMsgs = true;
    });
  }

  void searchMessages(const std::string &query, std::function<void(std::vector<object_ptr<td_api::message>>)> done) {
    if (!openChatId) return;
    auto f = make_object<td_api::searchChatMessages>();
    f->chat_id_ = openChatId;
    f->query_ = query;
    f->from_message_id_ = 0;
    f->offset_ = 0;
    f->limit_ = 100;
    send(std::move(f), [done](object_ptr<td_api::Object> o) {
      std::vector<object_ptr<td_api::message>> out;
      if (o->get_id() == td_api::foundChatMessages::ID)
        for (auto &m : static_cast<td_api::foundChatMessages &>(*o).messages_)
          if (m) out.push_back(std::move(m));
      done(std::move(out));
    });
  }

  void viewMessages(const std::vector<int64_t> &ids) {
    if (!openChatId || ids.empty()) return;
    auto f = make_object<td_api::viewMessages>();
    f->chat_id_ = openChatId;
    f->message_ids_ = ids;
    f->force_read_ = true;
    send(std::move(f));
  }

  void markChatRead(int64_t chatId) {
    auto c = chat(chatId);
    if (!c) return;
    if (c->last_message_) {
      auto f = make_object<td_api::viewMessages>();
      f->chat_id_ = chatId;
      f->message_ids_ = {c->last_message_->id_};
      f->force_read_ = true;
      send(std::move(f));
    }
    if (c->is_marked_as_unread_) send(make_object<td_api::toggleChatIsMarkedAsUnread>(chatId, false));
  }

  void sendText(int64_t chatId, const std::string &text, int64_t replyTo) {
    auto f = make_object<td_api::sendMessage>();
    f->chat_id_ = chatId;
    if (replyTo) {
      auto r = make_object<td_api::inputMessageReplyToMessage>();
      r->message_id_ = replyTo;
      f->reply_to_ = std::move(r);
    }
    auto content = make_object<td_api::inputMessageText>();
    content->text_ = make_object<td_api::formattedText>();
    content->text_->text_ = text;
    content->clear_draft_ = true;
    f->input_message_content_ = std::move(content);
    send(std::move(f), [this](object_ptr<td_api::Object> o) {
      if (o->get_id() == td_api::error::ID) reportError(static_cast<td_api::error &>(*o));
    });
  }

  void sendDocument(int64_t chatId, const std::string &path, int64_t replyTo) {
    auto f = make_object<td_api::sendMessage>();
    f->chat_id_ = chatId;
    if (replyTo) {
      auto r = make_object<td_api::inputMessageReplyToMessage>();
      r->message_id_ = replyTo;
      f->reply_to_ = std::move(r);
    }
    auto content = make_object<td_api::inputMessageDocument>();
    content->document_ = make_object<td_api::inputDocument>();
    content->document_->document_ = make_object<td_api::inputFileLocal>(path);
    f->input_message_content_ = std::move(content);
    send(std::move(f), [this](object_ptr<td_api::Object> o) {
      if (o->get_id() == td_api::error::ID) reportError(static_cast<td_api::error &>(*o));
    });
  }

  void deleteMessage(int64_t chatId, int64_t msgId, bool revoke) {
    auto f = make_object<td_api::deleteMessages>();
    f->chat_id_ = chatId;
    f->message_ids_ = {msgId};
    f->revoke_ = revoke;
    send(std::move(f), [this](object_ptr<td_api::Object> o) {
      if (o->get_id() == td_api::error::ID) reportError(static_cast<td_api::error &>(*o));
    });
  }

  td_api::message *message(int64_t id) {
    auto it = msgs.find(id);
    return it == msgs.end() ? nullptr : it->second.get();
  }

  // ---- files ----
  // Returns the freshest known state of a file embedded in a message.
  const td_api::file *file(const td_api::file *f) {
    if (!f) return nullptr;
    auto it = files.find(f->id_);
    return it == files.end() ? f : it->second.get();
  }

  static bool done(const td_api::file *f) {
    return f && f->local_ && f->local_->is_downloading_completed_ && !f->local_->path_.empty();
  }
  static bool downloading(const td_api::file *f) {
    return f && f->local_ && f->local_->is_downloading_active_;
  }

  void download(int32_t fileId, int priority) {
    auto f = make_object<td_api::downloadFile>();
    f->file_id_ = fileId;
    f->priority_ = priority;
    f->offset_ = 0;
    f->limit_ = 0;
    f->synchronous_ = false;
    send(std::move(f), [this](object_ptr<td_api::Object> o) {
      if (o->get_id() == td_api::error::ID) {
        reportError(static_cast<td_api::error &>(*o));
      } else if (o->get_id() == td_api::file::ID) {
        auto file = move_object_as<td_api::file>(o);
        storeFile(std::move(file));
      }
    });
  }

  void cancelDownload(int32_t fileId) {
    send(make_object<td_api::cancelDownloadFile>(fileId, false));
  }

 private:
#ifdef TG_MOCK
  struct MockMgr {
    int32_t create_client_id() { return 1; }
    void send(int32_t, uint64_t, object_ptr<td_api::Function>) {}
    td::ClientManager::Response receive(double) { return {}; }
  };
  std::unique_ptr<MockMgr> mgr_;
#else
  std::unique_ptr<td::ClientManager> mgr_;
#endif
  int32_t client_ = 0;
  uint64_t nextId_ = 1;
  std::map<uint64_t, Handler> handlers_;

  Handler authHandler() {
    return [this](object_ptr<td_api::Object> o) {
      if (o->get_id() == td_api::error::ID) {
        reportError(static_cast<td_api::error &>(*o));
        dirtyAuth = true;
      }
    };
  }

  void storeFile(object_ptr<td_api::file> f) {
    int32_t id = f->id_;
    bool wasDone = false;
    auto it = files.find(id);
    if (it != files.end()) wasDone = done(it->second.get());
    bool nowDone = done(f.get());
    files[id] = std::move(f);
    dirtyFiles.insert(id);
    if (nowDone && !wasDone && onFileDone) onFileDone(*files[id]);
  }

  void setPosition(td_api::chat &c, object_ptr<td_api::chatPosition> pos) {
    std::string key = listKey(pos->list_.get());
    if (key.empty()) return;
    for (auto it = c.positions_.begin(); it != c.positions_.end(); ++it) {
      if (listKey((*it)->list_.get()) == key) {
        lists[key].erase({(*it)->order_, c.id_});
        c.positions_.erase(it);
        break;
      }
    }
    if (pos->order_ != 0) {
      lists[key].insert({pos->order_, c.id_});
      c.positions_.push_back(std::move(pos));
    }
    dirtyChats = true;
  }

  // The loaded window is attached again once it contains the chat's newest message.
  void checkAttached() {
    auto c = chat(openChatId);
    if (detached && c && c->last_message_ && msgs.count(c->last_message_->id_)) detached = false;
  }

  void insertMessage(object_ptr<td_api::message> m) {
    if (!m || m->chat_id_ != openChatId) return;
    if (detached && !m->is_outgoing_) return;  // would leave a gap; it shows up once we catch up
    msgs[m->id_] = std::move(m);
    dirtyMsgs = true;
  }

  void onAuth(object_ptr<td_api::AuthorizationState> st) {
    authId = st->get_id();
    dirtyAuth = true;
    util::log("auth state %d", authId);
    switch (authId) {
      case td_api::authorizationStateWaitTdlibParameters::ID:
        if (apiId && !apiHash.empty()) sendParameters();
        else waitingForParams = true;
        break;
      case td_api::authorizationStateWaitCode::ID: {
        auto &s = static_cast<td_api::authorizationStateWaitCode &>(*st);
        codeInfo.clear();
        if (s.code_info_ && s.code_info_->type_) {
          switch (s.code_info_->type_->get_id()) {
            case td_api::authenticationCodeTypeTelegramMessage::ID:
              codeInfo = tr(S_CODE_APP);
              break;
            case td_api::authenticationCodeTypeSms::ID:
            case td_api::authenticationCodeTypeSmsWord::ID:
            case td_api::authenticationCodeTypeSmsPhrase::ID:
              codeInfo = tr(S_CODE_SMS);
              break;
            case td_api::authenticationCodeTypeCall::ID:
              codeInfo = tr(S_CODE_CALL);
              break;
            default:
              codeInfo = tr(S_CODE);
          }
        }
        break;
      }
      case td_api::authorizationStateWaitPassword::ID: {
        auto &s = static_cast<td_api::authorizationStateWaitPassword &>(*st);
        passwordHint = s.password_hint_;
        break;
      }
      case td_api::authorizationStateWaitOtherDeviceConfirmation::ID:
        qrLink = static_cast<td_api::authorizationStateWaitOtherDeviceConfirmation &>(*st).link_;
        break;
      case td_api::authorizationStateReady::ID:
        ready = true;
        send(make_object<td_api::getMe>(), [this](object_ptr<td_api::Object> o) {
          if (o->get_id() == td_api::user::ID) myId = static_cast<td_api::user &>(*o).id_;
        });
        loadChats("main", 60);
        break;
      case td_api::authorizationStateLoggingOut::ID:
        ready = false;
        break;
      case td_api::authorizationStateClosed::ID:
        ready = false;
        closed = true;
        break;
    }
  }


 public:
  void update(object_ptr<td_api::Object> o) {
    switch (o->get_id()) {
      case td_api::updateAuthorizationState::ID:
        onAuth(std::move(static_cast<td_api::updateAuthorizationState &>(*o).authorization_state_));
        break;
      case td_api::updateConnectionState::ID: {
        auto &u = static_cast<td_api::updateConnectionState &>(*o);
        switch (u.state_->get_id()) {
          case td_api::connectionStateWaitingForNetwork::ID: connection = tr(S_WAITING_NET); break;
          case td_api::connectionStateConnectingToProxy::ID: connection = tr(S_CONNECTING); break;
          case td_api::connectionStateConnecting::ID: connection = tr(S_CONNECTING); break;
          case td_api::connectionStateUpdating::ID: connection = tr(S_UPDATING); break;
          case td_api::connectionStateReady::ID: connection = ""; break;
        }
        dirtyStatus = true;
        break;
      }
      case td_api::updateNewChat::ID: {
        auto c = std::move(static_cast<td_api::updateNewChat &>(*o).chat_);
        int64_t id = c->id_;
        auto positions = std::move(c->positions_);
        c->positions_.clear();
        chats[id] = std::move(c);
        for (auto &p : positions) setPosition(*chats[id], std::move(p));
        dirtyChats = true;
        break;
      }
      case td_api::updateChatTitle::ID: {
        auto &u = static_cast<td_api::updateChatTitle &>(*o);
        if (auto c = chat(u.chat_id_)) c->title_ = u.title_;
        dirtyChats = true;
        break;
      }
      case td_api::updateChatLastMessage::ID: {
        auto &u = static_cast<td_api::updateChatLastMessage &>(*o);
        if (auto c = chat(u.chat_id_)) {
          c->last_message_ = std::move(u.last_message_);
          for (auto &p : u.positions_) setPosition(*c, std::move(p));
        }
        dirtyChats = true;
        break;
      }
      case td_api::updateChatPosition::ID: {
        auto &u = static_cast<td_api::updateChatPosition &>(*o);
        if (auto c = chat(u.chat_id_)) setPosition(*c, std::move(u.position_));
        break;
      }
      case td_api::updateChatReadInbox::ID: {
        auto &u = static_cast<td_api::updateChatReadInbox &>(*o);
        if (auto c = chat(u.chat_id_)) {
          c->unread_count_ = u.unread_count_;
          c->last_read_inbox_message_id_ = u.last_read_inbox_message_id_;
        }
        dirtyChats = true;
        break;
      }
      case td_api::updateChatReadOutbox::ID: {
        auto &u = static_cast<td_api::updateChatReadOutbox &>(*o);
        if (auto c = chat(u.chat_id_)) c->last_read_outbox_message_id_ = u.last_read_outbox_message_id_;
        dirtyChats = true;
        if (u.chat_id_ == openChatId) dirtyMsgs = true;
        break;
      }
      case td_api::updateChatIsMarkedAsUnread::ID: {
        auto &u = static_cast<td_api::updateChatIsMarkedAsUnread &>(*o);
        if (auto c = chat(u.chat_id_)) c->is_marked_as_unread_ = u.is_marked_as_unread_;
        dirtyChats = true;
        break;
      }
      case td_api::updateChatUnreadMentionCount::ID: {
        auto &u = static_cast<td_api::updateChatUnreadMentionCount &>(*o);
        if (auto c = chat(u.chat_id_)) c->unread_mention_count_ = u.unread_mention_count_;
        dirtyChats = true;
        break;
      }
      case td_api::updateChatNotificationSettings::ID: {
        auto &u = static_cast<td_api::updateChatNotificationSettings &>(*o);
        if (auto c = chat(u.chat_id_)) c->notification_settings_ = std::move(u.notification_settings_);
        dirtyChats = true;
        break;
      }
      case td_api::updateChatFolders::ID: {
        auto &u = static_cast<td_api::updateChatFolders &>(*o);
        folders.clear();
        for (auto &f : u.chat_folders_) {
          std::string name = (f->name_ && f->name_->text_) ? util::clean(f->name_->text_->text_, true) : "";
          folders.push_back({f->id_, name.empty() ? std::string(tr(S_FOLDER)) : name});
        }
        break;
      }
      case td_api::updateUser::ID: {
        auto u = std::move(static_cast<td_api::updateUser &>(*o).user_);
        users[u->id_] = std::move(u);
        dirtyChats = true;
        break;
      }
      case td_api::updateFile::ID:
        storeFile(std::move(static_cast<td_api::updateFile &>(*o).file_));
        break;
      case td_api::updateNewMessage::ID: {
        auto &u = static_cast<td_api::updateNewMessage &>(*o);
        if (u.message_ && onIncoming && !u.message_->is_outgoing_) onIncoming(*u.message_);
        insertMessage(std::move(u.message_));
        break;
      }
      case td_api::updateMessageSendSucceeded::ID: {
        auto &u = static_cast<td_api::updateMessageSendSucceeded &>(*o);
        if (u.message_ && u.message_->chat_id_ == openChatId) {
          msgs.erase(u.old_message_id_);
          insertMessage(std::move(u.message_));
        }
        break;
      }
      case td_api::updateMessageSendFailed::ID: {
        auto &u = static_cast<td_api::updateMessageSendFailed &>(*o);
        if (u.error_) reportError(*u.error_);
        if (u.message_ && u.message_->chat_id_ == openChatId) {
          msgs.erase(u.old_message_id_);
          insertMessage(std::move(u.message_));
        }
        break;
      }
      case td_api::updateMessageContent::ID: {
        auto &u = static_cast<td_api::updateMessageContent &>(*o);
        if (u.chat_id_ == openChatId) {
          if (auto m = message(u.message_id_)) {
            m->content_ = std::move(u.new_content_);
            dirtyMsgs = true;
          }
        }
        break;
      }
      case td_api::updateMessageEdited::ID: {
        auto &u = static_cast<td_api::updateMessageEdited &>(*o);
        if (u.chat_id_ == openChatId) {
          if (auto m = message(u.message_id_)) {
            m->edit_date_ = u.edit_date_;
            dirtyMsgs = true;
          }
        }
        break;
      }
      case td_api::updateDeleteMessages::ID: {
        auto &u = static_cast<td_api::updateDeleteMessages &>(*o);
        if (u.chat_id_ == openChatId && u.is_permanent_) {
          for (auto id : u.message_ids_) msgs.erase(id);
          dirtyMsgs = true;
        }
        break;
      }
      default:
        break;
    }
  }
};
