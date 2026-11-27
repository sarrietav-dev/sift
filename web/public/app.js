import { QueueList, renderLibrary } from "./queue.js";
import { renderArticle } from "./reader.js";
import { AudioStore, announcement, cleanForSpeech, playingItem } from "./tts.js";
import { VoicePicker } from "./voices.js";

const $ = id => document.getElementById(id);
const desktop = matchMedia("(min-width: 960px)");
const SPEEDS = [0.5, 0.6, 0.7, 0.75, 0.8, 0.9, 1, 1.1, 1.2, 1.25, 1.3, 1.4, 1.5, 1.6, 1.75, 1.8, 2];

const local = {
    get(key, fallback) {
        try {
            const value = localStorage.getItem("sift:" + key);
            return value === null ? fallback : JSON.parse(value);
        } catch {
            return fallback;
        }
    },
    set(key, value) {
        try {
            localStorage.setItem("sift:" + key, JSON.stringify(value));
        } catch {}
    },
};

const state = {
    queue: [],
    articles: [],
    voice: "af_heart",
    speed: 1,
    article: null,
    sentences: [],
    speech: [],
    index: 0,
    intro: "",
    introPending: false,
    playing: false,
    following: true,
    filter: "",
};

const audio = new Audio();
audio.preload = "auto";
const store = new AudioStore();
let generation = 0;
let openToken = 0;
let currentElement = null;

// ---------------------------------------------------------------- server

async function api(path, options = {}) {
    const response = await fetch(path, {
        ...options,
        headers: options.body ? { "content-type": "application/json" } : undefined,
        body: options.body ? JSON.stringify(options.body) : undefined,
    });
    const body = await response.json().catch(() => ({}));
    if (!response.ok) throw new Error(body.error || "Something went wrong.");
    return body;
}

let toastTimer = 0;
function toast(message) {
    const element = $("toast");
    element.textContent = message;
    element.classList.add("show");
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => element.classList.remove("show"), 3200);
}

let progressTimer = 0;
function saveProgress(readState) {
    const article = state.article;
    if (!article) return;
    clearTimeout(progressTimer);
    const send = () => api(`/api/articles/${article.id}/progress`, {
        method: "PUT",
        body: { sentence: state.index, ...(readState === undefined ? {} : { readState }) },
    }).catch(() => {});
    if (readState !== undefined) send();
    else progressTimer = setTimeout(send, 800);
    article.lastSentence = state.index;
    for (const list of [state.queue, state.articles]) {
        const item = list.find(entry => entry.id === article.id);
        if (item) {
            item.lastSentence = state.index;
            if (readState !== undefined) item.readState = readState;
        }
    }
}

// ---------------------------------------------------------------- drawer

const queueList = new QueueList($("queue-list"), {
    onOpen: item => open(item.id, { autoplay: true }),
    onRemove: item => updateQueue(() => api(`/api/queue/${item.id}`, { method: "DELETE" }), state.queue.filter(entry => entry.id !== item.id)),
    onReorder: ids => updateQueue(() => api("/api/queue", { method: "PUT", body: { ids } }), ids.map(id => state.queue.find(entry => entry.id === id))),
});

async function updateQueue(request, optimistic) {
    const previous = state.queue;
    state.queue = optimistic;
    renderDrawer();
    try {
        state.queue = (await request()).queue;
    } catch (error) {
        state.queue = previous;
        toast(error.message);
    }
    renderDrawer();
}

function renderDrawer() {
    const currentId = state.article?.id ?? null;
    queueList.render(state.queue, currentId);
    $("queue-count").textContent = state.queue.length || "";
    $("queue-empty").hidden = state.queue.length > 0 || queueList.pending.length > 0;
    renderLibraryList();
    updateTransport();
}

function renderLibraryList() {
    const queued = new Set(state.queue.map(item => item.id));
    $("library-count").textContent = state.articles.length || "";
    renderLibrary($("library-list"), state.articles, queued, state.article?.id ?? null, state.filter, {
        onOpen: item => open(item.id, { autoplay: true }),
        onQueue: item => updateQueue(() => api(`/api/queue/${item.id}`, { method: "POST" }), [...state.queue, item]),
    });
}

function setDrawer(open) {
    document.body.classList.toggle("drawer-open", open);
    $("queue-button").setAttribute("aria-expanded", String(open));
    if (desktop.matches) local.set("sidebar", open);
}

for (const head of document.querySelectorAll(".group-head")) {
    const key = "group:" + head.dataset.group;
    const expanded = local.get(key, head.getAttribute("aria-expanded") === "true");
    const apply = value => {
        head.setAttribute("aria-expanded", String(value));
        $(head.getAttribute("aria-controls")).hidden = !value;
    };
    apply(expanded);
    head.addEventListener("click", () => {
        const value = head.getAttribute("aria-expanded") !== "true";
        apply(value);
        local.set(key, value);
    });
}

$("filter").addEventListener("input", event => {
    state.filter = event.target.value;
    renderLibraryList();
});

$("add-form").addEventListener("submit", async event => {
    event.preventDefault();
    const input = $("add-url");
    const url = input.value.trim();
    if (!url) return;
    await addPage(url);
});

async function addPage(url) {
    const input = $("add-url");
    let host = url;
    try {
        host = new URL(url).hostname.replace(/^www\./, "");
    } catch {}
    $("add-error").hidden = true;
    input.value = "";
    queueList.pending = [...queueList.pending, host];
    renderDrawer();
    try {
        const result = await api("/api/articles", { method: "POST", body: { url } });
        state.queue = result.queue;
        state.articles = result.articles;
        queueList.pending = queueList.pending.filter(label => label !== host);
        renderDrawer();
        if (!state.article) open(result.id);
        else toast("Added to the queue");
    } catch (error) {
        queueList.pending = queueList.pending.filter(label => label !== host);
        renderDrawer();
        input.value = url;
        $("add-error").textContent = error.message;
        $("add-error").hidden = false;
        setDrawer(true);
    }
}

// ---------------------------------------------------------------- reader

function renderMeta(article) {
    const parts = [];
    if (article.author) parts.push(["author", article.author]);
    if (article.published) {
        const date = new Date(article.published);
        parts.push(["date", Number.isNaN(date.getTime()) ? article.published
            : date.toLocaleDateString(undefined, { year: "numeric", month: "long", day: "numeric" })]);
    }
    if (article.domain) parts.push(["domain", article.domain]);
    if (article.wordCount > 0) parts.push(["time", `${Math.max(1, Math.round(article.wordCount / 220))} min read`]);
    const meta = $("meta");
    meta.replaceChildren();
    parts.forEach(([kind, text], i) => {
        if (i) {
            const dot = document.createElement("span");
            dot.className = "dot";
            dot.textContent = "·";
            meta.append(dot);
        }
        const span = document.createElement("span");
        span.className = kind;
        span.textContent = text;
        meta.append(span);
    });
}

async function open(id, { autoplay = false } = {}) {
    const token = ++openToken;
    pause();
    reading = null;
    pendingReading = null;
    audio.onended = null;
    audio.removeAttribute("src");
    audio.load();
    if (!desktop.matches) setDrawer(false);
    const preview = state.queue.find(item => item.id === id) || state.articles.find(item => item.id === id);
    $("empty").hidden = true;
    $("article").hidden = false;
    if (preview && preview.id !== state.article?.id) {
        $("title").textContent = preview.title || "Untitled";
        renderMeta(preview);
        $("article").classList.add("loading");
    }
    let article;
    try {
        article = await api(`/api/articles/${id}`);
    } catch (error) {
        if (token === openToken) {
            $("article").classList.remove("loading");
            toast(error.message);
        }
        return;
    }
    if (token !== openToken) return;

    state.article = article;
    local.set("last", id);
    document.documentElement.lang = article.language || "en";
    $("title").textContent = article.title || "Untitled";
    renderMeta(article);
    $("article").classList.remove("loading");
    $("source").href = article.url;
    $("source").hidden = !/^https?:/i.test(article.url || "");

    const sentences = renderArticle($("content"), article);
    state.sentences = sentences.map(sentence => sentence.element);
    state.speech = sentences.map(sentence => cleanForSpeech(sentence.text));
    state.index = article.readState === 2 ? 0 : Math.min(Math.max(0, article.lastSentence || 0), Math.max(0, sentences.length - 1));
    state.intro = cleanForSpeech(announcement(article.title, article.author));
    state.introPending = state.index === 0 && !!state.intro;
    state.following = true;
    currentElement = null;
    $("resume").hidden = true;

    if (article.readState === 0) {
        article.readState = 1;
        saveProgress(1);
    }
    highlight(false);
    if (state.index > 0) state.sentences[state.index]?.scrollIntoView({ block: "center" });
    else window.scrollTo(0, 0);
    if (article.source === "text") toast("Showing saved text. The page could not be fetched again.");

    setMediaMetadata();
    renderDrawer();
    if (!sentences.length) {
        toast("No readable text in this article.");
        return;
    }
    if (autoplay) play(state.index);
    else warm();
}

function highlight(scroll = true) {
    const element = state.sentences[state.index] || null;
    if (currentElement !== element) {
        currentElement?.classList.remove("current");
        element?.classList.add("current");
        currentElement = element;
    }
    const total = state.sentences.length;
    $("progress-bar").style.width = total ? `${((state.index + 1) / total) * 100}%` : "0";
    if (scroll && element && state.following) {
        const box = element.getBoundingClientRect();
        const bottom = window.innerHeight - (document.querySelector(".player").offsetHeight || 0);
        // Only move the page when the sentence leaves a comfortable band, so
        // reading along is not a constant scroll.
        if (box.top < window.innerHeight * 0.15 || box.bottom > bottom - window.innerHeight * 0.2)
            element.scrollIntoView({ block: "center", behavior: "smooth" });
    }
}

$("content").addEventListener("click", event => {
    if (event.target.closest("a")) return;
    if (String(getSelection() || "").length) return;
    const sentence = event.target.closest(".sentence");
    if (!sentence) return;
    state.introPending = false;
    state.following = true;
    $("resume").hidden = true;
    play(Number(sentence.dataset.index));
});

function stopFollowing(event) {
    if (!state.article || !state.following) return;
    if (event.target.closest?.(".drawer, .sheet, .player")) return;
    state.following = false;
    $("resume").hidden = !state.playing;
}
window.addEventListener("wheel", stopFollowing, { passive: true });
window.addEventListener("touchmove", stopFollowing, { passive: true });

$("resume").addEventListener("click", () => {
    state.following = true;
    $("resume").hidden = true;
    currentElement?.scrollIntoView({ block: "center", behavior: "smooth" });
});

// ---------------------------------------------------------------- playback

// An article plays as one continuous stream, not one clip per sentence. A
// single long-lived media element is what keeps playback going when the phone
// locks or the page is put away; a chain of short clips goes quiet at the
// first boundary. The server reports each clip's duration as it synthesizes,
// so audio time maps back to the sentence being spoken.
let reading = null;
let pendingReading = null;
let streamBase = 0;
let streamOffset = 0;
let timingTimer = 0;
let retried = false;
let pausedAt = 0;

function startItem(index, intro) {
    // Item zero is the announcement when an article has one; starting
    // anywhere else skips it.
    return intro ? 0 : index + (state.intro ? 1 : 0);
}

function ensureReading(warmFrom = 0) {
    const article = state.article;
    const voice = state.voice;
    const speed = state.speed;
    if (reading && reading.article === article.id && reading.voice === voice && reading.speed === speed)
        return Promise.resolve(reading);
    const key = [article.id, voice, speed].join("|");
    if (pendingReading && pendingReading.key === key) return pendingReading.promise;
    const texts = state.intro ? [state.intro, ...state.speech] : state.speech.slice();
    const promise = api("/api/readings", { method: "POST", body: { voice, speed, texts, warm: warmFrom } })
        .then(({ id }) => {
            pendingReading = null;
            reading = { id, article: article.id, voice, speed, intro: !!state.intro, durations: [] };
            return reading;
        }, error => {
            pendingReading = null;
            throw error;
        });
    pendingReading = { key, promise };
    return promise;
}

// Opening an article creates its reading and asks for the first clips right
// away, so pressing play rarely waits on Kokoro.
function warm() {
    if (!state.article || !state.speech.length) return;
    const intro = state.index === 0 && state.introPending && !!state.intro;
    ensureReading(startItem(state.index, intro)).catch(() => {});
}

function setBuffering(value) {
    document.body.classList.toggle("buffering", value);
}

function updateTransport() {
    document.body.classList.toggle("playing", state.playing);
    $("play").setAttribute("aria-label", state.playing ? "Pause" : "Play");
    const ready = !!state.article && state.sentences.length > 0;
    $("play").disabled = !ready;
    $("prev").disabled = !ready;
    $("next").disabled = !ready;
    if ("mediaSession" in navigator) navigator.mediaSession.playbackState = state.playing ? "playing" : "paused";
}

function syncIndex() {
    if (!reading) return;
    const item = playingItem(reading.durations, streamBase, audio.currentTime);
    const sentence = Math.max(0, Math.min(item - streamOffset, state.sentences.length - 1));
    if (streamOffset && item >= streamOffset) state.introPending = false;
    if (sentence === state.index) return;
    state.index = sentence;
    highlight();
    saveProgress();
}

function pollTiming(run) {
    clearTimeout(timingTimer);
    const current = reading;
    if (!current) return;
    const tick = async () => {
        if (run !== generation || !state.playing || reading !== current) return;
        try {
            const { durations } = await api(`/api/readings/${current.id}/timing`);
            if (run === generation && reading === current && Array.isArray(durations)) {
                current.durations = durations;
                syncIndex();
            }
        } catch {}
        if (run === generation && state.playing && reading === current)
            timingTimer = setTimeout(tick, document.hidden ? 5000 : 1500);
    };
    timingTimer = setTimeout(tick, 250);
}

async function play(index = state.index, { retry = false } = {}) {
    if (!state.article || !state.sentences.length) return;
    if (!retry) retried = false;
    voicePicker.cancel();
    const run = ++generation;
    state.index = Math.max(0, Math.min(index, state.sentences.length - 1));
    if (state.index !== 0) state.introPending = false;
    const intro = state.index === 0 && state.introPending && !!state.intro;
    state.playing = true;
    updateTransport();
    highlight();
    saveProgress();

    let current;
    try {
        current = await ensureReading(startItem(state.index, intro));
    } catch (error) {
        if (run !== generation) return;
        setBuffering(false);
        pause();
        toast(error.message === "Kokoro unavailable" ? "The voice server is not answering." : error.message);
        return;
    }
    if (run !== generation) return;

    streamOffset = current.intro ? 1 : 0;
    streamBase = intro ? 0 : state.index + streamOffset;
    audio.src = `/api/readings/${current.id}?at=${streamBase}`;
    audio.dataset.reading = current.id;
    audio.onended = () => {
        if (run === generation) finish();
    };
    setBuffering(true);
    try {
        await audio.play();
    } catch {
        if (run === generation) pause();
        return;
    }
    if (run !== generation) return;
    setBuffering(false);
    pollTiming(run);
}

function finish() {
    pause();
    saveProgress(2);
    const position = state.queue.findIndex(item => item.id === state.article?.id);
    const next = position >= 0 ? state.queue[position + 1] : null;
    if (next) {
        open(next.id, { autoplay: true });
    } else {
        toast("Finished reading");
        renderDrawer();
    }
}

function pause() {
    generation++;
    audio.pause();
    state.playing = false;
    pausedAt = Date.now();
    setBuffering(false);
    $("resume").hidden = true;
    clearTimeout(timingTimer);
    updateTransport();
}

function resume() {
    voicePicker.cancel();
    // A short pause picks up the buffered stream mid-sentence. After a long
    // pause the connection may be gone, so start the sentence it reached over
    // instead of letting the stream restart from its first byte.
    const live = reading && audio.dataset.reading === reading.id && !audio.error && audio.currentTime > 0
        && !audio.ended && Date.now() - pausedAt < 30000;
    if (live) {
        const run = ++generation;
        state.playing = true;
        state.following = true;
        updateTransport();
        syncIndex();
        highlight();
        audio.onended = () => {
            if (run === generation) finish();
        };
        audio.play().catch(() => run === generation && play(state.index));
        pollTiming(run);
        return;
    }
    state.following = true;
    play(state.index);
}

function toggle() {
    if (state.playing) pause();
    else resume();
}

function step(delta) {
    if (!state.article || !state.sentences.length) return;
    const index = Math.max(0, Math.min(state.index + delta, state.sentences.length - 1));
    state.introPending = false;
    state.following = true;
    $("resume").hidden = true;
    if (state.playing) {
        play(index);
    } else {
        state.index = index;
        highlight();
        saveProgress();
        warm();
    }
}

$("play").addEventListener("click", toggle);
$("prev").addEventListener("click", () => step(-1));
$("next").addEventListener("click", () => step(1));

audio.addEventListener("timeupdate", () => state.playing && syncIndex());
audio.addEventListener("waiting", () => state.playing && setBuffering(true));
audio.addEventListener("playing", () => {
    retried = false;
    setBuffering(false);
});
audio.addEventListener("error", () => {
    if (!state.playing || !audio.currentSrc) return;
    if (retried) {
        pause();
        toast("Playback stopped. Press play to continue.");
        return;
    }
    // A dropped stream (server restart, network change) is retried once from
    // the sentence that was playing.
    retried = true;
    const index = state.index;
    reading = null;
    pendingReading = null;
    audio.onended = null;
    setTimeout(() => {
        if (state.playing) play(index, { retry: true });
    }, 1000);
});

// Timers are throttled while the page is away; catch sentence tracking up as
// soon as it is back.
document.addEventListener("visibilitychange", () => {
    if (!document.hidden && state.playing && reading) pollTiming(generation);
});

function setMediaMetadata() {
    if (!("mediaSession" in navigator) || !state.article) return;
    navigator.mediaSession.metadata = new MediaMetadata({
        title: state.article.title || "Untitled",
        artist: state.article.author || state.article.domain || "",
        album: "sift",
    });
}

if ("mediaSession" in navigator) {
    const session = navigator.mediaSession;
    session.setActionHandler("play", () => !state.playing && resume());
    session.setActionHandler("pause", () => state.playing && pause());
    session.setActionHandler("previoustrack", () => step(-1));
    session.setActionHandler("nexttrack", () => step(1));
}

// ---------------------------------------------------------------- options

function setSheet(open) {
    if (!open) voicePicker.close();
    $("sheet").hidden = !open;
    document.body.classList.toggle("sheet-open", open);
    $("options-button").setAttribute("aria-expanded", String(open));
    if (open) voicePicker.load();
}

function formatSpeed(speed) {
    return `${Number(speed.toFixed(2))}×`;
}

function renderSpeed() {
    // The options button is an icon; a badge shows the speed only when it
    // is not the normal 1×, so a changed speed stays visible on the player.
    const badge = $("speed-badge");
    badge.textContent = formatSpeed(state.speed);
    badge.hidden = Math.abs(state.speed - 1) < 0.001;
    $("speed-value").textContent = state.speed.toFixed(2) + "×";
}

let settingsTimer = 0;
function saveSettings() {
    clearTimeout(settingsTimer);
    settingsTimer = setTimeout(() => api("/api/settings", { method: "PUT", body: { voice: state.voice, speed: state.speed } }).catch(() => {}), 500);
}

function changeSpeed(direction) {
    const position = SPEEDS.findIndex(value => value >= state.speed - 0.001);
    const next = SPEEDS[Math.max(0, Math.min(SPEEDS.length - 1, (position < 0 ? SPEEDS.length - 1 : position) + direction))];
    if (next === state.speed) return;
    state.speed = next;
    renderSpeed();
    saveSettings();
    restartForNewVoice();
}

function restartForNewVoice() {
    if (state.playing) play(state.index);
    else warm();
}

$("slower").addEventListener("click", () => changeSpeed(-1));
$("faster").addEventListener("click", () => changeSpeed(1));

const voicePicker = new VoicePicker({
    sheet: $("sheet"),
    audio,
    store,
    api,
    local,
    toast,
    current: () => state.voice,
    speed: () => state.speed,
    language: () => state.article?.language || "en",
    sentence: () => (state.introPending && state.index === 0 ? state.intro : state.speech[state.index]) || "",
    // A preview pauses the article and reports whether it was playing.
    onPreviewStart: () => {
        const wasPlaying = state.playing;
        if (wasPlaying) pause();
        return wasPlaying;
    },
    onPreviewEnd: () => resume(),
    onSelect: (id, resumePlayback) => {
        state.voice = id;
        saveSettings();
        if (resumePlayback) resume();
        else restartForNewVoice();
    },
});

function setTextSize(size) {
    const value = Math.max(14, Math.min(28, size));
    document.documentElement.style.setProperty("--text-size", value + "px");
    $("size-value").textContent = value;
    local.set("size", value);
    return value;
}
let textSize = setTextSize(local.get("size", 19));
$("smaller").addEventListener("click", () => (textSize = setTextSize(textSize - 1)));
$("larger").addEventListener("click", () => (textSize = setTextSize(textSize + 1)));

$("options-button").addEventListener("click", () => setSheet($("sheet").hidden));
$("queue-button").addEventListener("click", () => setDrawer(!document.body.classList.contains("drawer-open")));
$("scrim").addEventListener("click", () => {
    setSheet(false);
    if (!desktop.matches) setDrawer(false);
});

// Swipe the drawer closed toward the left edge.
{
    let start = null;
    const drawer = $("drawer");
    drawer.addEventListener("touchstart", event => {
        if (event.target.closest(".row, input")) return;
        start = { x: event.touches[0].clientX, y: event.touches[0].clientY };
    }, { passive: true });
    drawer.addEventListener("touchend", event => {
        if (!start || desktop.matches) return;
        const dx = event.changedTouches[0].clientX - start.x;
        const dy = event.changedTouches[0].clientY - start.y;
        if (dx < -60 && Math.abs(dx) > Math.abs(dy) * 1.5) setDrawer(false);
        start = null;
    });
}

desktop.addEventListener("change", () => setDrawer(desktop.matches ? local.get("sidebar", true) : false));

document.addEventListener("keydown", event => {
    if (event.target.closest("input, select, textarea") || event.metaKey || event.ctrlKey || event.altKey) return;
    if (event.key === "Escape") {
        if (voicePicker.open) voicePicker.back();
        else setSheet(false);
        if (!desktop.matches) setDrawer(false);
        return;
    }
    if (event.key === "v" || event.key === "V") {
        event.preventDefault();
        setSheet(true);
        voicePicker.show({ keyboard: true });
        return;
    }
    if (event.target.closest(".row-main") && (event.key === " " || event.key === "Enter")) return;
    const actions = {
        " ": toggle,
        "[": () => step(-1),
        "]": () => step(1),
        "-": () => changeSpeed(-1),
        "+": () => changeSpeed(1),
        "=": () => changeSpeed(1),
        f: () => setDrawer(!document.body.classList.contains("drawer-open")),
    };
    const action = actions[event.key];
    if (!action) return;
    event.preventDefault();
    action();
});

// ---------------------------------------------------------------- start

async function start() {
    setDrawer(desktop.matches ? local.get("sidebar", true) : false);
    updateTransport();
    let initial;
    try {
        initial = await api("/api/state");
    } catch (error) {
        toast("Could not load the library.");
        return;
    }
    state.queue = initial.queue;
    state.articles = initial.articles;
    state.voice = initial.settings.voice || "af_heart";
    state.speed = initial.settings.speed || 1;
    renderSpeed();
    voicePicker.renderValue();
    renderDrawer();

    // A link shared to the installed app arrives as ?url= or inside ?text=.
    const params = new URLSearchParams(location.search);
    const shared = params.get("url") || (params.get("text") || "").match(/https?:\/\/\S+/)?.[0];
    if (shared) {
        history.replaceState(null, "", "/");
        addPage(shared);
    }

    const last = local.get("last", null) || initial.settings.lastArticle;
    const known = id => state.articles.some(item => item.id === id);
    const first = known(last) ? last : state.queue[0]?.id;
    if (first) open(first);
    else if (!desktop.matches) setDrawer(true);
}

start();
