// Article rendering and sentence wrapping, ported from the desktop reader
// (src/reader.js) so both apps split an article into the same sentences and
// paragraphs: innermost blocks only, code and tables skipped, footnotes
// left unread.

const SKIP_TAGS = new Set(["PRE", "CODE", "SCRIPT", "STYLE", "SVG", "MATH", "TABLE", "TEXTAREA", "NOSCRIPT", "KBD", "SAMP"]);
const BLOCKS = "p,h1,h2,h3,h4,h5,h6,li,blockquote,figcaption,dd,dt,div";
const SKIP_INSIDE = "pre,code,script,style,svg,math,table,.sift-footnotes,[data-footnotes],.footnotes,#a-footnotes,section[role=doc-endnotes]";

function segmenterFor(language) {
    try {
        return new Intl.Segmenter(language, { granularity: "sentence" });
    } catch {
        return null;
    }
}

function split(segmenter, text) {
    const result = [];
    if (segmenter) {
        for (const part of segmenter.segment(text)) result.push({ start: part.index, length: part.segment.length });
        return result;
    }
    const expression = /[^.!?…]+(?:[.!?…]+["')\]]*|$)\s*/g;
    let match;
    while ((match = expression.exec(text)) !== null) {
        if (!match[0].length) {
            expression.lastIndex++;
            continue;
        }
        result.push({ start: match.index, length: match[0].length });
    }
    return result;
}

function collect(block) {
    const walker = document.createTreeWalker(block, NodeFilter.SHOW_TEXT, {
        acceptNode(node) {
            const parent = node.parentElement;
            if (!parent || SKIP_TAGS.has(parent.tagName)) return NodeFilter.FILTER_REJECT;
            if (parent.closest(SKIP_INSIDE)) return NodeFilter.FILTER_REJECT;
            if (!node.nodeValue || !node.nodeValue.trim()) return NodeFilter.FILTER_REJECT;
            return NodeFilter.FILTER_ACCEPT;
        },
    });
    const nodes = [];
    let text = "";
    while (walker.nextNode()) {
        nodes.push({ node: walker.currentNode, start: text.length });
        text += walker.currentNode.nodeValue;
    }
    return { nodes, text };
}

function locate(nodes, offset) {
    for (const entry of nodes) {
        if (offset <= entry.start + entry.node.nodeValue.length)
            return { node: entry.node, offset: Math.max(0, offset - entry.start) };
    }
    const last = nodes[nodes.length - 1];
    return last ? { node: last.node, offset: last.node.nodeValue.length } : null;
}

function wrapSentences(content, language) {
    let blocks = [];
    for (const candidate of content.querySelectorAll(BLOCKS)) {
        // Innermost blocks only, so a quote or list item that wraps
        // paragraphs is not read twice.
        if (!candidate.querySelector(BLOCKS)) blocks.push(candidate);
    }
    if (!blocks.length) blocks = [content];

    const segmenter = segmenterFor(language);
    const found = [];
    blocks.forEach((block, b) => {
        const data = collect(block);
        if (!data.text.trim()) return;
        const parts = split(segmenter, data.text);
        // Backwards, so splitting a text node cannot shift an earlier range.
        for (let p = parts.length - 1; p >= 0; p--) {
            const start = parts[p].start;
            const end = start + parts[p].length;
            const text = data.text.slice(start, end).trim();
            if (!text) continue;
            const from = locate(data.nodes, start);
            const to = locate(data.nodes, end);
            if (!from || !to) continue;
            try {
                const range = document.createRange();
                range.setStart(from.node, from.offset);
                range.setEnd(to.node, to.offset);
                const span = document.createElement("span");
                span.className = "sentence";
                span.appendChild(range.extractContents());
                range.insertNode(span);
                found.push({ element: span, text, block: b, start });
            } catch {
                // A malformed inline range must not stop the whole article.
            }
        }
    });
    found.sort((a, b) => (a.block === b.block ? a.start - b.start : a.block - b.block));
    found.forEach((sentence, i) => (sentence.element.dataset.index = String(i)));
    return found.map(({ element, text, block }) => ({ element, text, block }));
}

function wrapTables(root) {
    for (const table of root.querySelectorAll("table")) {
        if (table.parentElement?.classList.contains("table")) continue;
        const holder = document.createElement("div");
        holder.className = "table";
        table.replaceWith(holder);
        holder.appendChild(table);
    }
}

// Parse into an inert template first: nothing loads until the article is in
// the page, and images load lazily so a long illustrated article paints its
// first screen without waiting on every picture below it.
export function renderArticle(content, article) {
    const template = document.createElement("template");
    template.innerHTML = article.html || "";
    const fragment = template.content;
    for (const image of fragment.querySelectorAll("img")) {
        image.loading = "lazy";
        image.decoding = "async";
        image.referrerPolicy = "no-referrer";
        // A hotlinked image that fails leaves no broken-image box behind.
        image.addEventListener("error", () => image.remove(), { once: true });
    }
    const first = fragment.firstElementChild;
    const title = (article.title || "").trim().toLocaleLowerCase();
    if (first && /^H[1-2]$/.test(first.tagName) && first.textContent.trim().toLocaleLowerCase() === title) first.remove();
    // Defuddle wraps everything in one <article>; unwrap it so the first
    // paragraph sits flush under the header.
    if (fragment.children.length === 1 && fragment.firstElementChild.tagName === "ARTICLE")
        fragment.firstElementChild.replaceWith(...fragment.firstElementChild.childNodes);
    wrapTables(fragment);
    content.replaceChildren(fragment);
    return wrapSentences(content, article.language || "en");
}
