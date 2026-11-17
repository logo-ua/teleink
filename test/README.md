# Tests

These are developer tools; they are not part of the release build.

## Screenshot harness (`shots.cpp`, `mock_inkview.cpp`)

Renders the main screens on a Linux desktop with fake chats, using FreeType and the Liberation fonts
instead of InkView. It is a quick way to check layout changes without a reader. It links against a
**host** build of TDLib's `tdapi` and `tdutils` libraries (only the API object classes are used; no network).

```sh
# host build of TDLib's API classes (once)
git clone https://github.com/tdlib/td deps/td-host && cd deps/td-host
git checkout 03220f6b3043c6fd7785fbf0901d63f19ad19974
cmake -B build -DCMAKE_BUILD_TYPE=Release . && cmake --build build --target tdapi tdutils -j"$(nproc)"
cd ../..
# inkview.h comes from the SDK fetched by scripts/setup.sh
TD=deps/td-host
g++ -std=c++17 -fno-rtti -w -Ideps/sdk/SDK-B288/usr/arm-obreey-linux-gnueabi/sysroot/usr/local/include \
  -Ideps/toolchain/inc -I/usr/include/freetype2 -I$TD -I$TD/td/generate/auto -I$TD/tdutils -I$TD/build/tdutils \
  test/shots.cpp test/mock_inkview.cpp -o shots $TD/build/libtdapi.a $TD/build/tdutils/libtdutils.a \
  -lfreetype -lpthread -lz -lssl -lcrypto
./shots            # writes /tmp/shots/*.pgm (758x1024); ./shots 1072 1448 for Verse Pro
```

## TDLib smoke test on ARM (`tdtest.cpp`)

Runs the TDLib wrapper without UI under `qemu-arm` with the SDK's glibc to check that the ARM build
starts, creates its database and reaches the phone-number step. Needs the full SDK sysroot (not the
sparse checkout made by `setup.sh`) and your own keys in the `TG_API_ID` / `TG_API_HASH` environment variables.

## Presentation screenshots (`promo.cpp`)

Same harness with realistic demo chats. Build like `shots.cpp` (replace `test/shots.cpp` with `test/promo.cpp`), then:

```sh
MOCK_FONT_DIR=<dir with Roboto-Regular.ttf and Roboto-Bold.ttf> ./promo 1072 1448 1   # 0 = UK, 1 = EN, 2 = RU
```

It expects avatar images as PGM files in `/tmp/promo/` and writes `/tmp/promo/shot_*.pgm`. The framed images in
`docs/promo/` were made from these.
