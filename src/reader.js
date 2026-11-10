// The reader page is loaded once. Each article is rendered into it with
// siftRender(), which swaps the content in place, so opening another article
// never reloads WebEngine, never flashes, and costs only the DOM work.
(function () {
    'use strict';

    var following = true;
    var skipTags = new Set(['PRE', 'CODE', 'SCRIPT', 'STYLE', 'SVG', 'MATH', 'TABLE', 'TEXTAREA', 'NOSCRIPT', 'KBD', 'SAMP']);
    var blockSelector = 'p,h1,h2,h3,h4,h5,h6,li,blockquote,figcaption,dd,dt,div';
    var sentences = [];
    var spans = [];
    var currentSpan = null;

    function stopFollowing() {
        if (!following) return;
        following = false;
        window.location.href = 'https://sift.invalid/follow/paused';
    }
    window.addEventListener('wheel', stopFollowing, { passive: true });
    window.addEventListener('touchmove', stopFollowing, { passive: true });

    function segmenterFor(language) {
        try {
            return new Intl.Segmenter(language, { granularity: 'sentence' });
        } catch (error) {
            return null;
        }
    }

    function fallbackSegments(text) {
        var result = [];
        var expression = /[^.!?…]+(?:[.!?…]+["')\]]*|$)\s*/g;
        var match;
        while ((match = expression.exec(text)) !== null) {
            if (!match[0].length) {
                expression.lastIndex++;
                continue;
            }
            result.push({ start: match.index, length: match[0].length });
        }
        return result;
    }

    function split(segmenter, text) {
        if (!segmenter) return fallbackSegments(text);
        var result = [];
        for (var part of segmenter.segment(text))
            result.push({ start: part.index, length: part.segment.length });
        return result;
    }

    function locate(nodes, offset) {
        for (var i = 0; i < nodes.length; i++) {
            var entry = nodes[i];
            var length = entry.node.nodeValue.length;
            if (offset <= entry.start + length)
                return { node: entry.node, offset: Math.max(0, offset - entry.start) };
        }
        var last = nodes[nodes.length - 1];
        return last ? { node: last.node, offset: last.node.nodeValue.length } : null;
    }

    function collect(block) {
        var walker = document.createTreeWalker(block, NodeFilter.SHOW_TEXT, {
            acceptNode: function (node) {
                var parent = node.parentElement;
                if (!parent) return NodeFilter.FILTER_REJECT;
                if (skipTags.has(parent.tagName)) return NodeFilter.FILTER_REJECT;
                if (parent.closest('pre,code,script,style,svg,math,table,.sift-footnotes,[data-footnotes],.footnotes,#footnotes'))
                    return NodeFilter.FILTER_REJECT;
                if (!node.nodeValue || !node.nodeValue.trim()) return NodeFilter.FILTER_REJECT;
                return NodeFilter.FILTER_ACCEPT;
            }
        });
        var nodes = [];
        var text = '';
        while (walker.nextNode()) {
            var node = walker.currentNode;
            nodes.push({ node: node, start: text.length });
            text += node.nodeValue;
        }
        return { nodes: nodes, text: text };
    }

    function wrapSentences(content, language) {
        var blocks = [];
        var candidates = content.querySelectorAll(blockSelector);
        for (var c = 0; c < candidates.length; c++) {
            // Process the innermost blocks, so a quote or list item that wraps
            // paragraphs does not get read twice.
            if (candidates[c].querySelector(blockSelector)) continue;
            blocks.push(candidates[c]);
        }
        if (blocks.length === 0) blocks = [content];

        var segmenter = segmenterFor(language);
        var found = [];
        for (var b = 0; b < blocks.length; b++) {
            var data = collect(blocks[b]);
            if (!data.text.trim()) continue;
            var parts = split(segmenter, data.text);
            // Work backwards so splitting a text node cannot invalidate an
            // earlier range in the same block.
            for (var p = parts.length - 1; p >= 0; p--) {
                var start = parts[p].start;
                var end = start + parts[p].length;
                var sentenceText = data.text.slice(start, end).trim();
                if (!sentenceText) continue;
                var startPos = locate(data.nodes, start);
                var endPos = locate(data.nodes, end);
                if (!startPos || !endPos) continue;
                var range = document.createRange();
                try {
                    range.setStart(startPos.node, startPos.offset);
                    range.setEnd(endPos.node, endPos.offset);
                    var span = document.createElement('span');
                    span.className = 'sift-sentence';
                    span.appendChild(range.extractContents());
                    range.insertNode(span);
                    found.push({ element: span, text: sentenceText, block: b, start: start });
                } catch (error) {
                    // A malformed inline range should never stop the whole page.
                }
            }
        }

        found.sort(function (a, b) {
            return a.block === b.block ? a.start - b.start : a.block - b.block;
        });
        var result = [];
        spans = [];
        for (var i = 0; i < found.length; i++) {
            found[i].element.setAttribute('data-sift', String(i));
            spans.push(found[i].element);
            result.push({ i: i, text: found[i].text, block: found[i].block });
        }
        return result;
    }

    // Wide tables and code scroll inside their own box rather than the page.
    function wrapTables(content) {
        var tables = content.querySelectorAll('table');
        for (var i = 0; i < tables.length; i++) {
            var table = tables[i];
            if (table.parentElement && table.parentElement.classList.contains('sift-table')) continue;
            var holder = document.createElement('div');
            holder.className = 'sift-table';
            table.parentNode.insertBefore(holder, table);
            holder.appendChild(table);
        }
    }

    function renderMeta(parts) {
        var meta = document.getElementById('sift-meta');
        meta.textContent = '';
        for (var i = 0; i < (parts || []).length; i++) {
            if (i > 0) {
                var dot = document.createElement('span');
                dot.className = 'sift-dot';
                dot.textContent = '·';
                meta.appendChild(dot);
            }
            var item = document.createElement('span');
            item.className = 'sift-meta-' + (parts[i].kind || 'text');
            item.textContent = parts[i].text;
            meta.appendChild(item);
        }
    }

    window.siftRender = function (article) {
        var root = document.documentElement;
        root.lang = article.lang || 'en';
        root.dir = article.dir || 'ltr';
        var base = document.querySelector('base') || document.head.appendChild(document.createElement('base'));
        base.href = article.baseUrl || 'about:blank';

        document.getElementById('sift-title').textContent = article.title || '';
        renderMeta(article.meta);

        // Parse into an inert template first: nothing fetches until the
        // article is in the page, and then images load lazily, so a long
        // illustrated article paints its first screen without waiting on, or
        // decoding, every picture below it.
        var template = document.createElement('template');
        template.innerHTML = article.html || '';
        var fragment = template.content;
        var images = fragment.querySelectorAll('img');
        for (var m = 0; m < images.length; m++) {
            images[m].setAttribute('loading', 'lazy');
            images[m].setAttribute('decoding', 'async');
        }

        // Some extractors keep the title in the body as well as the metadata.
        var first = fragment.firstElementChild;
        if (first && /^H[1-2]$/.test(first.tagName)
            && first.textContent.trim().toLocaleLowerCase() === (article.title || '').trim().toLocaleLowerCase()) {
            first.remove();
        }
        wrapTables(fragment);

        var content = document.getElementById('sift-content');
        content.replaceChildren(fragment);

        following = true;
        currentSpan = null;
        window.scrollTo(0, 0);
        sentences = wrapSentences(content, root.lang);
        document.body.classList.add('sift-has-article');
        return JSON.stringify(sentences);
    };

    window.siftManualScroll = function (amount) {
        stopFollowing();
        window.scrollBy(0, amount);
    };

    window.siftResumeFollowing = function () {
        following = true;
        var current = document.querySelector('.sift-sentence.sift-current');
        if (current) current.scrollIntoView({ block: 'center', behavior: 'smooth' });
    };

    document.addEventListener('click', function (event) {
        var selection = window.getSelection();
        if (selection && String(selection).length > 0) return;

        var link = event.target.closest ? event.target.closest('a') : null;
        if (link && link.getAttribute('href')) {
            var href = link.getAttribute('href');
            if (href.charAt(0) === '#') return;
            var url;
            try {
                url = new URL(link.href, document.baseURI);
            } catch (error) {
                return;
            }
            if (url.protocol !== 'http:' && url.protocol !== 'https:') return;
            event.preventDefault();
            window.location.href = 'https://sift.invalid/link/' + encodeURIComponent(url.href)
                + (event.altKey ? '?new=1' : '');
            return;
        }

        var sentence = event.target.closest ? event.target.closest('.sift-sentence') : null;
        if (sentence) {
            event.preventDefault();
            window.location.href = 'https://sift.invalid/sentence/' + sentence.getAttribute('data-sift');
        }
    }, true);

    window.siftSetCurrent = function (index) {
        if (currentSpan) currentSpan.classList.remove('sift-current');
        currentSpan = spans[index] || null;
        if (currentSpan) currentSpan.classList.add('sift-current');
        return true;
    };

    window.siftScrollToCurrent = function (index, center) {
        var current = spans[index];
        if (current && following)
            current.scrollIntoView({ block: center ? 'center' : 'nearest', behavior: 'smooth' });
        return true;
    };

    window.siftApplyTheme = function (theme) {
        var root = document.documentElement;
        var style = root.style;
        if (theme.background) style.setProperty('--sift-bg', theme.background);
        if (theme.foreground) style.setProperty('--sift-fg', theme.foreground);
        if (theme.muted) style.setProperty('--sift-muted', theme.muted);
        if (theme.accent) style.setProperty('--sift-accent', theme.accent);
        if (theme.selection) style.setProperty('--sift-selection', theme.selection);
        if (theme.panel) style.setProperty('--sift-panel', theme.panel);
        if (theme.border) style.setProperty('--sift-border', theme.border);
        if (theme.radius !== undefined) style.setProperty('--sift-radius', theme.radius + 'px');
        if (theme.font) style.setProperty('--sift-body-font', '"' + theme.font + '", system-ui, sans-serif');
        if (theme.dark !== undefined) {
            root.classList.toggle('theme-dark', !!theme.dark);
            root.classList.toggle('theme-light', !theme.dark);
        }
        return true;
    };

    window.siftSetFontSize = function (size) {
        document.documentElement.style.setProperty('--sift-font-size', size + 'px');
        return true;
    };
})();
