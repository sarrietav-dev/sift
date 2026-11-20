// Extracted article HTML comes from arbitrary pages, so the server rebuilds it
// through an allowlist before anything reaches the browser. The page's CSP
// blocks inline script as well; this keeps markup that would still misbehave
// (forms, frames, styles, tracking attributes) out of the reader.
import { parseHTML } from "linkedom";

const DROP = new Set([
    "SCRIPT", "STYLE", "IFRAME", "OBJECT", "EMBED", "FORM", "INPUT", "BUTTON", "SELECT",
    "TEXTAREA", "NOSCRIPT", "TEMPLATE", "LINK", "META", "SVG", "MATH", "CANVAS", "AUDIO",
    "VIDEO", "FRAME", "FRAMESET", "BASE", "HEAD", "TITLE", "DIALOG",
]);

const KEEP = new Set([
    "P", "BR", "HR", "H1", "H2", "H3", "H4", "H5", "H6", "A", "STRONG", "B", "EM", "I", "U", "S",
    "DEL", "INS", "MARK", "SMALL", "SUB", "SUP", "CODE", "PRE", "KBD", "SAMP", "VAR", "BLOCKQUOTE",
    "Q", "CITE", "ABBR", "UL", "OL", "LI", "DL", "DT", "DD", "FIGURE", "FIGCAPTION", "IMG",
    "PICTURE", "SOURCE", "TABLE", "THEAD", "TBODY", "TFOOT", "TR", "TH", "TD", "CAPTION",
    "COLGROUP", "COL", "DIV", "SPAN", "SECTION", "ARTICLE", "ASIDE", "HEADER", "FOOTER",
    "DETAILS", "SUMMARY", "TIME",
]);

const ATTRIBUTES = {
    "*": ["lang", "dir", "title", "id"],
    A: ["href"],
    IMG: ["src", "srcset", "alt", "width", "height", "sizes"],
    SOURCE: ["srcset", "type", "media", "sizes"],
    TD: ["colspan", "rowspan"],
    TH: ["colspan", "rowspan", "scope"],
    OL: ["start", "reversed", "type"],
    TIME: ["datetime"],
    DETAILS: ["open"],
};

function absolute(value, base) {
    try {
        return new URL(value.trim(), base);
    } catch {
        return null;
    }
}

function cleanLink(value, base) {
    const trimmed = value.trim();
    if (trimmed.startsWith("#")) return "#a-" + trimmed.slice(1);
    const url = absolute(trimmed, base);
    if (!url || !["http:", "https:", "mailto:"].includes(url.protocol)) return null;
    return url.href;
}

function cleanImage(value, base) {
    const trimmed = value.trim();
    if (/^data:image\/(png|jpe?g|gif|webp|avif);/i.test(trimmed)) return trimmed;
    const url = absolute(trimmed, base);
    if (!url || !["http:", "https:"].includes(url.protocol)) return null;
    return url.href;
}

function cleanSrcset(value, base) {
    const parts = [];
    for (const candidate of value.split(/,\s+/)) {
        const [source, ...descriptor] = candidate.trim().split(/\s+/);
        const url = source && cleanImage(source, base);
        if (url && !url.startsWith("data:")) parts.push([url, ...descriptor].join(" "));
    }
    return parts.length ? parts.join(", ") : null;
}

function cleanElement(element, base) {
    const allowed = new Set([...ATTRIBUTES["*"], ...(ATTRIBUTES[element.tagName] || [])]);
    for (const { name, value } of [...element.attributes]) {
        if (!allowed.has(name)) {
            element.removeAttribute(name);
            continue;
        }
        let clean = value;
        if (name === "href") clean = cleanLink(value, base);
        else if (name === "src") clean = cleanImage(value, base);
        else if (name === "srcset") clean = cleanSrcset(value, base);
        // Ids are prefixed so an article can never shadow the app's own
        // elements; in-page links are rewritten to match.
        else if (name === "id") clean = "a-" + value;
        if (clean === null) element.removeAttribute(name);
        else if (clean !== value) element.setAttribute(name, clean);
    }
    if (element.tagName === "A" && element.getAttribute("href") && !element.getAttribute("href").startsWith("#")) {
        element.setAttribute("target", "_blank");
        element.setAttribute("rel", "noopener noreferrer");
    }
}

function walk(node, base) {
    for (const child of [...node.childNodes]) {
        if (child.nodeType === 8) {
            child.remove();
            continue;
        }
        if (child.nodeType !== 1) continue;
        if (DROP.has(child.tagName)) {
            child.remove();
            continue;
        }
        walk(child, base);
        if (!KEEP.has(child.tagName)) {
            child.replaceWith(...child.childNodes);
            continue;
        }
        cleanElement(child, base);
        if (child.tagName === "IMG" && !child.getAttribute("src") && !child.getAttribute("srcset")) child.remove();
    }
}

export function sanitize(html, base) {
    const { document } = parseHTML(`<!doctype html><html><body>${html || ""}</body></html>`);
    walk(document.body, base);
    return document.body.innerHTML.trim();
}

const TEXT_BLOCKS = "p,h1,h2,h3,h4,h5,h6,li,blockquote,figcaption,dt,dd,pre";

// Plain text for search and as the fallback when a page cannot be fetched
// again: one line per innermost block, the way the desktop app stores it.
export function plainText(html) {
    const { document } = parseHTML(`<!doctype html><html><body>${html || ""}</body></html>`);
    const lines = [];
    for (const block of document.body.querySelectorAll(TEXT_BLOCKS)) {
        if (block.querySelector(TEXT_BLOCKS)) continue;
        const text = block.textContent.replace(/\s+/g, " ").trim();
        if (text) lines.push(text);
    }
    if (!lines.length) {
        const text = document.body.textContent.replace(/\s+/g, " ").trim();
        if (text) lines.push(text);
    }
    return lines.join("\n\n");
}

const ESCAPES = { "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" };

// Articles synced from another machine arrive without their HTML snapshot.
// Until the page is fetched again, their stored text still reads as
// paragraphs instead of one wall of text.
export function textToHtml(text) {
    return (text || "")
        .split(/\n\s*\n|\n/)
        .map(line => line.trim())
        .filter(Boolean)
        .map(line => `<p>${line.replace(/[&<>"']/g, c => ESCAPES[c])}</p>`)
        .join("\n");
}
