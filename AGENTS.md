# AGENTS.md

sift is a single Qt 6 / QML desktop app (C++17, qmake, no CMake, no CI) that
clips web articles with Defuddle and reads them aloud through a local
Kokoro-FastAPI server. `README.md` documents user-facing behavior; this file
covers only what is easy to get wrong.

## Commands

```sh
./bin/build          # build/sift (qmake6 + make into build/)
./bin/test           # both Qt Test suites, offscreen
./bin/install        # build, then makepkg -fsi in pkgbuild/ (system install)
./bin/package        # prebuilt tarball in dist/, prints its sha256
```

Run one test function after `./bin/test` has built the binaries:

```sh
QT_QPA_PLATFORM=offscreen ./build/tests/sift_tests themeFallsBack
QT_QPA_PLATFORM=offscreen QT_FORCE_STDERR_LOGGING=1 ./build/gui-tests/sift_gui_smoke readerAndDialogs
```

Neither suite needs network, Kokoro, or a display. `gui_smoke` sets its own
temporary `HOME`/XDG dirs and `QTWEBENGINE_DISABLE_SANDBOX=1`; that sandbox
switch exists only in the test process. `SIFT_GUI_SCREENSHOT`,
`SIFT_GUI_DRAG_SCREENSHOT`, `SIFT_GUI_QUEUE_SCREENSHOT`,
`SIFT_GUI_MENU_SCREENSHOT`, `SIFT_GUI_SETTINGS_SCREENSHOT`, and
`SIFT_GUI_DROPDOWN_SCREENSHOT` save PNGs for visual checks.

`web/` is a separate Bun app (sift for the phone): `bun install`, `bun test`,
`bun server.js`. It shares no build with the Qt app, but keeps the desktop
database schema, ports `src/reader.js` sentence wrapping, and ports
`src/speech.cpp` numeral rules into `src/speech.js`; change those in both
places. See
`web/README.md` for deploy steps.

The demo video is a separate Remotion project in `video/` (`npm i`,
`npm run dev`, `npm run lint`, `npm run render`); it shares no toolchain with
the app.

## Resource pipeline

All QML, `reader.css`, `reader.js`, the 770 KB vendored `defuddle.full.js`,
and the i18n catalogs ship through `src/resources.qrc`. Consequences:

- A new QML/JS/CSS/JSON asset must be added to `src/resources.qrc` or it will
  not be in the binary.
- A new C++ source or header belongs in `sift.pro`, and in
  `tests/gui_smoke.pro` / `tests/sift_tests.pro` if the tests link it.
- `bin/build` touches `resources.qrc` first because qmake does not track the
  files listed inside it; a QML-only edit would otherwise keep the stale
  embedded copy. Prime the resource rebuild through `./bin/build`, not a bare
  `make` in `build/`.

## Architecture

- `src/main.cpp` is the CLI plus QML bootstrap. It also enforces a single
  instance through a `QLocalServer` named `sift-<uid>`: `add`, `queue`, and
  `open`/`play` forward JSON commands to a running app. Adding a subcommand
  means touching `parseCli`, `usage`, and the dispatch at the bottom of
  `main`. `add` and `queue add` force `QT_QPA_PLATFORM=offscreen` and
  `--disable-gpu` to fetch pages without a window.
- `Backend` (`src/backend.h`) is the only QML-facing object: every
  `Q_PROPERTY`, `Q_INVOKABLE`, and signal the UI uses lives here. It
  coordinates `Extractor`, `Library`, `Kokoro`, and `TtsEngine`; QML never
  talks to those directly.
- `Library` owns SQLite (`sift.db`), article HTML snapshots, Markdown exports,
  tags, playlists, and the queue. Ids are UUIDs; a re-clip finds the existing
  row by canonical URL (or URL) and updates it, so reading progress survives.
  If the Markdown file changed on disk (hash mismatch), it writes a new file
  instead of overwriting. Schema setup is `CREATE TABLE IF NOT EXISTS` plus
  `PRAGMA user_version=1`; there is no migration framework, so a column
  change needs a guarded `ALTER TABLE`.
- `Extractor` loads a page in a hidden `QWebEnginePage`, injects Defuddle at
  document creation, blocks non-text requests, and parses as soon as the DOM
  is usable. `defuddle.full.js` is vendored, MIT, and not to be edited.
- `TtsEngine` streams each sentence from `Kokoro` as 24 kHz PCM into one
  push-mode `QAudioSink`, keeps a timeline of where each sentence starts in
  that stream, and maps the sink's played bytes back to the current sentence.
  Only one Kokoro request runs at a time, nearest first, and one for a
  sentence playback has left is aborted (Kokoro slows every request down when
  several run together). Clips are cached as `.pcm` by voice, speed, and the
  cleaned text. `prefetchIndices` is static so it stays testable.
- `speech.cpp` spells out Roman numerals after a label ("Part II"). The web
  server's `src/speech.js` has the same rules; change both together.
- The reader is one long-lived `WebEngineView` (no reload per article). C++
  builds an HTML shell with a nonce-based CSP in `Backend::readerShell`; QML
  calls `siftRender` / `siftApplyTheme` / `siftSetCurrent` through
  `runJavaScript`, and `siftRender` returns the sentence JSON that QML passes
  to `backend.sentencesReady`. Reader events come back as navigations to
  fake `https://sift.invalid/...` URLs, which `Main.qml` routes to
  `backend.handleReaderUrl`. There is no WebChannel, so keep this contract in
  sync across `reader.js`, `Main.qml`, and `backend.cpp`.
- `Theme` and `Translator` read live Omarchy state:
  `~/.local/state/omarchy/current` and `~/.config/omarchy/language`. UI
  strings use `qsTr()` / `tr()` with the English source as the lookup key;
  catalogs are flat JSON under `src/i18n/` (only `en.json` exists, and it is
  listed in `resources.qrc`).

## Versions and release

The version is hardcoded in `src/main.cpp` three times (`--version` output and
two `setApplicationVersion` calls) and in `pkgbuild/PKGBUILD`,
`pkgbuild/aur/PKGBUILD`, and `pkgbuild/aur-bin/PKGBUILD`. Bump all of them
together. `bin/package` reads the version from `pkgbuild/aur/PKGBUILD`; the
AUR PKGBUILDs fetch from the `v$pkgver` tag, so tag and release must exist
before pushing to the AUR. The AUR package is `sift-reader` (the name `sift`
is taken); the installed command is still `sift`.

## Style

- 4-space C++ formatting, `m_` member prefix, headers that explain *why* a
  class exists. No `.clang-format` and no lint step for C++, so match the
  surrounding file.
- QML files are flat PascalCase components in `src/`, exposed to C++ only
  through `Backend`, `Theme`, and `translator` context properties.
- `reader.js` runs inside Qt WebEngine, so match the existing style there
  (`var`, ES2015+ features such as `Set` and `for...of` are fine). Respect the
  shell's CSP: only the inline script with the nonce runs, and handlers from
  extracted HTML must never execute.
- Keep new user-visible strings translatable and add no network dependency at
  startup; Kokoro is optional and its absence should degrade to an error
  banner, not a crash.
