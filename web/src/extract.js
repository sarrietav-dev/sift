// Fetches a page and runs Defuddle on it, the same extractor the desktop app
// injects into Qt WebEngine. Without a browser, pages that only render through
// JavaScript come back thin, but most articles ship their text in the HTML.
import { lookup } from "node:dns/promises";
import { isIP } from "node:net";
import { parseHTML } from "linkedom";
import { Defuddle } from "defuddle/node";
import { plainText, sanitize } from "./sanitize.js";

const USER_AGENT = "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0 Safari/537.36";
const MAX_BYTES = 8 * 1024 * 1024;
const MAX_REDIRECTS = 5;

export class ExtractError extends Error {}

function privateV4(address) {
    const [a, b] = address.split(".").map(Number);
    return a === 0 || a === 10 || a === 127 || (a === 100 && b >= 64 && b <= 127)
        || (a === 169 && b === 254) || (a === 172 && b >= 16 && b <= 31)
        || (a === 192 && b === 168) || a >= 224;
}

export function isPrivateAddress(address) {
    if (isIP(address) === 4) return privateV4(address);
    const value = address.toLowerCase();
    const mapped = value.match(/^::ffff:(\d+\.\d+\.\d+\.\d+)$/);
    if (mapped) return privateV4(mapped[1]);
    return value === "::" || value === "::1" || /^f[cd]/.test(value) || /^fe[89ab]/.test(value);
}

// The server sits next to Kokoro and the tailnet, so a clip must never reach
// an internal address, including through a redirect.
async function assertPublic(url) {
    if (!["http:", "https:"].includes(url.protocol)) throw new ExtractError("Only http and https pages can be added.");
    const host = url.hostname.replace(/^\[|\]$/g, "");
    const addresses = isIP(host) ? [{ address: host }] : await lookup(host, { all: true }).catch(() => []);
    if (!addresses.length) throw new ExtractError(`Could not find ${url.hostname}.`);
    if (addresses.some(entry => isPrivateAddress(entry.address)))
        throw new ExtractError("That address is not reachable from sift.");
}

async function readLimited(response) {
    const reader = response.body.getReader();
    const chunks = [];
    let size = 0;
    for (;;) {
        const { done, value } = await reader.read();
        if (done) break;
        size += value.length;
        if (size > MAX_BYTES) {
            reader.cancel();
            throw new ExtractError("The page is too large.");
        }
        chunks.push(value);
    }
    const type = response.headers.get("content-type") || "";
    const charset = type.match(/charset=([\w-]+)/i)?.[1] || "utf-8";
    let decoder;
    try {
        decoder = new TextDecoder(charset);
    } catch {
        decoder = new TextDecoder("utf-8");
    }
    return decoder.decode(Buffer.concat(chunks));
}

export async function fetchPage(address, fetcher = fetch) {
    let url;
    try {
        url = new URL(address);
    } catch {
        throw new ExtractError("That is not a valid URL.");
    }
    for (let hop = 0; hop <= MAX_REDIRECTS; hop++) {
        await assertPublic(url);
        let response;
        try {
            response = await fetcher(url.href, {
                redirect: "manual",
                signal: AbortSignal.timeout(20000),
                headers: { "user-agent": USER_AGENT, accept: "text/html,application/xhtml+xml;q=0.9,*/*;q=0.5", "accept-language": "en,*;q=0.5" },
            });
        } catch {
            throw new ExtractError(`Could not load ${url.hostname}.`);
        }
        if (response.status >= 300 && response.status < 400 && response.headers.get("location")) {
            url = new URL(response.headers.get("location"), url);
            continue;
        }
        if (!response.ok) throw new ExtractError(`${url.hostname} answered ${response.status}.`);
        const type = response.headers.get("content-type") || "";
        if (type && !/html|xml|text\/plain/i.test(type)) throw new ExtractError("That link is not a web page.");
        return { url: url.href, html: await readLimited(response) };
    }
    throw new ExtractError("Too many redirects.");
}

function canonicalOf(document, url) {
    const href = document.querySelector('link[rel="canonical"]')?.getAttribute("href");
    if (!href) return url;
    try {
        const canonical = new URL(href, url);
        return ["http:", "https:"].includes(canonical.protocol) ? canonical.href : url;
    } catch {
        return url;
    }
}

export async function extractHtml(html, url) {
    const { document } = parseHTML(html);
    const canonical = canonicalOf(document, url);
    const result = await Defuddle(document, url, {});
    const content = sanitize(result.content, url);
    const text = plainText(content);
    if (!text) throw new ExtractError("No readable article on that page.");
    let domain = result.domain;
    if (!domain) {
        try {
            domain = new URL(url).hostname.replace(/^www\./, "");
        } catch {}
    }
    return {
        url,
        canonicalUrl: canonical,
        title: (result.title || "").trim() || domain || "Untitled",
        author: (result.author || "").trim(),
        published: (result.published || "").trim(),
        domain: domain || "",
        language: (result.language || "en").split(/[_\s]/)[0],
        wordCount: result.wordCount || text.split(/\s+/).length,
        excerpt: (result.description || "").trim(),
        html: content,
        text,
    };
}

export async function extract(address, fetcher = fetch) {
    const page = await fetchPage(address, fetcher);
    return extractHtml(page.html, page.url);
}
