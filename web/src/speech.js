// Kokoro proxy with a disk cache keyed like the desktop's (voice, speed,
// text), so replaying a paragraph or reopening an article never waits on
// synthesis twice. Identical requests already in flight share one call.
import { existsSync, mkdirSync, readdirSync, renameSync, statSync, unlinkSync, utimesSync, writeFileSync } from "node:fs";
import { join } from "node:path";

export class SpeechError extends Error {}

export function cleanForSpeech(text) {
    return String(text || "").replace(/\[\s*\d+\s*\]/g, "").replace(/\s+/g, " ").trim();
}

export class Speech {
    constructor({ endpoint, cacheDirectory, cacheLimitMb = 1024, fetcher = fetch }) {
        this.endpoint = endpoint.replace(/\/$/, "");
        this.cacheDirectory = cacheDirectory;
        this.cacheLimit = cacheLimitMb * 1024 * 1024;
        this.fetcher = fetcher;
        this.inflight = new Map();
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

    async audio(input, voice, speed) {
        const text = cleanForSpeech(input);
        if (!text || text.length > 4000) throw new SpeechError("Invalid speech request");
        if (!/^[a-z]{2}_[a-z0-9_]+$/.test(voice) || !(speed >= 0.5 && speed <= 2)) throw new SpeechError("Invalid speech request");
        const path = this.path(voice, speed, text);
        if (existsSync(path)) {
            const now = new Date();
            try { utimesSync(path, now, now); } catch {}
            return path;
        }
        if (this.inflight.has(path)) return this.inflight.get(path);
        const job = this.synthesize(text, voice, speed, path).finally(() => this.inflight.delete(path));
        this.inflight.set(path, job);
        return job;
    }

    async synthesize(text, voice, speed, path) {
        let response;
        try {
            response = await this.fetcher(this.endpoint + "/v1/audio/speech", {
                method: "POST",
                headers: { "content-type": "application/json" },
                body: JSON.stringify({ model: "kokoro", input: text, voice, speed, response_format: "mp3", stream: false }),
                signal: AbortSignal.timeout(120000),
            });
        } catch {
            throw new SpeechError("Kokoro unavailable");
        }
        if (!response.ok) throw new SpeechError("Kokoro unavailable");
        const audio = new Uint8Array(await response.arrayBuffer());
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
