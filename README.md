# sift

Read the web, aloud, with Kokoro. A native Omarchy app that strips a page down
to the article, keeps a durable Markdown copy, and reads it sentence by
sentence with a local Kokoro voice.

![sift reading a page](docs/screenshot.png)

## What it does

- Loads a public URL in Qt WebEngine and extracts the article with
  [Defuddle](https://github.com/kepano/defuddle), the same extractor used by
  Obsidian Web Clipper.
- Saves a clean Markdown file and a full article snapshot, with resume position.
- Reads aloud through [Kokoro-FastAPI](https://github.com/remsky/Kokoro-FastAPI)
  at `http://127.0.0.1:8880`, with sentence highlighting, prefetch, and a voice
  and speed picker.
- Keeps a single reading queue that advances automatically.
- Reopens the article you were reading, where you left off.
- Follows the Omarchy theme, light/dark mode, window rounding, and language.
  Like Spotifast, it can instead follow the system's light or dark preference,
  or stay light or dark (Settings, or the **⋯ → Appearance** menu). Theme
  changes crossfade.
- Lets you choose any installed font and text size for the reading view.

## Requirements

- Omarchy with Qt 6 (`qt6-base`, `qt6-declarative`, `qt6-multimedia`,
  `qt6-webengine`, `qt6-wayland`).
- A running Kokoro-FastAPI server. The app connects to it; it does not start it.

```sh
docker run -d --name kokoro -p 8880:8880 ghcr.io/remsky/kokoro-fastapi-cpu:latest
```

The endpoint is configurable in Settings if you use a different host or port.

## Build and install

```sh
./bin/build      # builds build/sift
./bin/test       # Qt Test suite, offscreen
./bin/install    # makepkg -fsi: binary, desktop entry, icon
```

`bin/install` puts `sift` in the Omarchy launcher (`Super + Space`).

## Use

```sh
sift                              # open the app
sift <url>                        # open a page and read it
sift add <url>                    # clip it without opening the window
sift list                         # list saved pages
sift play <id|url>                # open and start reading
sift export <id>                  # Markdown to stdout
sift queue                        # list the queue
sift queue add <url|id>           # queue a page, fetching it if it is new
sift queue remove <id|url|n>      # take a page off the queue
sift queue move <id|url|n> <n|top|bottom>
```

`add`, `list`, `queue`, and `queue add` support `--json`. `add` saves a page
without queueing it; `queue add` fetches and saves a new URL before queueing
it, or queues a page you already saved. Queue positions start at 1. A running
app refreshes itself after any of these.

## Keyboard

| Key | Action |
|---|---|
| `Space` | Play / pause |
| `[` / `]` | Previous / next sentence |
| `-` / `+` | Slower / faster |
| `V` | Voice picker |
| `A` | Add a page |
| `J` / `K` | Move in the queue, or scroll the article when the queue is hidden |
| `Shift+J` / `Shift+K` | Move the selected queue item down / up |
| `Delete` | Remove the selected queue item from the queue |
| `Enter` | Open the selected queue item |
| `E` | Copy the Markdown export |
| `F` | Show / hide the queue |
| `PageUp` / `PageDown` | Scroll the article |
| `?` | All shortcuts |
| `Q` | Quit |

Drag a queue item by its grip (the position number turns into one on hover) to
reorder it, or use its menu to play it next or send it to the top or bottom.
Removing an item does not delete its saved files. Clicking any sentence starts reading from there. Manually
scrolling suspends following; choose **Return to playback** to resume it.
`Alt+click` on a link opens
it as a new sift article; a normal click opens it in your browser.

## Where things live

- Saved-page and queue index: `~/.local/share/sift/sift.db`
- Article snapshots: `~/.local/share/sift/articles/<id>/content.html`
- Markdown exports: `~/Documents/sift/<slug>-<hash>.md` (configurable)
- Kokoro audio cache: `~/.cache/sift/audio/` (1 GB cap by default)

Markdown files get YAML frontmatter with `title`, `source`, `author`,
`published`, `clipped`, `domain`, `word_count`, and `tags`. If you edit a
Markdown file outside sift, sift writes the next clip to a new file instead of
overwriting your work.

## How it works

```text
URL → hidden Qt WebEngine → Defuddle → HTML + Markdown + metadata
                                          │
                    SQLite index + Markdown + article snapshot
                                          │
   one persistent Qt WebEngine reader page; each article renders in place
                                          │
        Kokoro /v1/audio/speech → MP3 cache → QMediaPlayer
```

`Intl.Segmenter` splits the rendered article into sentences inside the reader;
the C++ engine preloads the rest of the current paragraph and the whole next
paragraph through Kokoro, then caches each MP3 by voice, speed, and text. Code
blocks and tables are skipped when speaking.

## Limitations

- Public pages only for v1. Logged-in and paywalled pages may fail.
- Anti-bot pages can block Qt WebEngine; the error banner reports the failure.
- Article images stay linked to their source; the HTML snapshot is not a fully
  offline image archive.
- Kokoro audio is cached per sentence, so there is a short gap between
  sentences while playback moves to the next file.
- The app connects to Kokoro but does not manage the Docker container.

## Development

`bin/test` covers theme fallbacks, translation, saved articles and queue
operations. It also launches the real QML and WebEngine reader offscreen with
an isolated temporary home, checks sentence seeking and the queue, and opens
the main dialogs. The GUI test disables the Chromium sandbox for that isolated
test process; the application does not disable it.

The reader's typography follows Obsidian Web Clipper's reader view (MIT,
Obsidian). The app scaffolding follows Omacut and Monologue (MIT, David Heinemeier
Hansson). Defuddle is MIT, copyright Steph Ango; its license is in
`LICENSES/defuddle.txt`.

## License

MIT. See [LICENSE](LICENSE).
