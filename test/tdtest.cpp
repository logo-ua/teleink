// Runs the TDLib wrapper without UI to check the ARM build under qemu.
#include "../src/tg.h"
#include <cstdio>
#include <ctime>
#include <cstdlib>
int main() {
  Tg tg;
  tg.dataDir = "/tmp/tdarm";
  util::mkdirs(tg.dataDir);
  util::logPath() = "/tmp/tdarm/app.log";
  // your own keys from my.telegram.org
  const char *id = getenv("TG_API_ID"), *hash = getenv("TG_API_HASH");
  if (!id || !hash) {
    printf("set TG_API_ID and TG_API_HASH\n");
    return 1;
  }
  tg.apiId = atoi(id);
  tg.apiHash = hash;
  tg.onError = [](const std::string &e) { printf("error: %s\n", e.c_str()); };
  tg.start();
  int last = -1;
  time_t start = time(nullptr);
  bool sentPhone = false;
  while (time(nullptr) - start < 40) {
    tg.poll(0.5);
    if (tg.authId != last) {
      last = tg.authId;
      printf("auth state id %d\n", last);
      fflush(stdout);
    }
    if (!sentPhone && tg.authId == td_api::authorizationStateWaitPhoneNumber::ID) {
      sentPhone = true;
      printf("reached WaitPhoneNumber, connection: '%s'\n", tg.connection.c_str());
      break;
    }
  }
  printf("connection: '%s'\n", tg.connection.c_str());
  tg.close();
  start = time(nullptr);
  while (!tg.closed && time(nullptr) - start < 10) tg.poll(0.5);
  printf("closed=%d\n", tg.closed);
  return 0;
}
