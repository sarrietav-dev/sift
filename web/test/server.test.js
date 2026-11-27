import { afterAll, beforeAll, describe, expect, test } from "bun:test";
import { mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { createApp } from "../server.js";
import { isPrivateAddress } from "../src/extract.js";
import { splitMp3 } from "../src/mp3.js";
import { plainText, sanitize, textToHtml } from "../src/sanitize.js";
import { Speech, spokenNumerals } from "../src/speech.js";
import { announcement, cleanForSpeech, playingItem } from "../public/tts.js";
import { describeVoice, previewText } from "../public/voices.js";

const HOST = "sift.example";
const PAGE = `<!doctype html><html><head><title>A page</title><link rel="canonical" href="/canonical"></head>
<body><article><h1>A page</h1><p>First sentence. Second sentence with <a href="/next" onclick="x()">a link</a>.</p>
<p><img src="/a.png" onerror="alert(1)"></p><script>alert(1)</script><p>${"Words go here. ".repeat(40)}</p></article></body></html>`;

// A minimal MPEG-2 Layer III clip: an ID3v2 tag and then `frames` 384-byte
// frames at 24 kHz, which is the shape Kokoro returns.
function mp3(frames = 200) {
    const frame = new Uint8Array(384);
    frame.set([0xff, 0xf3, 0xc4, 0xc4]);
    const bytes = new Uint8Array(10 + frames * frame.length);
    bytes.set([0x49, 0x44, 0x33, 4, 0, 0, 0, 0, 0, 0]);
    for (let i = 0; i < frames; i++) bytes.set(frame, 10 + i * frame.length);
    return bytes;
}

let directory;
let app;
let kokoroCalls = 0;

const fetcher = async (url, options = {}) => {
    if (String(url).endsWith("/v1/audio/speech")) {
        kokoroCalls++;
        return new Response(mp3(), { headers: { "content-type": "audio/mpeg" } });
    }
    if (String(url).endsWith("/v1/audio/voices")) return Response.json({ voices: ["af_heart", "am_michael", "bad voice"] });
    return new Response(PAGE, { headers: { "content-type": "text/html; charset=utf-8" } });
};

function call(method, path, body, headers = {}) {
    return app.handle(new Request("http://" + HOST + path, {
        method,
        headers: { host: HOST, origin: "http://" + HOST, ...(body ? { "content-type": "application/json" } : {}), ...headers },
        body: body ? JSON.stringify(body) : undefined,
    }));
}

beforeAll(() => {
    directory = mkdtempSync(join(tmpdir(), "sift-web-"));
    app = createApp({ dataDirectory: directory, kokoroUrl: "http://kokoro", hosts: [HOST], fetcher });
});

afterAll(() => {
    app.library.close();
    rmSync(directory, { recursive: true, force: true });
});

describe("sanitize", () => {
    test("drops scripts and handlers, resolves URLs, prefixes ids", () => {
        const html = sanitize(`<p id="x" style="color:red" onclick="a()">Hi <a href="javascript:alert(1)">bad</a>
            <a href="/ok">ok</a> <a href="#x">jump</a></p><img src="a.png" onerror="b()"><iframe src="//evil"></iframe>
            <script>c()</script><custom-tag>kept text</custom-tag>`, "https://example.com/post/");
        expect(html).not.toContain("onclick");
        expect(html).not.toContain("onerror");
        expect(html).not.toContain("script");
        expect(html).not.toContain("iframe");
        expect(html).not.toContain("javascript:");
        expect(html).not.toContain("style=");
        expect(html).toContain('id="a-x"');
        expect(html).toContain('href="#a-x"');
        expect(html).toContain('href="https://example.com/ok"');
        expect(html).toContain('src="https://example.com/post/a.png"');
        expect(html).toContain("kept text");
        expect(html).not.toContain("custom-tag");
    });

    test("plain text keeps one line per block", () => {
        expect(plainText("<ul><li>One</li><li>Two <b>bold</b></li></ul><p>Three</p>")).toBe("One\n\nTwo bold\n\nThree");
    });

    test("text fallback escapes and splits paragraphs", () => {
        expect(textToHtml("a <b>\n\nsecond")).toBe("<p>a &lt;b&gt;</p>\n<p>second</p>");
    });
});

describe("speech helpers", () => {
    test("audio time maps back to the item that is playing", () => {
        expect(playingItem([1, 2, 3], 0, 0)).toBe(0);
        expect(playingItem([1, 2, 3], 0, 1.5)).toBe(1);
        expect(playingItem([1, 2, 3], 1, 0.2)).toBe(1);
        expect(playingItem([1, 2, null, 3], 0, 2.5)).toBe(1);
        expect(playingItem([1, 2, 3], 0, 99)).toBe(2);
    });

    test("cleaning and the title announcement match the desktop", () => {
        expect(cleanForSpeech("A claim [12] here.\n Next")).toBe("A claim here. Next");
        expect(announcement("Title", "Ann")).toBe("Title. By Ann.");
        expect(announcement("Title", "")).toBe("Title.");
    });
});

describe("mp3", () => {
    test("drops tags, keeps frames, and measures the clip", () => {
        const { audio, duration } = splitMp3(mp3(25));
        expect(audio.length).toBe(25 * 384);
        expect(audio[0]).toBe(0xff);
        expect(duration).toBeCloseTo(0.6, 5);
        expect(splitMp3(new Uint8Array([1, 2, 3]))).toMatchObject({ duration: null });
    });
});

describe("speech queue", () => {
    test("numerals after a label are spoken as numbers, like the desktop", () => {
        expect(spokenNumerals("Part I of this book")).toBe("Part one of this book");
        expect(spokenNumerals("after World War II ended")).toBe("after World War two ended");
        expect(spokenNumerals("Chapter IV: the next step")).toBe("Chapter four: the next step");
        expect(spokenNumerals("(Part I)")).toBe("(Part one)");
        expect(spokenNumerals("Parts I and II")).toBe("Parts one and two");
        expect(spokenNumerals("Phase XXIV")).toBe("Phase twenty-four");
        expect(spokenNumerals("Henry VIII and I went")).toBe("Henry VIII and I went");
        expect(spokenNumerals("the article I wrote")).toBe("the article I wrote");
        expect(spokenNumerals("Type IIII and Class C")).toBe("Type IIII and Class C");
    });

    function slowKokoro() {
        const log = { order: [], open: 0, maxOpen: 0, aborted: [], release: [] };
        const fetcher = (url, options) => {
            const text = JSON.parse(options.body).input;
            log.order.push(text);
            log.open++;
            log.maxOpen = Math.max(log.maxOpen, log.open);
            return new Promise((resolve, reject) => {
                options.signal.addEventListener("abort", () => {
                    log.open--;
                    log.aborted.push(text);
                    reject(new Error("aborted"));
                });
                log.release.push(() => {
                    log.open--;
                    resolve(new Response(new Uint8Array([1, 2, 3])));
                });
            });
        };
        return { log, fetcher };
    }

    const settle = () => Bun.sleep(5);

    test("clips are made one at a time, urgent first", async () => {
        const { log, fetcher } = slowKokoro();
        const speech = new Speech({ endpoint: "http://kokoro", cacheDirectory: join(directory, "queue-a"), fetcher });
        const jobs = [
            speech.audio("First.", "af_heart", 1),
            speech.audio("Second.", "af_heart", 1),
            speech.audio("Waited on.", "af_heart", 1, { urgent: true }),
        ];
        await settle();
        expect(log.order).toEqual(["First."]);
        log.release.shift()();
        await settle();
        expect(log.order).toEqual(["First.", "Waited on."]);
        log.release.shift()();
        await settle();
        log.release.shift()();
        await Promise.all(jobs);
        expect(log.order).toEqual(["First.", "Waited on.", "Second."]);
        expect(log.maxOpen).toBe(1);
    });

    test("a clip nobody waits for is dropped or aborted", async () => {
        const { log, fetcher } = slowKokoro();
        const speech = new Speech({ endpoint: "http://kokoro", cacheDirectory: join(directory, "queue-b"), fetcher });
        const stream = new AbortController();
        const other = new AbortController();
        const running = speech.audio("Playing.", "af_heart", 1, { signal: stream.signal });
        const queued = speech.audio("Next.", "af_heart", 1, { signal: stream.signal });
        // A second listener of the running clip keeps it alive.
        const shared = speech.audio("Playing.", "af_heart", 1, { signal: other.signal });
        await settle();
        stream.abort();
        await expect(queued).rejects.toThrow("Cancelled");
        expect(log.aborted).toEqual([]);
        const settled = Promise.allSettled([running, shared]);
        other.abort();
        for (const result of await settled) expect(result.reason?.message).toBe("Cancelled");
        await settle();
        expect(log.aborted).toEqual(["Playing."]);
        expect(log.order).toEqual(["Playing."]);
        // The queue moves on after an abort.
        const after = speech.audio("After.", "af_heart", 1);
        await settle();
        log.release.at(-1)();
        expect(await after).toEndWith(".mp3");
    });
});

describe("voice picker helpers", () => {
    test("voice ids read as name, accent, and gender", () => {
        expect(describeVoice("bf_emma")).toMatchObject({ name: "Emma", label: "Emma · UK ♀", meta: "English (UK) · female" });
        expect(describeVoice("am_v0michael").name).toBe("Michael (v0)");
        expect(describeVoice("af_amelia_inno").name).toBe("Amelia Inno");
        expect(describeVoice("zm_yunjian").search).toContain("mandarin");
    });

    test("previews read a short sentence, or a sample without one", () => {
        expect(previewText("")).toBe("Hello. This is how I sound reading to you.");
        expect(previewText("Short one.")).toBe("Short one.");
        const long = previewText("word ".repeat(100));
        expect(long.length).toBeLessThanOrEqual(241);
        expect(long.endsWith("…")).toBe(true);
    });

    test("a voice previews in its own language, not the article's", () => {
        const article = "The quick brown fox jumps over the lazy dog.";
        expect(previewText(article, describeVoice("ef_dora").language.codes, "en")).toBe("Hola. Así suena mi voz cuando te leo.");
        expect(previewText(article, describeVoice("bf_emma").language.codes, "es")).toBe("Hello. This is how I sound reading to you.");
        expect(previewText("El zorro salta.", describeVoice("ef_dora").language.codes, "es")).toBe("El zorro salta.");
        for (const [id, start] of [["ff_siwis", "Bonjour"], ["if_sara", "Ciao"], ["jf_alpha", "こんにちは"], ["pf_dora", "Olá"], ["zf_xiaobei", "你好"], ["hf_alpha", "नमस्ते"]])
            expect(previewText(article, describeVoice(id).language.codes, "en").startsWith(start)).toBe(true);
        expect(previewText(article, describeVoice("of_x").language.codes, "en")).toBe(article);
    });
});

test("private addresses are refused", () => {
    for (const address of ["127.0.0.1", "10.1.2.3", "172.20.0.1", "192.168.1.1", "100.100.1.1", "169.254.0.1", "::1", "fd00::1", "::ffff:127.0.0.1"])
        expect(isPrivateAddress(address)).toBe(true);
    for (const address of ["1.1.1.1", "93.184.216.34", "2606:4700::1111"]) expect(isPrivateAddress(address)).toBe(false);
});

describe("api", () => {
    let id;

    test("rejects unknown hosts and cross-origin writes", async () => {
        expect((await call("GET", "/api/state", null, { host: "evil.example" })).status).toBe(400);
        expect((await call("PUT", "/api/queue", { ids: [] }, { origin: "https://evil.example" })).status).toBe(403);
    });

    test("adding a page extracts, saves, and queues it", async () => {
        const response = await call("POST", "/api/articles", { url: "https://93.184.216.34/post" });
        expect(response.status).toBe(200);
        const body = await response.json();
        id = body.id;
        expect(body.queue.map(item => item.id)).toEqual([id]);
        const article = await (await call("GET", "/api/articles/" + id)).json();
        expect(article.title).toBe("A page");
        expect(article.source).toBe("snapshot");
        expect(article.html).toContain("First sentence.");
        expect(article.html).not.toContain("onerror");
        expect(article.html).not.toContain("<script");
    });

    test("re-adding the same page keeps one article", async () => {
        const body = await (await call("POST", "/api/articles", { url: "https://93.184.216.34/post" })).json();
        expect(body.id).toBe(id);
        expect(body.articles.length).toBe(1);
    });

    test("queue reorder, remove, and progress persist", async () => {
        app.library.db.query("INSERT INTO articles (id, url, title, clipped_at, content_text) VALUES ('b', 'https://b.example', 'B', '2020', 'Saved text.')").run();
        let queue = (await (await call("POST", "/api/queue/b")).json()).queue;
        expect(queue.map(item => item.id)).toEqual([id, "b"]);
        queue = (await (await call("PUT", "/api/queue", { ids: ["b", id, "missing"] })).json()).queue;
        expect(queue.map(item => item.id)).toEqual(["b", id]);
        queue = (await (await call("DELETE", "/api/queue/b")).json()).queue;
        expect(queue.map(item => item.id)).toEqual([id]);
        expect((await call("PUT", `/api/articles/${id}/progress`, { sentence: 3, readState: 1 })).status).toBe(200);
        const state = await (await call("GET", "/api/state")).json();
        expect(state.articles.find(item => item.id === id)).toMatchObject({ lastSentence: 3, readState: 1 });
    });

    test("speech is cached on disk", async () => {
        const path = "/api/speech?voice=af_heart&speed=1.00&text=" + encodeURIComponent("Hello there.");
        const first = await call("GET", path);
        expect(first.status).toBe(200);
        expect(first.headers.get("content-type")).toBe("audio/mpeg");
        await call("GET", path);
        expect(kokoroCalls).toBe(1);
        expect((await call("GET", "/api/speech?voice=bad&speed=1&text=x")).status).toBe(400);
    });

    test("a reading streams its clips and reports durations", async () => {
        const created = await (await call("POST", "/api/readings", {
            voice: "af_heart",
            speed: 1,
            texts: ["One.", "", "Two."],
        })).json();
        expect(typeof created.id).toBe("string");
        const response = await call("GET", `/api/readings/${created.id}`);
        expect(response.status).toBe(200);
        expect(response.headers.get("content-type")).toBe("audio/mpeg");
        const bytes = new Uint8Array(await response.arrayBuffer());
        expect(bytes.length).toBe(2 * 200 * 384);
        expect([...bytes.slice(0, 4)]).toEqual([0xff, 0xf3, 0xc4, 0xc4]);
        expect(new TextDecoder("latin1").decode(bytes)).not.toContain("ID3");
        const timing = await (await call("GET", `/api/readings/${created.id}/timing`)).json();
        expect(timing.durations[0]).toBeCloseTo(4.8, 5);
        expect(timing.durations[1]).toBe(0);
        expect(timing.durations[2]).toBeCloseTo(4.8, 5);
    });

    test("readings validate their input and reject unknown ids", async () => {
        expect((await call("POST", "/api/readings", { voice: "bad", speed: 1, texts: ["x"] })).status).toBe(400);
        expect((await call("POST", "/api/readings", { voice: "af_heart", speed: 3, texts: ["x"] })).status).toBe(400);
        expect((await call("POST", "/api/readings", { voice: "af_heart", speed: 1, texts: [] })).status).toBe(400);
        expect((await call("POST", "/api/readings", { voice: "af_heart", speed: 1, texts: [1] })).status).toBe(400);
        expect((await call("GET", "/api/readings/missing")).status).toBe(404);
        expect((await call("GET", "/api/readings/missing/timing")).status).toBe(404);
    });

    test("voices come from Kokoro, filtered", async () => {
        expect((await (await call("GET", "/api/voices")).json()).voices).toEqual(["af_heart", "am_michael"]);
    });

    test("static files carry a strict CSP", async () => {
        const response = await call("GET", "/");
        expect(response.status).toBe(200);
        expect(response.headers.get("content-security-policy")).toContain("script-src 'self'");
        expect((await call("GET", "/../server.js")).status).toBe(404);
    });
});
