// Kokoro proxy with a disk cache keyed like the desktop's (voice, speed,
// text), so replaying a paragraph or reopening an article never waits on
// synthesis twice. Identical requests already in flight share one call.
//
// Kokoro runs concurrent requests together and every one of them slows down
// (six at once made the first wait four times as long), so clips are made one
// at a time: urgent ones first (the clip a listener is waiting on), then a
// playing stream's next clips, then background work such as the opening of
// the next article. A clip nobody is waiting for any more is dropped or its
// request aborted.
import { existsSync, mkdirSync, readdirSync, renameSync, statSync, unlinkSync, utimesSync, writeFileSync } from "node:fs";
import { join } from "node:path";

export class SpeechError extends Error {}

const BACKGROUND_QUIET = 500;

// Words after which a Roman numeral is a number, not letters. Same rules as
// src/speech.cpp in the desktop app; change both together.
const LABELS = new Set(("part parts chapter chapters section sections book books volume volumes vol title titles " +
    "appendix appendices annex annexes act acts scene scenes article articles clause clauses exhibit exhibits " +
    "figure figures table tables step steps phase phases level levels tier tiers stage stages unit units " +
    "module modules rule rules division divisions grade grades class classes item items question questions " +
    "example examples exercise exercises war wars episode episodes season seasons round rounds type types " +
    "category categories").split(" "));
const JOINERS = new Set(["and", "or", "through", "to", "versus", "vs"]);
const ONES = ["", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten", "eleven", "twelve",
    "thirteen", "fourteen", "fifteen", "sixteen", "seventeen", "eighteen", "nineteen"];
const ROMAN = [[1000, "M"], [900, "CM"], [500, "D"], [400, "CD"], [100, "C"], [90, "XC"], [50, "L"], [40, "XL"],
    [10, "X"], [9, "IX"], [5, "V"], [4, "IV"], [1, "I"]];

function numberWord(value) {
    if (value < 20) return ONES[value];
    const tens = value < 30 ? "twenty" : "thirty";
    return value % 10 ? `${tens}-${ONES[value % 10]}` : tens;
}

// "III" → "three". Only canonical numerals up to thirty-nine.
function romanWord(token) {
    const values = { I: 1, V: 5, X: 10, L: 50, C: 100, D: 500, M: 1000 };
    let total = 0;
    let previous = 0;
    for (const char of [...token.toUpperCase()].reverse()) {
        const value = values[char];
        if (!value) return null;
        if (value < previous) total -= value;
        else { total += value; previous = value; }
    }
    if (total <= 0 || total > 39) return null;
    let roman = "";
    let rest = total;
    for (const [step, digits] of ROMAN) while (rest >= step) { roman += digits; rest -= step; }
    return roman === token.toUpperCase() ? numberWord(total) : null;
}

// Kokoro reads a bare Roman numeral as letters, so "World War II" comes out
// as "world war eye eye". After a label such as "Part" or "War", a numeral is
// a number. "I" is also the pronoun ("the article I wrote"), so it counts
// only after a capitalised label.
export function spokenNumerals(text) {
    let afterLabel = false;
    let inList = false;
    let capitalLabel = false;
    return text.replace(/(\S+)(\s*)/g, (whole, body, space) => {
        const match = body.match(/^([^\p{L}\p{N}]*)(.*?)([^\p{L}\p{N}]*)$/u);
        const [, lead, core, trail] = match;
        if (!core) return whole;
        const word = core.toLowerCase();
        if (LABELS.has(word)) {
            afterLabel = true;
            inList = false;
            capitalLabel = core[0] !== core[0].toLowerCase();
            return whole;
        }
        if ((afterLabel || inList) && JOINERS.has(word)) {
            afterLabel = false;
            inList = true;
            return whole;
        }
        if (afterLabel || inList) {
            const said = core === "I" && !capitalLabel ? null : romanWord(core);
            afterLabel = false;
            inList = !!said;
            if (said) return lead + said + trail + space;
        }
        return whole;
    });
}

export function cleanForSpeech(text) {
    return spokenNumerals(String(text || "").replace(/\[\s*\d+\s*\]/g, "").replace(/\s+/g, " ").trim());
}

export class Speech {
    constructor({ endpoint, cacheDirectory, cacheLimitMb = 1024, fetcher = fetch }) {
        this.endpoint = endpoint.replace(/\/$/, "");
        this.cacheDirectory = cacheDirectory;
        this.cacheLimit = cacheLimitMb * 1024 * 1024;
        this.fetcher = fetcher;
        this.inflight = new Map();
        this.waiting = [];
        this.busy = false;
        this.quietTimer = null;
        // When a stream's clip finished or was dropped. A jump cancels one
        // clip and asks for the next a moment later; background work starting
        // in that moment would make the listener wait behind it.
        this.lastForeground = 0;
        this.voiceList = null;
        this.writes = 0;
        mkdirSync(cacheDirectory, { recursive: true });
    }

    async voices() {
        if (this.voiceList && Date.now() - this.voiceList.at < 10 * 60 * 1000) return this.voiceList.names;
        const response = await this.fetcher(this.endpoint + "/v1/audio/voices", { signal: AbortSignal.timeout(10000) });
        if (!response.ok) throw new SpeechError("Kokoro unavailable");
        const body = await response.json();
        const names = (body.voices || body || []).map(voice => typeof voice === "string" ? voice : voice.id || voice.name)
            .filter(name => /^[a-z]{2}_[a-z0-9_]+$/.test(name)).sort();
        this.voiceList = { at: Date.now(), names };
        return names;
    }

    path(voice, speed, text) {
        const hash = new Bun.CryptoHasher("sha256").update(`${voice}|${speed.toFixed(2)}|${text}|kokoro`).digest("hex");
        return join(this.cacheDirectory, hash.slice(0, 2), hash + ".mp3");
    }

    // `urgent` puts the clip ahead of everything, `background` behind
    // everything. Aborting `signal` gives up on the clip; it is dropped, or
    // its request aborted, once no caller is waiting for it.
    async audio(input, voice, speed, { urgent = false, background = false, signal } = {}) {
        const text = cleanForSpeech(input);
        if (!text || text.length > 4000) throw new SpeechError("Invalid speech request");
        if (!/^[a-z]{2}_[a-z0-9_]+$/.test(voice) || !(speed >= 0.5 && speed <= 2)) throw new SpeechError("Invalid speech request");
        const path = this.path(voice, speed, text);
        if (existsSync(path)) {
            const now = new Date();
            try { utimesSync(path, now, now); } catch {}
            return path;
        }
        if (signal?.aborted) throw new SpeechError("Cancelled");
        let job = this.inflight.get(path);
        if (!job) {
            job = { path, text, voice, speed, rank: 2, started: false, waiters: 0, controller: new AbortController() };
            job.promise = new Promise((resolve, reject) => { job.resolve = resolve; job.reject = reject; });
            job.promise.catch(() => {});
            this.inflight.set(path, job);
            this.waiting.push(job);
        }
        job.waiters++;
        job.rank = Math.min(job.rank, urgent ? 0 : background ? 2 : 1);
        signal?.addEventListener("abort", () => {
            if (--job.waiters <= 0) this.cancel(job);
        }, { once: true });
        this.next();
        return job.promise;
    }

    cancel(job) {
        if (this.inflight.get(job.path) !== job) return;
        this.inflight.delete(job.path);
        const at = this.waiting.indexOf(job);
        if (at >= 0) this.waiting.splice(at, 1);
        if (job.started) job.controller.abort();
        if (job.rank < 2) this.lastForeground = performance.now();
        job.reject(new SpeechError("Cancelled"));
    }

    next() {
        if (this.busy || !this.waiting.length) return;
        let at = 0;
        for (let i = 1; i < this.waiting.length; i++) if (this.waiting[i].rank < this.waiting[at].rank) at = i;
        const quiet = BACKGROUND_QUIET - (performance.now() - this.lastForeground);
        if (this.waiting[at].rank === 2 && quiet > 0) {
            clearTimeout(this.quietTimer);
            this.quietTimer = setTimeout(() => this.next(), quiet);
            return;
        }
        const [job] = this.waiting.splice(at, 1);
        job.started = true;
        this.busy = true;
        this.synthesize(job.text, job.voice, job.speed, job.path, job.controller.signal)
            .then(job.resolve, job.reject)
            .finally(() => {
                if (this.inflight.get(job.path) === job) this.inflight.delete(job.path);
                if (job.rank < 2) this.lastForeground = performance.now();
                this.busy = false;
                this.next();
            });
    }

    async synthesize(text, voice, speed, path, signal) {
        let response;
        try {
            // Streamed, so an aborted request stops Kokoro after its current
            // segment; the bytes are the same MP3 either way.
            response = await this.fetcher(this.endpoint + "/v1/audio/speech", {
                method: "POST",
                headers: { "content-type": "application/json" },
                body: JSON.stringify({ model: "kokoro", input: text, voice, speed, response_format: "mp3", stream: true }),
                signal: AbortSignal.any([signal, AbortSignal.timeout(120000)]),
            });
        } catch {
            throw new SpeechError(signal.aborted ? "Cancelled" : "Kokoro unavailable");
        }
        if (!response.ok) throw new SpeechError("Kokoro unavailable");
        let audio;
        try {
            audio = new Uint8Array(await response.arrayBuffer());
        } catch {
            throw new SpeechError(signal.aborted ? "Cancelled" : "Kokoro unavailable");
        }
        if (!audio.length) throw new SpeechError("Kokoro returned no audio");
        mkdirSync(join(path, ".."), { recursive: true });
        writeFileSync(path + ".tmp", audio);
        renameSync(path + ".tmp", path);
        if (++this.writes % 50 === 0) this.prune();
        return path;
    }

    prune() {
        const files = [];
        let total = 0;
        for (const folder of readdirSync(this.cacheDirectory)) {
            const directory = join(this.cacheDirectory, folder);
            let names;
            try { names = readdirSync(directory); } catch { continue; }
            for (const name of names) {
                if (!name.endsWith(".mp3")) continue;
                const file = join(directory, name);
                const info = statSync(file);
                total += info.size;
                files.push({ file, size: info.size, time: info.mtimeMs });
            }
        }
        if (total <= this.cacheLimit) return;
        files.sort((a, b) => a.time - b.time);
        for (const entry of files) {
            if (total <= this.cacheLimit) break;
            try { unlinkSync(entry.file); total -= entry.size; } catch {}
        }
    }
}
