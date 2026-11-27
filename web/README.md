# sift web

sift's reading queue and Kokoro voice in a browser, built for a phone. One Bun
process serves the page, extracts articles with
[Defuddle](https://github.com/kepano/defuddle), keeps a sift library, and
proxies speech to a private Kokoro-FastAPI container.

- The player has previous, play/pause, and next sentence. The button on the
  left opens the queue; the sliders button on the right opens speed, voice,
  and text size, and carries a small badge with the speed when it is not 1×.
- The voice row in that sheet opens a picker grouped by language, with the
  article's language first and your last three voices on top. Search matches
  name, language, or gender. Each voice has a preview button that reads the
  sentence you are on; a preview pauses the article and hands playback back
  when it ends. Picking a voice restarts the current sentence in it. `V`
  opens the picker from the keyboard; arrows move, `Space` previews, `Enter`
  picks.
- The queue drawer adds pages from a pasted link, reorders by dragging the
  grip, and removes a row when you swipe it left (or hover and click ✕ with a
  mouse). Queue and library sections collapse. On a wide screen the drawer is
  a sidebar that the same button collapses.
- Articles render as sanitized HTML with their images, split into the same
  sentences and paragraphs as the desktop reader. The current sentence is
  highlighted and followed; scrolling by hand pauses following until you tap
  **Back to playback**.
- An article plays as one continuous stream, so the phone keeps reading with
  the screen off or another app in front. The server synthesizes ahead and
  joins the clips, and caches every one on disk by voice, speed, and text.
  Opening an article synthesizes its first clip before you press play.
- The server sends Kokoro one clip at a time, the one a listener is waiting
  on first. Several at once slow every one of them down. A jump or a new voice
  aborts the clips the old stream asked for. Roman numerals after a label are
  read as numbers, as on the desktop.
- A stream stays about 15 seconds ahead of what you hear (the player reports
  its position with each timing poll), so Kokoro has time left over. The
  player uses it to fetch the queue's articles and to have the next two
  articles' opening sentences made, so moving to the next item shows its text
  at once and starts speaking in about half a second.
- Colors are Flexoki, light or dark from the system setting. Text is Atkinson
  Hyperlegible Next, served with the page.
- The page is installable. On Android, sharing a link to it adds the page to
  the queue.

## Library

The data directory holds a regular sift database (`sift.db`, the desktop
schema) plus `articles/<id>/content.html` snapshots and `cache/audio/`. To
start from the desktop library, copy a snapshot in:

```sh
sift sync export /tmp/sift.db
scp /tmp/sift.db server:apps/sift/data/sift.db
```

Articles that arrive without an HTML snapshot are fetched again in the
background after startup, queue first, so they show their images and
formatting. Until a page can be fetched, sift shows its saved text, split back
into paragraphs. A page that refuses is asked again after six hours, not on
every open. Some sites (CNBC) refuse every request from a datacenter address;
copy the desktop's `articles/<id>/content.html` for those, as the sync section
of the main README describes. Changes
made on the web stay in the server's database; `sift sync merge` on the
desktop can read a copy of it.

## Run

```sh
bun install
SIFT_DATA=./data KOKORO_URL=http://127.0.0.1:8880 PORT=8790 bun server.js
bun test
```

| Variable | Default | |
|---|---|---|
| `SIFT_DATA` | `/data` | Library, snapshots, and audio cache |
| `KOKORO_URL` | `http://kokoro:8880` | Kokoro-FastAPI endpoint |
| `SIFT_HOSTS` | `localhost,127.0.0.1` | Host headers to accept; add your domain |
| `PORT` | `8080` | Listen port |

Writes must come from the same origin as the page. The server refuses to
fetch pages on private or tailnet addresses.

## Deploy

`compose.yaml` runs the app and Kokoro. The app listens on
`127.0.0.1:8777` for a reverse proxy in front of it and runs as uid 1000, so
`./data` must be writable by that user. Put the public hostname in a `.env`
next to `compose.yaml`, for example `SIFT_HOSTS=sift.example.com,localhost`.
The reading stream is sent without a `Content-Length`, which Caddy flushes
immediately; other proxies need response buffering off (`proxy_buffering off`
in nginx).

```sh
rsync -a --delete --exclude node_modules --exclude data --exclude .env web/ server:apps/sift/
ssh server 'cd apps/sift && docker compose up -d --build'
```
