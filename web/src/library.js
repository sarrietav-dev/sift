// The web app's library is a sift database with the desktop schema, so the
// file stays a valid `sift sync` snapshot in both directions. HTML snapshots
// live beside it under articles/<id>/content.html, like on the desktop.
import { Database } from "bun:sqlite";
import { existsSync, mkdirSync, readFileSync, renameSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";

const SCHEMA = `
CREATE TABLE IF NOT EXISTS articles (
    id TEXT PRIMARY KEY, url TEXT NOT NULL, canonical_url TEXT, title TEXT, author TEXT,
    published TEXT, domain TEXT, language TEXT, word_count INTEGER DEFAULT 0, excerpt TEXT,
    content_text TEXT, content_path TEXT, markdown_path TEXT, clipped_at TEXT, updated_at TEXT,
    read_state INTEGER DEFAULT 0, last_sentence INTEGER DEFAULT 0, last_played_at TEXT,
    content_hash TEXT, file_hash TEXT);
CREATE INDEX IF NOT EXISTS articles_canonical ON articles(canonical_url);
CREATE INDEX IF NOT EXISTS articles_url ON articles(url);
CREATE INDEX IF NOT EXISTS articles_clipped ON articles(clipped_at);
CREATE TABLE IF NOT EXISTS tags (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT UNIQUE NOT NULL COLLATE NOCASE);
CREATE TABLE IF NOT EXISTS article_tags (article_id TEXT NOT NULL, tag_id INTEGER NOT NULL, PRIMARY KEY (article_id, tag_id));
CREATE TABLE IF NOT EXISTS playlists (id TEXT PRIMARY KEY, name TEXT NOT NULL, created_at TEXT, updated_at TEXT);
CREATE TABLE IF NOT EXISTS playlist_items (playlist_id TEXT NOT NULL, article_id TEXT NOT NULL, position INTEGER NOT NULL, PRIMARY KEY (playlist_id, article_id));
CREATE INDEX IF NOT EXISTS playlist_position ON playlist_items(playlist_id, position);
CREATE TABLE IF NOT EXISTS queue (position INTEGER PRIMARY KEY, article_id TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS settings (key TEXT PRIMARY KEY, value TEXT);
`;

const SUMMARY = `id, title, author, domain, url, published, language, word_count AS wordCount,
    read_state AS readState, last_sentence AS lastSentence, clipped_at AS clippedAt`;

const now = () => new Date().toISOString().replace(/\.\d+Z$/, "Z");

export class Library {
    constructor(directory) {
        this.directory = directory;
        mkdirSync(directory, { recursive: true });
        this.db = new Database(join(directory, "sift.db"), { create: true });
        this.db.exec("PRAGMA journal_mode=WAL; PRAGMA busy_timeout=5000;");
        this.db.exec(SCHEMA);
        try {
            this.db.exec("CREATE VIRTUAL TABLE IF NOT EXISTS articles_fts USING fts5(article_id UNINDEXED, title, author, excerpt, content_text)");
            this.fts = true;
        } catch {
            this.fts = false;
        }
        this.db.exec("PRAGMA user_version=1");
    }

    close() {
        this.db.close();
    }

    htmlPath(id) {
        return join(this.directory, "articles", id, "content.html");
    }

    setting(key, fallback = "") {
        return this.db.query("SELECT value FROM settings WHERE key = ?").get(key)?.value ?? fallback;
    }

    setSetting(key, value) {
        this.db.query("INSERT INTO settings(key, value) VALUES (?, ?) ON CONFLICT(key) DO UPDATE SET value = excluded.value")
            .run(key, String(value));
    }

    queue() {
        return this.db.query(`SELECT ${SUMMARY} FROM queue q JOIN articles a ON a.id = q.article_id ORDER BY q.position`).all();
    }

    articles() {
        return this.db.query(`SELECT ${SUMMARY} FROM articles ORDER BY clipped_at DESC`).all();
    }

    article(id) {
        return this.db.query(`SELECT ${SUMMARY}, content_text AS text FROM articles WHERE id = ?`).get(id);
    }

    html(id) {
        const path = this.htmlPath(id);
        return existsSync(path) ? readFileSync(path, "utf8") : null;
    }

    missingHtml() {
        const rows = this.db.query(`SELECT a.id, a.url FROM articles a LEFT JOIN queue q ON q.article_id = a.id
            ORDER BY q.position IS NULL, q.position, a.clipped_at DESC`).all();
        return rows.filter(row => !existsSync(this.htmlPath(row.id)));
    }

    writeHtml(id, html) {
        const path = this.htmlPath(id);
        mkdirSync(dirname(path), { recursive: true });
        writeFileSync(path + ".tmp", html);
        renameSync(path + ".tmp", path);
        this.db.query("UPDATE articles SET content_path = ? WHERE id = ?").run(path, id);
    }

    findByUrl(...urls) {
        for (const url of urls.filter(Boolean)) {
            const row = this.db.query("SELECT id FROM articles WHERE canonical_url = ? OR url = ? ORDER BY updated_at DESC LIMIT 1").get(url, url);
            if (row) return row.id;
        }
        return null;
    }

    // A re-clip updates the existing row, so reading progress survives.
    save(page, requestedUrl) {
        const existing = this.findByUrl(page.canonicalUrl, page.url, requestedUrl);
        const id = existing || crypto.randomUUID();
        const stamp = now();
        const values = [page.url, page.canonicalUrl, page.title, page.author, page.published, page.domain,
            page.language, page.wordCount, page.excerpt, page.text, this.htmlPath(id), stamp];
        this.db.transaction(() => {
            if (existing) {
                this.db.query(`UPDATE articles SET url=?, canonical_url=?, title=?, author=?, published=?, domain=?,
                    language=?, word_count=?, excerpt=?, content_text=?, content_path=?, updated_at=? WHERE id=?`).run(...values, id);
            } else {
                this.db.query(`INSERT INTO articles (url, canonical_url, title, author, published, domain, language,
                    word_count, excerpt, content_text, content_path, updated_at, id, clipped_at)
                    VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)`).run(...values, id, stamp);
            }
            if (this.fts) {
                this.db.query("DELETE FROM articles_fts WHERE article_id = ?").run(id);
                this.db.query("INSERT INTO articles_fts(article_id, title, author, excerpt, content_text) VALUES (?, ?, ?, ?, ?)")
                    .run(id, page.title, page.author, page.excerpt, page.text);
            }
        })();
        this.writeHtml(id, page.html);
        return id;
    }

    // The queue table keys on position, so every change rewrites it in one
    // transaction and stamps the time `sift sync merge` compares.
    setQueue(ids) {
        const known = new Set(this.db.query("SELECT id FROM articles").all().map(row => row.id));
        const unique = [...new Set(ids)].filter(id => known.has(id));
        this.db.transaction(() => {
            this.db.exec("DELETE FROM queue");
            const insert = this.db.query("INSERT INTO queue(position, article_id) VALUES (?, ?)");
            unique.forEach((id, position) => insert.run(position, id));
            this.setSetting("queue_updated_at", now());
        })();
        return this.queue();
    }

    enqueue(id) {
        const ids = this.queue().map(item => item.id);
        if (!ids.includes(id)) ids.push(id);
        return this.setQueue(ids);
    }

    dequeue(id) {
        return this.setQueue(this.queue().map(item => item.id).filter(other => other !== id));
    }

    setProgress(id, sentence, readState) {
        const row = this.db.query("SELECT read_state FROM articles WHERE id = ?").get(id);
        if (!row) return false;
        const state = Math.max(0, Math.min(2, readState ?? row.read_state));
        this.db.query("UPDATE articles SET last_sentence = ?, read_state = ?, last_played_at = ? WHERE id = ?")
            .run(Math.max(0, sentence | 0), state, now(), id);
        return true;
    }
}
