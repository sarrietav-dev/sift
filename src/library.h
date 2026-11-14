#pragma once

#include <QObject>
#include <QSqlDatabase>
#include <QVariantList>
#include <QVariantMap>

class QSqlQuery;

// Owns sift's durable state: the SQLite index, Markdown exports, article
// snapshots, tags, playlists, and the playback queue.
class Library : public QObject {
    Q_OBJECT

public:
    explicit Library(const QString &dataDirectory = {}, QObject *parent = nullptr);
    ~Library() override;

    QString dataDirectory() const { return m_dataDirectory; }
    QString saveDirectory() const;

    // Saves or updates an extracted article. The id is stable for a canonical
    // URL, so progress survives re-clipping.
    bool saveArticle(const QVariantMap &data, QString *articleId, QString *error);

    QVariantList articles(const QString &filter = "all", const QString &tag = {},
                          const QString &search = {}) const;
    QVariantMap article(const QString &id) const;
    QString articleHtml(const QString &id) const;
    QString articleMarkdown(const QString &id) const;
    bool deleteArticle(const QString &id);

    QVariantList tags() const;
    void setTags(const QString &articleId, const QStringList &tags);
    void setReadState(const QString &articleId, int state);
    void saveProgress(const QString &articleId, int sentence);

    QVariantList playlists() const;
    QString createPlaylist(const QString &name);
    bool deletePlaylist(const QString &id);
    bool renamePlaylist(const QString &id, const QString &name);
    QVariantList playlistItems(const QString &playlistId) const;
    bool addToPlaylist(const QString &playlistId, const QString &articleId);
    bool removeFromPlaylist(const QString &playlistId, const QString &articleId);
    bool movePlaylistItem(const QString &playlistId, int from, int to);

    QVariantList queue() const;
    void setQueue(const QStringList &articleIds);
    bool addToQueue(const QString &articleId);
    bool removeFromQueue(const QString &articleId);
    bool moveQueueItem(int from, int to);
    // Reorders the queue by title, author, or the date each page was added.
    // Sorting writes back positions, replacing the manual order.
    bool sortQueue(const QString &mode);
    void clearQueue();

    QString setting(const QString &key, const QString &fallback = {}) const;
    void setSetting(const QString &key, const QString &value);

    QString markdownFor(const QString &id) const;

signals:
    void changed();

private:
    bool openDatabase(QString *error);
    void migrate();
    bool writeArticleFiles(const QString &id, const QVariantMap &data, const QString &oldMarkdownPath,
                           const QString &oldFileHash, QString *markdownPath, QString *fileHash,
                           QString *contentPath, QString *error);
    bool writeMarkdown(const QString &id, const QVariantMap &data, const QString &oldPath,
                       const QString &oldHash, QString *path, QString *hash, QString *error);
    QString defaultSaveDirectory() const;
    QString uniqueMarkdownPath(const QString &saveDir, const QString &slug, const QString &hash,
                               const QString &oldPath, const QString &oldHash) const;
    void updateFts(const QString &id, const QVariantMap &data);
    void removeFts(const QString &id);
    QVariantMap rowToArticle(const QSqlQuery &query) const;
    QStringList tagsFor(const QString &articleId) const;
    static QString slug(const QString &title);
    static QString sha256(const QByteArray &data);
    static QString yamlQuote(const QString &value);

    QString m_dataDirectory;
    QString m_cacheDirectory;
    bool m_customDataDirectory = false;
    QString m_connectionName;
    QSqlDatabase m_db;
    bool m_fts = false;
};
