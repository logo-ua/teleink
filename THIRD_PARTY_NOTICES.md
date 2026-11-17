# Third-party components

Release binaries of Teleink statically include the following components.
Full license texts are in the [`licenses/`](licenses) folder of the source repository.

| Component | License | Source |
|---|---|---|
| TDLib (Telegram Database Library) 1.8.70 | Boost Software License 1.0 | https://github.com/tdlib/td |
| SQLite (bundled with TDLib) | Public domain | https://sqlite.org |
| OpenSSL 3.0.22 | Apache License 2.0 | https://www.openssl.org |
| zlib 1.2.11 | zlib License | https://zlib.net |
| LLVM libc++, libc++abi, libunwind (via Zig 0.14.1) | Apache License 2.0 with LLVM Exceptions | https://llvm.org |
| Zig compiler-rt | MIT | https://ziglang.org |

At run time the program uses the reader's own system libraries (InkView, glibc). They are part of the
PocketBook firmware and are not distributed with this project.

Telegram is a trademark of Telegram FZ-LLC. This project is not affiliated with or endorsed by Telegram.
