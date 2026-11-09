#include "library.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QStringList>
#include <QUrl>
#include <QUuid>
#include <QVariant>

#include <algorithm>

namespace {
QString now() { return QDateTime::currentDateTimeUtc().toString(Qt::ISODate); }

QString cleanText(const QString &text)
{
    QString value = text;
    value.replace(QRegularExpression("\\s+"), " ");
    return value.trimmed();
}
}

Library::Library(const QString &dataDirectory, QObject *parent)
    : QObject(parent) {
    m_customDataDirectory = !dataDirectory.isEmpty();
    m_dataDirectory = dataDirectory.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        : dataDirectory;
    if (m_dataDirectory.isEmpty())
        m_dataDirectory = QDir::homePath() + "/.local/share/sift";
    m_cacheDirectory = m_customDataDirectory
        ? m_dataDirectory + "/cache"
        : QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (m_cacheDirectory.isEmpty())
        m_cacheDirectory = QDir::homePath() + "/.cache/sift";

    QDir().mkpath(m_dataDirectory);
    QDir().mkpath(m_dataDirectory + "/articles");
    QDir().mkpath(m_cacheDirectory);

    m_connectionName = QStringLiteral("sift_%1").arg(reinterpret_cast<quintptr>(this), 0, 16);
    m_db = QSqlDatabase::addDatabase("QSQLITE", m_connectionName);
    m_db.setDatabaseName(m_dataDirectory + "/sift.db");

    QString error;
    if (!openDatabase(&error))
        qWarning("sift: could not open library: %s", qPrintable(error));
    QDir().mkpath(saveDirectory());
}

Library::~Library() {
    if (m_db.isOpen())
        m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(m_connectionName);
}

bool Library::openDatabase(QString *error) {
    if (!m_db.open()) {
        if (error)
            *error = m_db.lastError().text();
        return false;
    }
    QSqlQuery pragma(m_db);
    pragma.exec("PRAGMA journal_mode=WAL");
    pragma.exec("PRAGMA foreign_keys=ON");
    migrate();
    return true;
}

void Library::migrate() {
    QSqlQuery query(m_db);
    query.exec(R"(
        CREATE TABLE IF NOT EXISTS articles (
            id TEXT PRIMARY KEY,
            url TEXT NOT NULL,
            canonical_url TEXT,
            title TEXT,
            author TEXT,
            published TEXT,
            domain TEXT,
            language TEXT,
            word_count INTEGER DEFAULT 0,
            excerpt TEXT,
            content_text TEXT,
            content_path TEXT,
            markdown_path TEXT,
            clipped_at TEXT,
            updated_at TEXT,
            read_state INTEGER DEFAULT 0,
            last_sentence INTEGER DEFAULT 0,
            last_played_at TEXT,
            content_hash TEXT,
            file_hash TEXT
        )
    )");
    query.exec("CREATE INDEX IF NOT EXISTS articles_canonical ON articles(canonical_url)");
    query.exec("CREATE INDEX IF NOT EXISTS articles_url ON articles(url)");
    query.exec("CREATE INDEX IF NOT EXISTS articles_clipped ON articles(clipped_at)");
    query.exec(R"(
        CREATE TABLE IF NOT EXISTS tags (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT UNIQUE NOT NULL COLLATE NOCASE
        )
    )");
    query.exec(R"(
        CREATE TABLE IF NOT EXISTS article_tags (
            article_id TEXT NOT NULL,
            tag_id INTEGER NOT NULL,
            PRIMARY KEY (article_id, tag_id)
        )
    )");
    query.exec(R"(
        CREATE TABLE IF NOT EXISTS playlists (
            id TEXT PRIMARY KEY,
            name TEXT NOT NULL,
            created_at TEXT,
            updated_at TEXT
        )
    )");
    query.exec(R"(
        CREATE TABLE IF NOT EXISTS playlist_items (
            playlist_id TEXT NOT NULL,
            article_id TEXT NOT NULL,
            position INTEGER NOT NULL,
            PRIMARY KEY (playlist_id, article_id)
        )
    )");
    query.exec("CREATE INDEX IF NOT EXISTS playlist_position ON playlist_items(playlist_id, position)");
    query.exec(R"(
        CREATE TABLE IF NOT EXISTS queue (
            position INTEGER PRIMARY KEY,
            article_id TEXT NOT NULL
        )
    )");
    query.exec(R"(
        CREATE TABLE IF NOT EXISTS settings (
            key TEXT PRIMARY KEY,
            value TEXT
        )
    )");

    QSqlQuery fts(m_db);
    m_fts = fts.exec("CREATE VIRTUAL TABLE IF NOT EXISTS articles_fts USING fts5("
                     "article_id UNINDEXED, title, author, excerpt, content_text)");
    fts.exec("PRAGMA user_version=1");
}

QString Library::defaultSaveDirectory() const {
    if (m_customDataDirectory)
        return m_dataDirectory + "/exports";
    QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (documents.isEmpty())
        documents = QDir::homePath();
    return documents + "/sift";
}

QString Library::saveDirectory() const {
    QString directory = setting("save_dir");
    if (directory.isEmpty())
        directory = defaultSaveDirectory();
    if (directory.startsWith("~/"))
        directory = QDir::homePath() + directory.mid(1);
    QDir().mkpath(directory);
    return directory;
}

QString Library::sha256(const QByteArray &data) {
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

QString Library::yamlQuote(const QString &value) {
    QString escaped = value;
    escaped.replace('\\', "\\\\");
    escaped.replace('"', "\\\"");
    escaped.replace('\n', "\\n");
    escaped.replace('\r', "");
    return "\"" + escaped + "\"";
}

QString Library::slug(const QString &title) {
    QString value = title.toLower().trimmed();
    value.replace(QRegularExpression("[^\\p{L}\\p{N}]+"), "-");
    value.remove(QRegularExpression("^-+|-+$"));
    if (value.isEmpty())
        value = "article";
    if (value.size() > 80)
        value = value.left(80);
    return value;
}

QString Library::uniqueMarkdownPath(const QString &saveDir, const QString &slugValue, const QString &hash,
                                    const QString &oldPath, const QString &oldHash) const {
    const QString base = QStringLiteral("%1/%2-%3.md").arg(saveDir, slugValue, hash.left(8));
    auto matchesOld = [&](const QString &path) {
        if (path != oldPath || oldHash.isEmpty() || !QFileInfo::exists(path))
            return false;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return false;
        return sha256(file.readAll()) == oldHash;
    };
    if (!QFileInfo::exists(base) || matchesOld(base))
        return base;
    for (int i = 2; i < 1000; ++i) {
        const QString candidate = QStringLiteral("%1/%2-%3-%4.md").arg(saveDir, slugValue, hash.left(8)).arg(i);
        if (!QFileInfo::exists(candidate) || matchesOld(candidate))
            return candidate;
    }
    return base;
}

bool Library::writeMarkdown(const QString &id, const QVariantMap &data, const QString &oldPath,
                            const QString &oldHash, QString *path, QString *hash, QString *error) {
    Q_UNUSED(id)
    const QString title = data.value("title").toString().trimmed().isEmpty()
        ? QStringLiteral("Untitled") : data.value("title").toString().trimmed();
    const QString canonical = data.value("canonicalUrl", data.value("url")).toString();
    const QString body = data.value("markdown").toString().trimmed();
    const QString contentText = data.value("contentText").toString();
    const QStringList tags = data.value("tags").toStringList();

    QString text = body;
    if (text.isEmpty())
        text = contentText;
    if (!text.startsWith("# "))
        text = "# " + title + "\n\n" + text;
    if (!text.endsWith('\n'))
        text += '\n';

    QString frontmatter;
    frontmatter += "---\n";
    frontmatter += "title: " + yamlQuote(title) + "\n";
    frontmatter += "source: " + yamlQuote(data.value("url").toString()) + "\n";
    if (!data.value("author").toString().trimmed().isEmpty())
        frontmatter += "author: " + yamlQuote(data.value("author").toString()) + "\n";
    if (!data.value("published").toString().trimmed().isEmpty())
        frontmatter += "published: " + yamlQuote(data.value("published").toString()) + "\n";
    frontmatter += "clipped: " + yamlQuote(data.value("clippedAt", now()).toString()) + "\n";
    frontmatter += "domain: " + yamlQuote(data.value("domain").toString()) + "\n";
    frontmatter += "word_count: " + QString::number(data.value("wordCount").toInt()) + "\n";
    if (tags.isEmpty()) {
        frontmatter += "tags: []\n";
    } else {
        frontmatter += "tags:\n";
        for (const QString &tag : tags)
            frontmatter += "  - " + yamlQuote(tag) + "\n";
    }
    frontmatter += "---\n\n";

    const QString directory = saveDirectory();
    QDir().mkpath(directory);
    const QString target = uniqueMarkdownPath(directory, slug(title), sha256(canonical.toUtf8()),
                                              oldPath, oldHash);
    QSaveFile file(target);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    const QByteArray contents = (frontmatter + text).toUtf8();
    if (file.write(contents) != contents.size() || !file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    if (path)
        *path = target;
    if (hash)
        *hash = sha256(contents);
    return true;
}

bool Library::writeArticleFiles(const QString &id, const QVariantMap &data, const QString &oldMarkdownPath,
                                const QString &oldFileHash, QString *markdownPath, QString *fileHash,
                                QString *contentPath, QString *error) {
    const QString directory = m_dataDirectory + "/articles/" + id;
    QDir().mkpath(directory);
    const QString htmlPath = directory + "/content.html";
    QSaveFile html(htmlPath);
    if (!html.open(QIODevice::WriteOnly)) {
        if (error)
            *error = html.errorString();
        return false;
    }
    const QByteArray content = data.value("contentHtml").toString().toUtf8();
    if (html.write(content) != content.size() || !html.commit()) {
        if (error)
            *error = html.errorString();
        return false;
    }
    if (!writeMarkdown(id, data, oldMarkdownPath, oldFileHash, markdownPath, fileHash, error))
        return false;
    if (contentPath)
        *contentPath = htmlPath;
    return true;
}

bool Library::saveArticle(const QVariantMap &input, QString *articleId, QString *error) {
    QVariantMap data = input;
    QString url = data.value("url").toString().trimmed();
    if (url.isEmpty()) {
        if (error)
            *error = tr("The page has no URL.");
        return false;
    }
    QString canonical = data.value("canonicalUrl").toString().trimmed();
    if (canonical.isEmpty())
        canonical = url;
    data["url"] = url;
    data["canonicalUrl"] = canonical;

    QString id;
    QString oldMarkdownPath;
    QString oldFileHash;
    int readState = 0;
    int lastSentence = 0;
    QString clippedAt;

    QSqlQuery existing(m_db);
    existing.prepare("SELECT id, markdown_path, file_hash, read_state, last_sentence, clipped_at "
                     "FROM articles WHERE canonical_url = ? OR url = ? ORDER BY updated_at DESC LIMIT 1");
    existing.addBindValue(canonical);
    existing.addBindValue(url);
    if (existing.exec() && existing.next()) {
        id = existing.value(0).toString();
        oldMarkdownPath = existing.value(1).toString();
        oldFileHash = existing.value(2).toString();
        readState = existing.value(3).toInt();
        lastSentence = existing.value(4).toInt();
        clippedAt = existing.value(5).toString();
    } else {
        id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }

    if (data.value("title").toString().trimmed().isEmpty())
        data["title"] = "Untitled";
    if (data.value("domain").toString().isEmpty())
        data["domain"] = QUrl(url).host();
    if (data.value("language").toString().isEmpty())
        data["language"] = "en";
    if (data.value("excerpt").toString().isEmpty())
        data["excerpt"] = cleanText(data.value("contentText").toString()).left(240);
    if (data.value("tags").toStringList().isEmpty() && !id.isEmpty()) {
        const QStringList existingTags = tagsFor(id);
        if (!existingTags.isEmpty())
            data["tags"] = existingTags;
    }
    data["clippedAt"] = clippedAt.isEmpty() ? now() : clippedAt;
    data["updatedAt"] = now();

    QString markdownPath;
    QString fileHash;
    QString contentPath;
    if (!writeArticleFiles(id, data, oldMarkdownPath, oldFileHash,
                           &markdownPath, &fileHash, &contentPath, error))
        return false;

    const QString contentHtml = data.value("contentHtml").toString();
    const QString contentHash = sha256(contentHtml.toUtf8());

    if (oldMarkdownPath.isEmpty()) {
        QSqlQuery insert(m_db);
        insert.prepare("INSERT INTO articles "
                       "(id, url, canonical_url, title, author, published, domain, language, word_count, "
                       "excerpt, content_text, content_path, markdown_path, clipped_at, updated_at, "
                       "read_state, last_sentence, content_hash, file_hash) "
                       "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 0, 0, ?, ?)");
        insert.addBindValue(id);
        insert.addBindValue(url);
        insert.addBindValue(canonical);
        insert.addBindValue(data.value("title"));
        insert.addBindValue(data.value("author"));
        insert.addBindValue(data.value("published"));
        insert.addBindValue(data.value("domain"));
        insert.addBindValue(data.value("language"));
        insert.addBindValue(data.value("wordCount").toInt());
        insert.addBindValue(data.value("excerpt"));
        insert.addBindValue(cleanText(data.value("contentText").toString()));
        insert.addBindValue(contentPath);
        insert.addBindValue(markdownPath);
        insert.addBindValue(data.value("clippedAt"));
        insert.addBindValue(data.value("updatedAt"));
        insert.addBindValue(contentHash);
        insert.addBindValue(fileHash);
        if (!insert.exec()) {
            if (error)
                *error = insert.lastError().text();
            return false;
        }
    } else {
        QSqlQuery update(m_db);
        update.prepare("UPDATE articles SET url=?, canonical_url=?, title=?, author=?, published=?, domain=?, "
                       "language=?, word_count=?, excerpt=?, content_text=?, content_path=?, markdown_path=?, "
                       "updated_at=?, read_state=?, last_sentence=?, content_hash=?, file_hash=? WHERE id=?");
        update.addBindValue(url);
        update.addBindValue(canonical);
        update.addBindValue(data.value("title"));
        update.addBindValue(data.value("author"));
        update.addBindValue(data.value("published"));
        update.addBindValue(data.value("domain"));
        update.addBindValue(data.value("language"));
        update.addBindValue(data.value("wordCount").toInt());
        update.addBindValue(data.value("excerpt"));
        update.addBindValue(cleanText(data.value("contentText").toString()));
        update.addBindValue(contentPath);
        update.addBindValue(markdownPath);
        update.addBindValue(data.value("updatedAt"));
        update.addBindValue(readState);
        update.addBindValue(lastSentence);
        update.addBindValue(contentHash);
        update.addBindValue(fileHash);
        update.addBindValue(id);
        if (!update.exec()) {
            if (error)
                *error = update.lastError().text();
            return false;
        }
        if (oldMarkdownPath != markdownPath && !oldMarkdownPath.isEmpty() && !oldFileHash.isEmpty()
            && QFileInfo::exists(oldMarkdownPath)) {
            QFile oldFile(oldMarkdownPath);
            if (oldFile.open(QIODevice::ReadOnly) && sha256(oldFile.readAll()) == oldFileHash) {
                oldFile.close();
                QFile::remove(oldMarkdownPath);
            }
        }
    }

    if (data.contains("tags"))
        setTags(id, data.value("tags").toStringList());
    updateFts(id, data);

    if (articleId)
        *articleId = id;
    emit changed();
    return true;
}

void Library::updateFts(const QString &id, const QVariantMap &data) {
    if (!m_fts)
        return;
    QSqlQuery remove(m_db);
    remove.prepare("DELETE FROM articles_fts WHERE article_id = ?");
    remove.addBindValue(id);
    remove.exec();

    QSqlQuery insert(m_db);
    insert.prepare("INSERT INTO articles_fts(article_id, title, author, excerpt, content_text) "
                   "VALUES (?, ?, ?, ?, ?)");
    insert.addBindValue(id);
    insert.addBindValue(data.value("title").toString());
    insert.addBindValue(data.value("author").toString());
    insert.addBindValue(data.value("excerpt").toString());
    insert.addBindValue(cleanText(data.value("contentText").toString()));
    insert.exec();
}

void Library::removeFts(const QString &id) {
    if (!m_fts)
        return;
    QSqlQuery remove(m_db);
    remove.prepare("DELETE FROM articles_fts WHERE article_id = ?");
    remove.addBindValue(id);
    remove.exec();
}

QStringList Library::tagsFor(const QString &articleId) const {
    QStringList tags;
    QSqlQuery query(m_db);
    query.prepare("SELECT t.name FROM tags t JOIN article_tags at ON at.tag_id = t.id "
                  "WHERE at.article_id = ? ORDER BY t.name COLLATE NOCASE");
    query.addBindValue(articleId);
    if (query.exec()) {
        while (query.next())
            tags << query.value(0).toString();
    }
    return tags;
}

QVariantMap Library::rowToArticle(const QSqlQuery &query) const {
    QVariantMap article;
    article["id"] = query.value("id");
    article["url"] = query.value("url");
    article["canonicalUrl"] = query.value("canonical_url");
    article["title"] = query.value("title");
    article["author"] = query.value("author");
    article["published"] = query.value("published");
    article["domain"] = query.value("domain");
    article["language"] = query.value("language");
    article["wordCount"] = query.value("word_count");
    article["excerpt"] = query.value("excerpt");
    article["readState"] = query.value("read_state");
    article["lastSentence"] = query.value("last_sentence");
    article["clippedAt"] = query.value("clipped_at");
    article["updatedAt"] = query.value("updated_at");
    article["markdownPath"] = query.value("markdown_path");
    article["contentPath"] = query.value("content_path");
    article["fileHash"] = query.value("file_hash");
    article["tags"] = tagsFor(article.value("id").toString());
    return article;
}

QVariantList Library::articles(const QString &filter, const QString &tag, const QString &search) const {
    QStringList where;
    QVariantList binds;

    if (filter == "unread")
        where << "a.read_state = 0";
    else if (filter == "reading")
        where << "a.read_state = 1";
    else if (filter == "read")
        where << "a.read_state = 2";

    if (!tag.trimmed().isEmpty()) {
        where << "EXISTS (SELECT 1 FROM article_tags at2 JOIN tags t2 ON t2.id = at2.tag_id "
                 "WHERE at2.article_id = a.id AND t2.name = ? COLLATE NOCASE)";
        binds << tag.trimmed();
    }

    const QString trimmedSearch = search.trimmed();
    if (!trimmedSearch.isEmpty()) {
        if (m_fts) {
            QStringList terms;
            const auto parts = trimmedSearch.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            for (const QString &part : parts) {
                QString term = part;
                term.remove('"');
                if (!term.isEmpty())
                    terms << "\"" + term + "\"*";
            }
            if (!terms.isEmpty()) {
                where << "a.id IN (SELECT article_id FROM articles_fts WHERE articles_fts MATCH ?)";
                binds << terms.join(' ');
            }
        } else {
            where << "(a.title LIKE ? OR a.author LIKE ? OR a.excerpt LIKE ? OR a.content_text LIKE ?)";
            const QString like = "%" + trimmedSearch + "%";
            binds << like << like << like << like;
        }
    }

    QString sql = "SELECT a.* FROM articles a";
    if (!where.isEmpty())
        sql += " WHERE " + where.join(" AND ");
    sql += " ORDER BY a.clipped_at DESC";

    QSqlQuery query(m_db);
    query.prepare(sql);
    for (const auto &bind : binds)
        query.addBindValue(bind);

    QVariantList result;
    if (!query.exec()) {
        // A malformed FTS query should still leave the library usable.
        if (m_fts && !trimmedSearch.isEmpty()) {
            QSqlQuery fallback(m_db);
            QString fallbackSql = "SELECT a.* FROM articles a";
            QStringList fallbackWhere;
            QVariantList fallbackBinds;
            if (filter == "unread") fallbackWhere << "a.read_state = 0";
            else if (filter == "reading") fallbackWhere << "a.read_state = 1";
            else if (filter == "read") fallbackWhere << "a.read_state = 2";
            if (!tag.trimmed().isEmpty()) {
                fallbackWhere << "EXISTS (SELECT 1 FROM article_tags at2 JOIN tags t2 ON t2.id = at2.tag_id "
                                 "WHERE at2.article_id = a.id AND t2.name = ? COLLATE NOCASE)";
                fallbackBinds << tag.trimmed();
            }
            fallbackWhere << "(a.title LIKE ? OR a.author LIKE ? OR a.excerpt LIKE ? OR a.content_text LIKE ?)";
            const QString like = "%" + trimmedSearch + "%";
            fallbackBinds << like << like << like << like;
            if (!fallbackWhere.isEmpty())
                fallbackSql += " WHERE " + fallbackWhere.join(" AND ");
            fallbackSql += " ORDER BY a.clipped_at DESC";
            fallback.prepare(fallbackSql);
            for (const auto &bind : fallbackBinds)
                fallback.addBindValue(bind);
            fallback.exec();
            while (fallback.next())
                result << rowToArticle(fallback);
            return result;
        }
        return result;
    }
    while (query.next())
        result << rowToArticle(query);
    return result;
}

QVariantMap Library::article(const QString &id) const {
    QSqlQuery query(m_db);
    query.prepare("SELECT * FROM articles WHERE id = ?");
    query.addBindValue(id);
    if (query.exec() && query.next()) {
        QVariantMap result = rowToArticle(query);
        result["hasContent"] = !result.value("contentPath").toString().isEmpty()
            && QFileInfo::exists(result.value("contentPath").toString());
        return result;
    }
    return {};
}

QString Library::articleHtml(const QString &id) const {
    const QVariantMap record = article(id);
    const QString path = record.value("contentPath").toString();
    if (path.isEmpty())
        return {};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.readAll());
}

QString Library::articleMarkdown(const QString &id) const {
    const QVariantMap record = article(id);
    return markdownFor(record.value("markdownPath").toString());
}

QString Library::markdownFor(const QString &path) const {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.readAll());
}

bool Library::deleteArticle(const QString &id) {
    const QVariantMap record = article(id);
    if (record.isEmpty())
        return false;
    const QString markdownPath = record.value("markdownPath").toString();
    const QString contentPath = record.value("contentPath").toString();
    const QString storedHash = [&] {
        QSqlQuery query(m_db);
        query.prepare("SELECT file_hash FROM articles WHERE id = ?");
        query.addBindValue(id);
        if (query.exec() && query.next())
            return query.value(0).toString();
        return QString();
    }();

    if (!contentPath.isEmpty()) {
        const QFileInfo info(contentPath);
        QDir(info.absolutePath()).removeRecursively();
    }
    if (!markdownPath.isEmpty() && QFileInfo::exists(markdownPath)) {
        QFile file(markdownPath);
        if (file.open(QIODevice::ReadOnly) && sha256(file.readAll()) == storedHash) {
            file.close();
            QFile::remove(markdownPath);
        }
    }

    QSqlQuery query(m_db);
    query.prepare("DELETE FROM article_tags WHERE article_id = ?");
    query.addBindValue(id);
    query.exec();
    query.prepare("DELETE FROM playlist_items WHERE article_id = ?");
    query.addBindValue(id);
    query.exec();
    query.prepare("DELETE FROM queue WHERE article_id = ?");
    query.addBindValue(id);
    query.exec();
    query.prepare("DELETE FROM articles WHERE id = ?");
    query.addBindValue(id);
    query.exec();
    removeFts(id);
    emit changed();
    return true;
}

QVariantList Library::tags() const {
    QVariantList result;
    QSqlQuery query(m_db);
    if (query.exec("SELECT t.name, COUNT(at.article_id) AS count FROM tags t "
                   "LEFT JOIN article_tags at ON at.tag_id = t.id "
                   "GROUP BY t.id HAVING COUNT(at.article_id) > 0 ORDER BY t.name COLLATE NOCASE")) {
        while (query.next()) {
            QVariantMap tag;
            tag["name"] = query.value(0).toString();
            tag["count"] = query.value(1).toInt();
            result << tag;
        }
    }
    return result;
}

void Library::setTags(const QString &articleId, const QStringList &tags) {
    QSqlQuery clear(m_db);
    clear.prepare("DELETE FROM article_tags WHERE article_id = ?");
    clear.addBindValue(articleId);
    clear.exec();

    QStringList seen;
    for (const QString &raw : tags) {
        const QString name = raw.trimmed();
        if (name.isEmpty() || seen.contains(name, Qt::CaseInsensitive))
            continue;
        seen << name;

        QSqlQuery insertTag(m_db);
        insertTag.prepare("INSERT OR IGNORE INTO tags(name) VALUES (?)");
        insertTag.addBindValue(name);
        insertTag.exec();

        QSqlQuery tagId(m_db);
        tagId.prepare("SELECT id FROM tags WHERE name = ? COLLATE NOCASE");
        tagId.addBindValue(name);
        if (tagId.exec() && tagId.next()) {
            QSqlQuery link(m_db);
            link.prepare("INSERT OR IGNORE INTO article_tags(article_id, tag_id) VALUES (?, ?)");
            link.addBindValue(articleId);
            link.addBindValue(tagId.value(0));
            link.exec();
        }
    }

    // Keep the Markdown frontmatter in step with the library, but never
    // overwrite a file the user has edited outside sift.
    const QVariantMap record = article(articleId);
    const QString markdownPath = record.value("markdownPath").toString();
    const QString storedHash = record.value("fileHash").toString();
    if (!markdownPath.isEmpty() && QFileInfo::exists(markdownPath)) {
        QFile file(markdownPath);
        if (file.open(QIODevice::ReadOnly)) {
            const QByteArray current = file.readAll();
            file.close();
            if (sha256(current) == storedHash) {
                QString body = QString::fromUtf8(current);
                if (body.startsWith("---\n")) {
                    const int end = body.indexOf("\n---\n", 4);
                    if (end >= 0)
                        body = body.mid(end + 5);
                }
                QVariantMap data = record;
                data["tags"] = seen;
                data["markdown"] = body;
                QString newPath, newHash, error;
                if (writeMarkdown(articleId, data, markdownPath, storedHash, &newPath, &newHash, &error)) {
                    QSqlQuery update(m_db);
                    update.prepare("UPDATE articles SET markdown_path = ?, file_hash = ? WHERE id = ?");
                    update.addBindValue(newPath);
                    update.addBindValue(newHash);
                    update.addBindValue(articleId);
                    update.exec();
                }
            }
        }
    }
    emit changed();
}

void Library::setReadState(const QString &articleId, int state) {
    QSqlQuery query(m_db);
    query.prepare("UPDATE articles SET read_state = ? WHERE id = ?");
    query.addBindValue(state);
    query.addBindValue(articleId);
    if (query.exec())
        emit changed();
}

void Library::saveProgress(const QString &articleId, int sentence) {
    QSqlQuery query(m_db);
    query.prepare("UPDATE articles SET last_sentence = ?, last_played_at = ? WHERE id = ?");
    query.addBindValue(qMax(0, sentence));
    query.addBindValue(now());
    query.addBindValue(articleId);
    query.exec();
}

QVariantList Library::playlists() const {
    QVariantList result;
    QSqlQuery query(m_db);
    if (query.exec("SELECT p.id, p.name, COUNT(i.article_id) AS count FROM playlists p "
                   "LEFT JOIN playlist_items i ON i.playlist_id = p.id "
                   "GROUP BY p.id ORDER BY p.name COLLATE NOCASE")) {
        while (query.next()) {
            QVariantMap playlist;
            playlist["id"] = query.value(0).toString();
            playlist["name"] = query.value(1).toString();
            playlist["count"] = query.value(2).toInt();
            result << playlist;
        }
    }
    return result;
}

QString Library::createPlaylist(const QString &name) {
    const QString clean = name.trimmed();
    if (clean.isEmpty())
        return {};
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QSqlQuery query(m_db);
    query.prepare("INSERT INTO playlists(id, name, created_at, updated_at) VALUES (?, ?, ?, ?)");
    query.addBindValue(id);
    query.addBindValue(clean);
    query.addBindValue(now());
    query.addBindValue(now());
    if (!query.exec())
        return {};
    emit changed();
    return id;
}

bool Library::deletePlaylist(const QString &id) {
    QSqlQuery query(m_db);
    query.prepare("DELETE FROM playlist_items WHERE playlist_id = ?");
    query.addBindValue(id);
    query.exec();
    query.prepare("DELETE FROM playlists WHERE id = ?");
    query.addBindValue(id);
    const bool ok = query.exec();
    if (ok)
        emit changed();
    return ok;
}

bool Library::renamePlaylist(const QString &id, const QString &name) {
    const QString clean = name.trimmed();
    if (clean.isEmpty())
        return false;
    QSqlQuery query(m_db);
    query.prepare("UPDATE playlists SET name = ?, updated_at = ? WHERE id = ?");
    query.addBindValue(clean);
    query.addBindValue(now());
    query.addBindValue(id);
    const bool ok = query.exec();
    if (ok)
        emit changed();
    return ok;
}

QVariantList Library::playlistItems(const QString &playlistId) const {
    QVariantList result;
    QSqlQuery query(m_db);
    query.prepare("SELECT article_id FROM playlist_items WHERE playlist_id = ? ORDER BY position");
    query.addBindValue(playlistId);
    if (query.exec()) {
        while (query.next()) {
            const QVariantMap item = article(query.value(0).toString());
            if (!item.isEmpty())
                result << item;
        }
    }
    return result;
}

bool Library::addToPlaylist(const QString &playlistId, const QString &articleId) {
    QSqlQuery exists(m_db);
    exists.prepare("SELECT 1 FROM playlist_items WHERE playlist_id = ? AND article_id = ?");
    exists.addBindValue(playlistId);
    exists.addBindValue(articleId);
    if (exists.exec() && exists.next())
        return true;

    QSqlQuery position(m_db);
    position.prepare("SELECT COALESCE(MAX(position), -1) + 1 FROM playlist_items WHERE playlist_id = ?");
    position.addBindValue(playlistId);
    int next = 0;
    if (position.exec() && position.next())
        next = position.value(0).toInt();

    QSqlQuery insert(m_db);
    insert.prepare("INSERT INTO playlist_items(playlist_id, article_id, position) VALUES (?, ?, ?)");
    insert.addBindValue(playlistId);
    insert.addBindValue(articleId);
    insert.addBindValue(next);
    const bool ok = insert.exec();
    if (ok) {
        QSqlQuery touch(m_db);
        touch.prepare("UPDATE playlists SET updated_at = ? WHERE id = ?");
        touch.addBindValue(now());
        touch.addBindValue(playlistId);
        touch.exec();
        emit changed();
    }
    return ok;
}

bool Library::removeFromPlaylist(const QString &playlistId, const QString &articleId) {
    QSqlQuery position(m_db);
    position.prepare("SELECT position FROM playlist_items WHERE playlist_id = ? AND article_id = ?");
    position.addBindValue(playlistId);
    position.addBindValue(articleId);
    int removed = -1;
    if (position.exec() && position.next())
        removed = position.value(0).toInt();

    QSqlQuery remove(m_db);
    remove.prepare("DELETE FROM playlist_items WHERE playlist_id = ? AND article_id = ?");
    remove.addBindValue(playlistId);
    remove.addBindValue(articleId);
    const bool ok = remove.exec();
    if (ok && removed >= 0) {
        QSqlQuery shift(m_db);
        shift.prepare("UPDATE playlist_items SET position = position - 1 "
                      "WHERE playlist_id = ? AND position > ?");
        shift.addBindValue(playlistId);
        shift.addBindValue(removed);
        shift.exec();
        emit changed();
    }
    return ok;
}

bool Library::movePlaylistItem(const QString &playlistId, int from, int to) {
    QStringList ids;
    QSqlQuery query(m_db);
    query.prepare("SELECT article_id FROM playlist_items WHERE playlist_id = ? ORDER BY position");
    query.addBindValue(playlistId);
    if (query.exec()) {
        while (query.next())
            ids << query.value(0).toString();
    }
    if (from < 0 || from >= ids.size() || to < 0 || to >= ids.size() || from == to)
        return false;
    ids.move(from, to);
    m_db.transaction();
    for (int i = 0; i < ids.size(); ++i) {
        QSqlQuery update(m_db);
        update.prepare("UPDATE playlist_items SET position = ? WHERE playlist_id = ? AND article_id = ?");
        update.addBindValue(i);
        update.addBindValue(playlistId);
        update.addBindValue(ids.at(i));
        update.exec();
    }
    m_db.commit();
    emit changed();
    return true;
}

QVariantList Library::queue() const {
    QVariantList result;
    QSqlQuery query(m_db);
    if (query.exec("SELECT article_id FROM queue ORDER BY position")) {
        while (query.next()) {
            const QVariantMap item = article(query.value(0).toString());
            if (!item.isEmpty())
                result << item;
        }
    }
    return result;
}

void Library::setQueue(const QStringList &articleIds) {
    m_db.transaction();
    QSqlQuery clear(m_db);
    clear.exec("DELETE FROM queue");
    int position = 0;
    for (const QString &id : articleIds) {
        QSqlQuery insert(m_db);
        insert.prepare("INSERT OR REPLACE INTO queue(position, article_id) VALUES (?, ?)");
        insert.addBindValue(position++);
        insert.addBindValue(id);
        insert.exec();
    }
    m_db.commit();
    emit changed();
}

bool Library::addToQueue(const QString &articleId) {
    QSqlQuery exists(m_db);
    exists.prepare("SELECT 1 FROM queue WHERE article_id = ?");
    exists.addBindValue(articleId);
    if (exists.exec() && exists.next())
        return true;

    QSqlQuery position(m_db);
    position.exec("SELECT COALESCE(MAX(position), -1) + 1 FROM queue");
    int next = 0;
    if (position.next())
        next = position.value(0).toInt();

    QSqlQuery insert(m_db);
    insert.prepare("INSERT INTO queue(position, article_id) VALUES (?, ?)");
    insert.addBindValue(next);
    insert.addBindValue(articleId);
    const bool ok = insert.exec();
    if (ok)
        emit changed();
    return ok;
}

bool Library::removeFromQueue(const QString &articleId) {
    QSqlQuery position(m_db);
    position.prepare("SELECT position FROM queue WHERE article_id = ?");
    position.addBindValue(articleId);
    int removed = -1;
    if (position.exec() && position.next())
        removed = position.value(0).toInt();

    QSqlQuery remove(m_db);
    remove.prepare("DELETE FROM queue WHERE article_id = ?");
    remove.addBindValue(articleId);
    const bool ok = remove.exec();
    if (ok && removed >= 0) {
        QStringList remaining;
        QSqlQuery order(m_db);
        if (order.exec("SELECT article_id FROM queue ORDER BY position")) {
            while (order.next())
                remaining << order.value(0).toString();
        }
        m_db.transaction();
        QSqlQuery clear(m_db);
        clear.exec("DELETE FROM queue");
        for (int i = 0; i < remaining.size(); ++i) {
            QSqlQuery insert(m_db);
            insert.prepare("INSERT INTO queue(position, article_id) VALUES (?, ?)");
            insert.addBindValue(i);
            insert.addBindValue(remaining.at(i));
            insert.exec();
        }
        m_db.commit();
        emit changed();
    }
    return ok;
}

bool Library::moveQueueItem(int from, int to) {
    QStringList ids;
    QSqlQuery query(m_db);
    if (query.exec("SELECT article_id FROM queue ORDER BY position")) {
        while (query.next())
            ids << query.value(0).toString();
    }
    if (from < 0 || from >= ids.size() || to < 0 || to >= ids.size() || from == to)
        return false;
    ids.move(from, to);
    m_db.transaction();
    QSqlQuery clear(m_db);
    clear.exec("DELETE FROM queue");
    for (int i = 0; i < ids.size(); ++i) {
        QSqlQuery insert(m_db);
        insert.prepare("INSERT INTO queue(position, article_id) VALUES (?, ?)");
        insert.addBindValue(i);
        insert.addBindValue(ids.at(i));
        insert.exec();
    }
    m_db.commit();
    emit changed();
    return true;
}

void Library::clearQueue() {
    QSqlQuery query(m_db);
    query.exec("DELETE FROM queue");
    emit changed();
}

QString Library::setting(const QString &key, const QString &fallback) const {
    if (!m_db.isOpen())
        return fallback;
    QSqlQuery query(m_db);
    query.prepare("SELECT value FROM settings WHERE key = ?");
    query.addBindValue(key);
    if (query.exec() && query.next())
        return query.value(0).toString();
    return fallback;
}

void Library::setSetting(const QString &key, const QString &value) {
    if (!m_db.isOpen())
        return;
    QSqlQuery query(m_db);
    query.prepare("INSERT INTO settings(key, value) VALUES (?, ?) "
                  "ON CONFLICT(key) DO UPDATE SET value = excluded.value");
    query.addBindValue(key);
    query.addBindValue(value);
    query.exec();
}
