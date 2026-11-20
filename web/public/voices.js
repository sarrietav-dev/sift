// The voice picker: a second page of the options sheet. Voices are grouped by
// language with the article's language first and recent picks on top, and
// each row previews the sentence being read so a voice is judged on the
// text it would actually speak.

const LANGUAGES = [
    { prefix: "a", name: "English (US)", short: "US", search: "american english us usa", codes: ["en"] },
    { prefix: "b", name: "English (UK)", short: "UK", search: "british english uk", codes: ["en"] },
    { prefix: "e", name: "Spanish", short: "Spanish", search: "spanish español", codes: ["es"] },
    { prefix: "f", name: "French", short: "French", search: "french français", codes: ["fr"] },
    { prefix: "h", name: "Hindi", short: "Hindi", search: "hindi", codes: ["hi"] },
    { prefix: "i", name: "Italian", short: "Italian", search: "italian italiano", codes: ["it"] },
    { prefix: "j", name: "Japanese", short: "Japanese", search: "japanese 日本語", codes: ["ja"] },
    { prefix: "p", name: "Portuguese", short: "Portuguese", search: "portuguese brazilian português", codes: ["pt"] },
    { prefix: "z", name: "Chinese", short: "Chinese", search: "chinese mandarin 中文", codes: ["zh"] },
];
const OTHER = { prefix: "", name: "Other", short: "", search: "", codes: [] };
const SAMPLE = "Hello. This is how I sound reading to you.";
const RECENT_LIMIT = 3;

export function describeVoice(id) {
    const [prefix = "", ...rest] = id.split("_");
    const language = LANGUAGES.find(entry => entry.prefix === prefix[0]) || OTHER;
    const gender = prefix[1] === "f" ? "female" : prefix[1] === "m" ? "male" : "";
    // Kokoro keeps older models of some voices as "v0<name>".
    const version = rest[0]?.match(/^v(\d+)(.+)$/);
    if (version) rest[0] = version[2];
    const base = rest.join(" ").replace(/\b\w/g, letter => letter.toUpperCase()) || id;
    const name = version ? `${base} (v${version[1]})` : base;
    const symbol = gender === "female" ? " ♀" : gender === "male" ? " ♂" : "";
    return {
        id,
        name,
        language,
        gender,
        label: `${name} · ${language.short || prefix.toUpperCase()}${symbol}`,
        meta: [language.name, gender].filter(Boolean).join(" · "),
        search: `${id} ${name} ${language.name} ${language.search} ${gender}`.toLocaleLowerCase(),
    };
}

// Long sentences make slow previews; a couple of lines is enough to judge a voice.
export function previewText(text) {
    const clean = (text || "").replace(/\s+/g, " ").trim();
    if (!clean) return SAMPLE;
    if (clean.length <= 240) return clean;
    const cut = clean.slice(0, 240);
    return cut.slice(0, cut.lastIndexOf(" ") > 120 ? cut.lastIndexOf(" ") : 240) + "…";
}

function icon(id) {
    return document.getElementById(id).content.firstElementChild.cloneNode(true);
}

export class VoicePicker {
    constructor({ sheet, audio, store, api, local, toast, current, speed, language, sentence, onSelect, onPreviewStart, onPreviewEnd }) {
        this.sheet = sheet;
        this.main = sheet.querySelector("#sheet-main");
        this.page = sheet.querySelector("#voice-page");
        this.list = sheet.querySelector("#voice-list");
        this.search = sheet.querySelector("#voice-search");
        this.status = sheet.querySelector("#voice-status");
        this.audio = audio;
        this.store = store;
        this.api = api;
        this.local = local;
        this.toast = toast;
        this.current = current;
        this.speed = speed;
        this.language = language;
        this.sentence = sentence;
        this.onSelect = onSelect;
        this.onPreviewStart = onPreviewStart;
        this.onPreviewEnd = onPreviewEnd;
        this.voices = [];
        this.loaded = false;
        this.previewing = null;
        this.resumeAfter = false;
        this.previewRun = 0;

        sheet.querySelector("#voice-row").addEventListener("click", () => this.show());
        sheet.querySelector("#voice-back").addEventListener("click", () => this.back());
        sheet.querySelector("#voice-retry").addEventListener("click", () => this.load(true));
        this.search.addEventListener("input", () => this.render());
        this.search.addEventListener("keydown", event => this.searchKey(event));
        this.list.addEventListener("keydown", event => this.listKey(event));
        this.renderValue();
    }

    get open() {
        return !this.page.hidden;
    }

    renderValue() {
        this.sheet.querySelector("#voice-value").textContent = describeVoice(this.current()).label;
    }

    async load(force = false) {
        if (this.loaded && !force) return;
        this.status.hidden = true;
        try {
            this.voices = (await this.api("/api/voices")).voices;
            this.loaded = true;
        } catch {
            this.status.hidden = false;
        }
        if (this.open) this.render();
    }

    show({ keyboard = false } = {}) {
        this.main.hidden = true;
        this.page.hidden = false;
        this.search.value = "";
        this.focusSelected = keyboard;
        this.render();
        this.list.querySelector(".voice-pick[aria-current='true']")?.scrollIntoView({ block: "center" });
        this.load().finally(() => (this.focusSelected = false));
    }

    back() {
        this.stopPreview(true);
        this.page.hidden = true;
        this.main.hidden = false;
        this.sheet.querySelector("#voice-row").focus({ preventScroll: true });
    }

    // Called when the sheet closes: a preview that interrupted playback
    // hands it back.
    close() {
        this.stopPreview(true);
        this.page.hidden = true;
        this.main.hidden = false;
    }

    recent() {
        return this.local.get("voices:recent", []).filter(id => typeof id === "string");
    }

    remember(id) {
        this.local.set("voices:recent", [id, ...this.recent().filter(other => other !== id)].slice(0, RECENT_LIMIT));
    }

    groups(query) {
        const current = this.current();
        const ids = this.voices.includes(current) ? this.voices : [current, ...this.voices];
        const voices = ids.map(describeVoice).filter(voice => !query || query.split(/\s+/).every(word => voice.search.includes(word)));
        const groups = [];
        if (!query) {
            const recent = this.recent().filter(id => ids.includes(id)).map(describeVoice);
            if (recent.length) groups.push({ name: "Recent", voices: recent });
        }
        const language = (this.language() || "en").toLowerCase().split("-")[0];
        const order = [...LANGUAGES].sort((a, b) => Number(b.codes.includes(language)) - Number(a.codes.includes(language)));
        for (const entry of [...order, OTHER]) {
            const members = voices.filter(voice => voice.language === entry);
            if (members.length) groups.push({ name: entry.name, voices: members });
        }
        return groups;
    }

    render() {
        // Re-rendering when the voice list arrives must not drop keyboard focus.
        const focused = this.list.contains(document.activeElement)
            ? document.activeElement.closest(".voice-item")?.dataset.voice : null;
        const query = this.search.value.trim().toLocaleLowerCase();
        const current = this.current();
        const nodes = [];
        for (const group of this.groups(query)) {
            const heading = document.createElement("div");
            heading.className = "voice-group";
            heading.textContent = group.name;
            nodes.push(heading);
            for (const voice of group.voices) nodes.push(this.row(voice, voice.id === current));
        }
        if (!nodes.length) {
            const empty = document.createElement("p");
            empty.className = "hint voice-empty";
            empty.textContent = "No voices match.";
            nodes.push(empty);
        }
        this.list.replaceChildren(...nodes);
        this.markPreview();
        const target = focused || (this.focusSelected ? current : null);
        if (target) {
            const pick = this.list.querySelector(`.voice-item[data-voice="${CSS.escape(target)}"] .voice-pick`);
            pick?.focus({ preventScroll: true });
            pick?.scrollIntoView({ block: "center" });
        }
    }

    row(voice, selected) {
        const row = document.createElement("div");
        row.className = "voice-item";
        row.dataset.voice = voice.id;

        const pick = document.createElement("button");
        pick.type = "button";
        pick.className = "voice-pick";
        pick.setAttribute("aria-current", String(selected));
        const text = document.createElement("span");
        text.className = "voice-text";
        const name = document.createElement("span");
        name.className = "voice-name";
        name.textContent = voice.name;
        const meta = document.createElement("span");
        meta.className = "voice-meta";
        meta.textContent = voice.meta;
        text.append(name, meta);
        const check = icon("icon-check");
        check.classList.add("voice-check");
        pick.append(text, check);
        pick.addEventListener("click", () => this.select(voice.id));

        const preview = document.createElement("button");
        preview.type = "button";
        preview.className = "voice-preview";
        preview.tabIndex = -1;
        preview.setAttribute("aria-label", `Preview ${voice.name}`);
        const play = icon("icon-play-small");
        play.classList.add("voice-icon-play");
        const stop = icon("icon-stop");
        stop.classList.add("voice-icon-stop");
        const ring = document.createElement("span");
        ring.className = "voice-ring";
        preview.append(play, stop, ring);
        preview.addEventListener("click", () => this.preview(voice.id));

        row.append(pick, preview);
        return row;
    }

    markPreview(state = this.previewState) {
        for (const row of this.list.querySelectorAll(".voice-item")) {
            const active = row.dataset.voice === this.previewing;
            row.classList.toggle("loading", active && state === "loading");
            row.classList.toggle("playing", active && state === "playing");
            row.querySelector(".voice-preview").setAttribute("aria-pressed", String(active));
        }
    }

    async preview(id) {
        if (this.previewing === id) {
            this.stopPreview(true);
            return;
        }
        this.stopPreview(false);
        const run = ++this.previewRun;
        this.previewing = id;
        this.previewState = "loading";
        this.markPreview();
        if (this.onPreviewStart()) this.resumeAfter = true;
        const entry = this.store.preview(previewText(this.sentence()), id, this.speed());
        let url;
        try {
            url = await entry.promise;
        } catch (error) {
            if (run !== this.previewRun) return;
            this.toast(error.message === "Kokoro unavailable" ? "The voice server is not answering." : error.message);
            this.stopPreview(true);
            return;
        }
        if (run !== this.previewRun) return;
        this.audio.src = url;
        this.audio.dataset.key = "";
        this.audio.onended = () => run === this.previewRun && this.stopPreview(true);
        try {
            await this.audio.play();
        } catch {
            if (run === this.previewRun) this.stopPreview(true);
            return;
        }
        if (run !== this.previewRun) return;
        this.previewState = "playing";
        this.markPreview();
    }

    // Ends a preview. `resume` hands playback back if the preview paused it.
    stopPreview(resume) {
        if (!this.previewing) return;
        this.previewRun++;
        this.previewing = null;
        this.previewState = null;
        this.audio.onended = null;
        this.audio.pause();
        this.markPreview();
        if (resume && this.resumeAfter) {
            this.resumeAfter = false;
            this.onPreviewEnd();
        }
    }

    // Playback started from elsewhere (play button, lock screen): drop the
    // preview without resuming anything.
    cancel() {
        this.resumeAfter = false;
        this.stopPreview(false);
    }

    select(id) {
        const resume = this.resumeAfter;
        this.resumeAfter = false;
        this.stopPreview(false);
        this.remember(id);
        this.onSelect(id, resume);
        this.renderValue();
        this.back();
    }

    picks() {
        return [...this.list.querySelectorAll(".voice-pick")];
    }

    searchKey(event) {
        if (event.key === "Escape") {
            event.preventDefault();
            event.stopPropagation();
            this.back();
        } else if (event.key === "ArrowDown") {
            event.preventDefault();
            this.picks()[0]?.focus();
        } else if (event.key === "Enter") {
            event.preventDefault();
            const first = this.picks()[0];
            if (first) this.select(first.closest(".voice-item").dataset.voice);
        }
    }

    listKey(event) {
        const pick = event.target.closest(".voice-pick");
        if (!pick) return;
        const picks = this.picks();
        const index = picks.indexOf(pick);
        if (event.key === "ArrowDown" || event.key === "ArrowUp") {
            event.preventDefault();
            const next = index + (event.key === "ArrowDown" ? 1 : -1);
            if (next < 0) this.search.focus();
            else picks[Math.min(next, picks.length - 1)].focus();
        } else if (event.key === " ") {
            event.preventDefault();
            event.stopPropagation();
            this.preview(pick.closest(".voice-item").dataset.voice);
        } else if (event.key === "Escape") {
            event.preventDefault();
            event.stopPropagation();
            this.back();
        } else if (event.key === "Enter") {
            event.stopPropagation();
        }
    }
}
