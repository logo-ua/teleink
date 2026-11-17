<img src="docs/icon.png" width="96" align="right">

# Teleink — Unofficial Telegram for PocketBook

**Telegram for PocketBook e-readers**: read and send messages, open channels, download files and books straight to your reader.
Works on PocketBook Verse, Verse Pro, Era, InkPad, Touch Lux, Touch HD and other models with firmware 6.x.

Unofficial client. Uses the Telegram API via [TDLib](https://github.com/tdlib/td). Not affiliated with Telegram.

![Teleink — Telegram for PocketBook](docs/promo/teleink-banner.png)

**[⬇ Download the latest version](https://github.com/logo-ua/teleink/releases/latest)** · [Website](https://logo-ua.github.io/teleink/)

| | | | |
|---|---|---|---|
| ![](docs/screenshots/language.png) | ![](docs/screenshots/chats.png) | ![](docs/screenshots/chat.png) | ![](docs/screenshots/channel.png) |

## Install Telegram on PocketBook

1. Download the zip from [Releases](../../releases/latest).
2. Copy its contents to the root of the reader (`Teleink.app` → `applications/`).
3. Open **Teleink** in Applications.

No built-in API keys? Get `api_id` and `api_hash` at [my.telegram.org](https://my.telegram.org) → API development tools.

## Features

Telegram chats and channels on an e-ink screen, nothing else: read and write, files (download to `Downloads/Teleink`, books and photos open in the reader, send from the reader) · English, Українська, Русский.

## Build

```sh
scripts/setup.sh   # SDK, Zig, OpenSSL, TDLib → ./deps (≈1 h first time)
scripts/build.sh   # → out/Teleink.app, out/Teleink-dev.zip
```

Embed keys: `TG_API_ID=… TG_API_HASH=… scripts/build.sh`, or add `TG_API_ID` / `TG_API_HASH` as repository secrets — GitHub Actions then builds every published release with them.

## License

Made by [LOGO.UA](https://logo.ua) · [MIT](LICENSE) · [third-party notices](THIRD_PARTY_NOTICES.md) · Telegram is a trademark of Telegram FZ-LLC.

---

**Українська — Telegram для PocketBook.** Неофіційний клієнт Telegram для електронних книг PocketBook. Скопіюйте вміст архіву з [Releases](../../releases) у корінь рідера й відкрийте **Teleink**. Мову обирають під час першого запуску.

**Русский — Telegram для PocketBook.** Неофициальный клиент Telegram для электронных книг PocketBook. Скопируйте содержимое архива из [Releases](../../releases) в корень ридера и откройте **Teleink**. Язык выбирается при первом запуске.
