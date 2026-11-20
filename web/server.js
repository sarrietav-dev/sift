// sift on the web: the reading queue and Kokoro voice of the desktop app,
// served to a phone over the tailnet. One Bun process owns the library,
// extracts pages with Defuddle, and proxies speech to a private Kokoro.
import { existsSync, readdirSync } from "node:fs";
import { join } from "node:path";
import { ExtractError, extract } from "./src/extract.js";
import { Library } from "./src/library.js";
import { textToHtml } from "./src/sanitize.js";
import { Speech, SpeechError } from "./src/speech.js";

const ROOT = import.meta.dir;
const PUBLIC = join(ROOT, "public");
const FONTS = join(ROOT, "node_modules/@fontsource-variable/atkinson-hyperlegible-next/files");

const CSP = "default-src 'self'; img-src 'self' https: http: data:; media-src 'self' blob:; style-src 'self'; "
    + "script-src 'self'; font-src 'self'; connect-src 'self'; manifest-src 'self'; object-src 'none'; "
    + "base-uri 'none'; frame-ancestors 'none'; form-action 'self'";

const TYPES = {
    ".html": "text/html; charset=utf-8", ".js": "text/javascript; charset=utf-8", ".css": "text/css; charset=utf-8",
    ".svg": "image/svg+xml", ".webmanifest": "application/manifest+json", ".woff2": "font/woff2",
};

function headers(type, extra = {}) {
    return {
        "content-type": type,
        "x-content-type-options": "nosniff",
        "referrer-policy": "no-referrer",
        "content-security-policy": CSP,
        "cache-control": "no-store",
        ...extra,
    };
}

const json = (body, status = 200) => new Response(JSON.stringify(body), { status, headers: headers("application/json") });
const failure = (message, status) => json({ error: message }, status);

function staticFiles() {
    const files = new Map();
    for (const name of readdirSync(PUBLIC)) {
        const extension = name.slice(name.lastIndexOf("."));
        if (TYPES[extension]) files.set("/" + name, { path: join(PUBLIC, name), type: TYPES[extension] });
    }
    files.set("/", files.get("/index.html"));
    if (existsSync(FONTS)) {
        for (const name of readdirSync(FONTS).filter(name => name.endsWith(".woff2")))
            files.set("/fonts/" + name, { path: join(FONTS, name), type: TYPES[".woff2"], immutable: true });
    }
    return files;
}

async function body(request) {
    try {
        return await request.json();
    } catch {
        return {};
    }
}

export function createApp({ dataDirectory, kokoroUrl, hosts, fetcher = fetch }) {
    const library = new Library(dataDirectory);
    const speech = new Speech({ endpoint: kokoroUrl, cacheDirectory: join(dataDirectory, "cache", "audio"), fetcher });
    const files = staticFiles();
    const allowedHosts = new Set(hosts);
    const refreshing = new Map();

    // Articles synced from another machine carry text but no HTML snapshot.
    // Fetching the page again restores images and formatting; one refresh per
    // article runs at a time, shared by the warm-up loop and open requests.
    function refresh(id) {
        if (refreshing.has(id)) return refreshing.get(id);
        const row = library.article(id);
        const job = extract(row.url, fetcher)
            .then(page => library.writeHtml(id, page.html))
            .finally(() => refreshing.delete(id));
        refreshing.set(id, job);
        return job;
    }

    async function warm() {
        for (const { id } of library.missingHtml()) {
            try {
                await refresh(id);
            } catch {}
        }
    }

    async function articleBody(id) {
        const article = library.article(id);
        if (!article) return null;
        let html = library.html(id);
        let source = "snapshot";
        if (html === null) {
            try {
                await Promise.race([refresh(id), Bun.sleep(15000).then(() => { throw new Error("slow"); })]);
                html = library.html(id);
            } catch {}
        }
        if (html === null) {
            html = textToHtml(article.text);
            source = "text";
        }
        const { text, ...meta } = article;
        return { ...meta, html, source };
    }

    function state() {
        return {
            queue: library.queue(),
            articles: library.articles(),
            settings: {
                voice: library.setting("voice", "af_heart"),
                speed: Number(library.setting("speed", "1")) || 1,
                lastArticle: library.setting("last_article"),
            },
        };
    }

    async function api(request, url) {
        const path = url.pathname;
        const method = request.method;
        let match;

        if (path === "/api/state" && method === "GET") return json(state());

        if (path === "/api/voices" && method === "GET") {
            try {
                return json({ voices: await speech.voices() });
            } catch {
                return failure("Kokoro unavailable", 502);
            }
        }

        if (path === "/api/speech" && method === "GET") {
            const speed = Number(url.searchParams.get("speed"));
            try {
                const file = await speech.audio(url.searchParams.get("text"), url.searchParams.get("voice") || "", speed);
                return new Response(Bun.file(file), { headers: headers("audio/mpeg", { "cache-control": "private, max-age=31536000, immutable" }) });
            } catch (error) {
                return failure(error.message, error instanceof SpeechError && error.message.startsWith("Invalid") ? 400 : 502);
            }
        }

        if ((match = path.match(/^\/api\/articles\/([\w-]+)$/)) && method === "GET") {
            const article = await articleBody(match[1]);
            if (!article) return failure("No such article", 404);
            library.setSetting("last_article", article.id);
            return json(article);
        }

        if (path === "/api/articles" && method === "POST") {
            const { url: address } = await body(request);
            if (typeof address !== "string" || !address.trim()) return failure("Paste a link to add.", 400);
            try {
                const page = await extract(address.trim(), fetcher);
                const id = library.save(page, address.trim());
                return json({ id, queue: library.enqueue(id), articles: library.articles() });
            } catch (error) {
                return failure(error instanceof ExtractError ? error.message : "Could not add that page.", 422);
            }
        }

        if ((match = path.match(/^\/api\/articles\/([\w-]+)\/progress$/)) && method === "PUT") {
            const { sentence, readState } = await body(request);
            if (!Number.isInteger(sentence)) return failure("Invalid progress", 400);
            if (!library.setProgress(match[1], sentence, Number.isInteger(readState) ? readState : undefined))
                return failure("No such article", 404);
            return json({ ok: true });
        }

        if (path === "/api/queue" && method === "PUT") {
            const { ids } = await body(request);
            if (!Array.isArray(ids) || ids.some(id => typeof id !== "string")) return failure("Invalid queue", 400);
            return json({ queue: library.setQueue(ids) });
        }

        if ((match = path.match(/^\/api\/queue\/([\w-]+)$/))) {
            if (method === "POST") return json({ queue: library.enqueue(match[1]) });
            if (method === "DELETE") return json({ queue: library.dequeue(match[1]) });
        }

        if (path === "/api/settings" && method === "PUT") {
            const { voice, speed } = await body(request);
            if (typeof voice === "string" && /^[a-z]{2}_[a-z0-9_]+$/.test(voice)) library.setSetting("voice", voice);
            if (typeof speed === "number" && speed >= 0.5 && speed <= 2) library.setSetting("speed", speed.toFixed(2));
            return json(state().settings);
        }

        return failure("Not found", 404);
    }

    async function handle(request) {
        const url = new URL(request.url);
        const host = (request.headers.get("host") || "").replace(/:\d+$/, "");
        if (!allowedHosts.has(host)) return new Response("Invalid host", { status: 400 });
        if (request.method !== "GET" && request.method !== "HEAD") {
            const origin = request.headers.get("origin");
            let originHost = "";
            try { originHost = new URL(origin).host; } catch {}
            if (!origin || originHost !== request.headers.get("host")) return new Response("Forbidden", { status: 403 });
        }
        if (url.pathname.startsWith("/api/")) return api(request, url);
        const file = files.get(url.pathname);
        if (!file || (request.method !== "GET" && request.method !== "HEAD")) return new Response("Not found", { status: 404 });
        const cache = file.immutable ? { "cache-control": "public, max-age=31536000, immutable" } : { "cache-control": "no-cache" };
        return new Response(Bun.file(file.path), { headers: headers(file.type, cache) });
    }

    return { handle, library, speech, warm };
}

if (import.meta.main) {
    const app = createApp({
        dataDirectory: process.env.SIFT_DATA || "/data",
        kokoroUrl: process.env.KOKORO_URL || "http://kokoro:8880",
        hosts: (process.env.SIFT_HOSTS || "localhost,127.0.0.1").split(",").map(host => host.trim()),
    });
    const server = Bun.serve({ port: Number(process.env.PORT || 8080), fetch: app.handle, idleTimeout: 120 });
    console.log(`sift web on :${server.port}`);
    app.warm();
}
