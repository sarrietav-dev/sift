(function () {
    'use strict';

    var content = document.getElementById('sift-content');
    if (!content) return '[]';
    if (window.__siftWrapped) return JSON.stringify(window.__siftSentences || []);

    var skipTags = new Set(['PRE', 'CODE', 'SCRIPT', 'STYLE', 'SVG', 'MATH', 'TABLE', 'TEXTAREA', 'NOSCRIPT', 'KBD', 'SAMP']);
    var blockSelector = 'p,h1,h2,h3,h4,h5,h6,li,blockquote,figcaption,dd,dt,div';
    var blocks = [];
    var candidates = content.querySelectorAll(blockSelector);
    for (var c = 0; c < candidates.length; c++) {
        var element = candidates[c];
        // Process the innermost blocks, so a quote or list item that wraps
        // paragraphs does not get read twice.
        if (element.querySelector(blockSelector)) continue;
        blocks.push(element);
    }
    if (blocks.length === 0) blocks = [content];

    var language = document.documentElement.lang || navigator.language || 'en';
    var segmenter = null;
    try {
        segmenter = new Intl.Segmenter(language, { granularity: 'sentence' });
    } catch (error) {
        segmenter = null;
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

    function split(text) {
        if (!segmenter) return fallbackSegments(text);
        var result = [];
        var iterator = segmenter.segment(text);
        for (var part of iterator) {
            result.push({ start: part.index, length: part.segment.length });
        }
        return result;
    }

    function locate(nodes, offset) {
        for (var i = 0; i < nodes.length; i++) {
            var entry = nodes[i];
            var length = entry.node.nodeValue.length;
            if (offset <= entry.start + length) {
                return { node: entry.node, offset: Math.max(0, offset - entry.start) };
            }
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
                if (parent.closest('pre,code,script,style,svg,math,table,.sift-footnotes,[data-footnotes],.footnotes')) {
                    return NodeFilter.FILTER_REJECT;
                }
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

    var found = [];
    for (var b = 0; b < blocks.length; b++) {
        var data = collect(blocks[b]);
        if (!data.text.trim()) continue;
        var parts = split(data.text);
        // Work backwards so splitting a text node cannot invalidate an
        // earlier range in the same block.
        for (var p = parts.length - 1; p >= 0; p--) {
            var part = parts[p];
            var start = part.start;
            var end = part.start + part.length;
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

    var sentences = [];
    for (var i = 0; i < found.length; i++) {
        var item = found[i];
        item.element.setAttribute('data-sift', String(i));
        sentences.push({ i: i, text: item.text });
    }
    window.__siftSentences = sentences;
    window.__siftWrapped = true;

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
        var spans = document.querySelectorAll('.sift-sentence.sift-current');
        for (var i = 0; i < spans.length; i++) spans[i].classList.remove('sift-current');
        var current = document.querySelector('.sift-sentence[data-sift="' + index + '"]');
        if (current) current.classList.add('sift-current');
        return true;
    };

    window.siftScrollToCurrent = function (index, center) {
        var current = document.querySelector('.sift-sentence[data-sift="' + index + '"]');
        if (current) {
            current.scrollIntoView({ block: center ? 'center' : 'nearest', behavior: 'smooth' });
        }
        return true;
    };

    window.siftApplyTheme = function (theme) {
        var root = document.documentElement.style;
        if (theme.background) root.setProperty('--sift-bg', theme.background);
        if (theme.foreground) root.setProperty('--sift-fg', theme.foreground);
        if (theme.muted) root.setProperty('--sift-muted', theme.muted);
        if (theme.accent) root.setProperty('--sift-accent', theme.accent);
        if (theme.selection) root.setProperty('--sift-selection', theme.selection);
        if (theme.panel) root.setProperty('--sift-panel', theme.panel);
        if (theme.border) root.setProperty('--sift-border', theme.border);
        if (theme.radius !== undefined) root.setProperty('--sift-radius', theme.radius + 'px');
        return true;
    };

    window.siftSetFontSize = function (size) {
        document.documentElement.style.setProperty('--sift-font-size', size + 'px');
        return true;
    };

    window.siftWrapSentences = function () {
        return JSON.stringify(window.__siftSentences || []);
    };

    return JSON.stringify(sentences);
})();

