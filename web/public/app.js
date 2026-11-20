import { QueueList, renderLibrary } from "./queue.js";
import { renderArticle } from "./reader.js";
import { AudioStore, Dropped, announcement, cleanForSpeech, prefetchIndices } from "./tts.js";

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
    blocks: [],
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
    state.blocks = sentences.map(sentence => sentence.block);
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

function clip(index, intro) {
    return { text: intro ? state.intro : state.speech[index], voice: state.voice, speed: state.speed };
}

function wanted(index, intro) {
    const items = [];
    if (intro) items.push(clip(0, true));
    items.push(clip(index, false));
    for (const next of prefetchIndices(state.blocks, index)) items.push(clip(next, false));
    return items;
}

// Opening an article asks for its first clip right away, so pressing play
// rarely waits on Kokoro.
function warm() {
    if (!state.article || !state.speech.length) return;
    store.want(wanted(state.index, state.introPending).slice(0, 2));
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

async function play(index = state.index) {
    if (!state.article || !state.sentences.length) return;
    const run = ++generation;
    state.index = Math.max(0, Math.min(index, state.sentences.length - 1));
    if (state.index !== 0) state.introPending = false;
    const intro = state.introPending;
    state.playing = true;
    updateTransport();
    highlight();
    saveProgress();

    if (!intro && !state.speech[state.index]) return advance(run);

    const [entry] = store.want(wanted(state.index, intro));
    setBuffering(entry.status !== "ready");
    let url;
    try {
        url = await entry.promise;
    } catch (error) {
        if (run !== generation || error instanceof Dropped) return;
        setBuffering(false);
        pause();
        toast(error.message === "Kokoro unavailable" ? "The voice server is not answering." : error.message);
        return;
    }
    if (run !== generation) return;
    audio.src = url;
    audio.dataset.key = entry.key;
    bindEnded(run, intro);
    try {
        await audio.play();
    } catch {
        if (run === generation) pause();
        return;
    }
    if (run === generation) setBuffering(false);
}

function bindEnded(run, intro) {
    audio.onended = () => {
        if (run !== generation) return;
        if (intro) {
            state.introPending = false;
            play(state.index);
        } else {
            advance(run);
        }
    };
}

function advance(run) {
    if (run !== generation) return;
    if (state.index + 1 < state.sentences.length) {
        play(state.index + 1);
        return;
    }
    finish();
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
    setBuffering(false);
    $("resume").hidden = true;
    updateTransport();
}

function resume() {
    // A paused clip of the current sentence picks up mid-sentence; anything
    // else starts the sentence over.
    const intro = state.introPending;
    const current = store.entries.get(audio.dataset.key);
    const expected = clip(state.index, intro);
    if (current && current.url === audio.src && current.text === expected.text && current.voice === expected.voice
        && current.speed === expected.speed && audio.currentTime > 0 && !audio.ended) {
        const run = ++generation;
        state.playing = true;
        state.following = true;
        updateTransport();
        highlight();
        bindEnded(run, intro);
        store.want(wanted(state.index, intro));
        audio.play().catch(() => run === generation && pause());
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

audio.addEventListener("waiting", () => state.playing && setBuffering(true));
audio.addEventListener("playing", () => setBuffering(false));

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
    $("sheet").hidden = !open;
    document.body.classList.toggle("sheet-open", open);
    $("options-button").setAttribute("aria-expanded", String(open));
    if (open) loadVoices();
}

function formatSpeed(speed) {
    return `${Number(speed.toFixed(2))}×`;
}

function renderSpeed() {
    $("options-button").textContent = formatSpeed(state.speed);
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

const ACCENTS = { a: "US", b: "UK", e: "Spanish", f: "French", h: "Hindi", i: "Italian", j: "Japanese", p: "Portuguese", z: "Chinese" };
function voiceLabel(id) {
    const [prefix, name] = id.split("_");
    const accent = ACCENTS[prefix[0]] || prefix[0].toUpperCase();
    const gender = prefix[1] === "f" ? " ♀" : prefix[1] === "m" ? " ♂" : "";
    const title = name.split("_").map(part => part[0].toUpperCase() + part.slice(1)).join(" ");
    return `${title} · ${accent}${gender}`;
}

let voicesLoaded = false;
function renderVoices(voices) {
    const select = $("voice");
    const list = voices.includes(state.voice) ? voices : [state.voice, ...voices];
    select.replaceChildren(...list.map(id => {
        const option = document.createElement("option");
        option.value = id;
        option.textContent = voiceLabel(id);
        return option;
    }));
    select.value = state.voice;
}

async function loadVoices() {
    if (voicesLoaded) return;
    try {
        renderVoices((await api("/api/voices")).voices);
        voicesLoaded = true;
    } catch {
        renderVoices([]);
    }
}

$("voice").addEventListener("change", event => {
    state.voice = event.target.value;
    saveSettings();
    restartForNewVoice();
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
        setSheet(false);
        if (!desktop.matches) setDrawer(false);
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
    renderVoices([]);
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
