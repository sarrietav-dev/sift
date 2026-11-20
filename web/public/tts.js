// Speech fetching for the player: which sentences to preload, and a small
// scheduler that asks the server for them in priority order.

// Sentence indices to preload: the rest of this paragraph, then the whole
// next one, crossing short blocks (headings, captions, list items) until at
// least six sentences are ahead. Same window as TtsEngine::prefetchIndices.
export function prefetchIndices(blocks, current) {
    const indices = [];
    if (current < 0 || current >= blocks.length) return indices;
    const currentBlock = blocks[current];
    if (currentBlock < 0) {
        for (let i = current + 1; i <= current + 8 && i < blocks.length; i++) indices.push(i);
        return indices;
    }
    const minimumAhead = 6;
    let targetBlock = currentBlock;
    let nextParagraph = false;
    for (let i = current + 1; i < blocks.length && i - current <= 48; i++) {
        const block = blocks[i];
        if (block >= 0 && block !== targetBlock) {
            if (nextParagraph && indices.length >= minimumAhead) break;
            nextParagraph = true;
            targetBlock = block;
        }
        indices.push(i);
    }
    return indices;
}

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

export class Dropped extends Error {}

// Kokoro on a CPU synthesizes one clip at a time well; two in flight keep it
// busy without starving the sentence the listener is waiting for. Requests
// fall out of the queue when playback moves away from them.
export class AudioStore {
    constructor({ concurrency = 2, limit = 96, fetcher = (...args) => fetch(...args) } = {}) {
        this.concurrency = concurrency;
        this.limit = limit;
        this.fetcher = fetcher;
        this.entries = new Map();
        this.wanted = [];
        this.active = 0;
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

    // `items` is [{text, voice, speed}] in the order they are needed; the
    // first one is the clip playback is waiting on.
    want(items) {
        const entries = items.filter(item => item.text).map(item => this.entry(item.text, item.voice, item.speed));
        this.wanted = entries.map(entry => entry.key);
        const keep = new Set(this.wanted);
        for (const [key, entry] of this.entries) {
            if (entry.status === "queued" && !keep.has(key)) {
                this.entries.delete(key);
                entry.reject(new Dropped());
            }
        }
        this.pump();
        return entries;
    }

    pump() {
        for (const key of this.wanted) {
            if (this.active >= this.concurrency) break;
            const entry = this.entries.get(key);
            if (entry && entry.status === "queued") this.load(entry);
        }
    }

    async load(entry) {
        entry.status = "loading";
        this.active++;
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
            this.active--;
            this.evict();
            this.pump();
        }
    }

    evict() {
        const ready = [...this.entries.values()].filter(entry => entry.status === "ready");
        if (ready.length <= this.limit) return;
        const keep = new Set(this.wanted);
        ready.sort((a, b) => a.used - b.used);
        for (const entry of ready.slice(0, ready.length - this.limit)) {
            if (keep.has(entry.key)) continue;
            URL.revokeObjectURL(entry.url);
            this.entries.delete(entry.key);
        }
    }
}
