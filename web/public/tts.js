// Speech helpers for the player: cleaning text for Kokoro, the announcement
// an article opens with, mapping audio time back to a sentence, and a small
// cache that fetches voice previews.

export function cleanForSpeech(text) {
    return String(text || "").replace(/\[\s*\d+\s*\]/g, "").replace(/\s+/g, " ").trim().slice(0, 4000);
}

// The way an article introduces itself before its first sentence.
export function announcement(title, author) {
    const cleanTitle = (title || "").replace(/\s+/g, " ").trim();
    if (!cleanTitle) return "";
    const cleanAuthor = (author || "").replace(/\s+/g, " ").trim();
    return cleanAuthor ? `${cleanTitle}. By ${cleanAuthor}.` : `${cleanTitle}.`;
}

// Which item of a continuous reading is playing `time` seconds into a stream
// that started at `base`. Durations are known only for clips the server has
// synthesized already, so stop at the first gap and report the last item that
// is certain; the next poll fills the gap in.
export function playingItem(durations, base, time) {
    let elapsed = 0;
    let last = base;
    for (let i = base; i < durations.length; i++) {
        const duration = durations[i];
        if (duration == null) return last;
        last = i;
        if (time < elapsed + duration) return i;
        elapsed += duration;
    }
    return last;
}

// Kokoro on a CPU synthesizes a preview in about a second. Entries are kept
// by voice, speed, and text so replaying one never asks twice; the oldest
// previews are dropped once the cache fills.
export class AudioStore {
    constructor({ limit = 96, fetcher = (...args) => fetch(...args) } = {}) {
        this.limit = limit;
        this.fetcher = fetcher;
        this.entries = new Map();
    }

    static key(text, voice, speed) {
        return `${voice}|${speed.toFixed(2)}|${text}`;
    }

    entry(text, voice, speed) {
        const key = AudioStore.key(text, voice, speed);
        let entry = this.entries.get(key);
        if (!entry) {
            entry = { key, text, voice, speed, status: "queued", url: null, used: 0 };
            entry.promise = new Promise((resolve, reject) => {
                entry.resolve = resolve;
                entry.reject = reject;
            });
            entry.promise.catch(() => {});
            this.entries.set(key, entry);
        }
        entry.used = performance.now();
        return entry;
    }

    preview(text, voice, speed) {
        const entry = this.entry(text, voice, speed);
        if (entry.status === "queued") this.load(entry);
        return entry;
    }

    async load(entry) {
        entry.status = "loading";
        try {
            const query = new URLSearchParams({ voice: entry.voice, speed: entry.speed.toFixed(2), text: entry.text });
            const response = await this.fetcher("/api/speech?" + query);
            if (!response.ok) throw new Error((await response.json().catch(() => ({}))).error || "Speech unavailable");
            entry.url = URL.createObjectURL(await response.blob());
            entry.status = "ready";
            entry.resolve(entry.url);
        } catch (error) {
            this.entries.delete(entry.key);
            entry.status = "error";
            entry.reject(error);
        } finally {
            this.evict();
        }
    }

    evict() {
        const ready = [...this.entries.values()].filter(entry => entry.status === "ready");
        if (ready.length <= this.limit) return;
        ready.sort((a, b) => a.used - b.used);
        for (const entry of ready.slice(0, ready.length - this.limit)) {
            URL.revokeObjectURL(entry.url);
            this.entries.delete(entry.key);
        }
    }
}
