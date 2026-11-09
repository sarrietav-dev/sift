#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

class Extractor;
class Kokoro;
class Library;
class Theme;
class TtsEngine;

// The app's coordinator: extracts pages, stores them, drives the reader, and
// owns the playback queue and playlists.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList articles READ articles NOTIFY articlesChanged)
    Q_PROPERTY(QVariantList playlists READ playlists NOTIFY playlistsChanged)
    Q_PROPERTY(QVariantList queue READ queue NOTIFY queueChanged)
    Q_PROPERTY(QVariantList tags READ tags NOTIFY tagsChanged)
    Q_PROPERTY(QVariantList voices READ voices NOTIFY voicesChanged)
    Q_PROPERTY(QVariantList selectedPlaylistItems READ selectedPlaylistItems NOTIFY selectedPlaylistItemsChanged)
    Q_PROPERTY(QVariantMap currentArticle READ currentArticle NOTIFY currentArticleChanged)
    Q_PROPERTY(QString selectedPlaylistId READ selectedPlaylistId NOTIFY selectedPlaylistChanged)
    Q_PROPERTY(QString filter READ filter NOTIFY filterChanged)
    Q_PROPERTY(QString search READ search NOTIFY searchChanged)
    Q_PROPERTY(QString selectedTag READ selectedTag NOTIFY selectedTagChanged)
    Q_PROPERTY(QString section READ section NOTIFY sectionChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString ttsStatus READ ttsStatus NOTIFY ttsStatusChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QString voice READ voice NOTIFY voiceChanged)
    Q_PROPERTY(QString kokoroStatus READ kokoroStatus NOTIFY kokoroChanged)
    Q_PROPERTY(QString kokoroEndpoint READ kokoroEndpoint NOTIFY settingsChanged)
    Q_PROPERTY(QString saveDirectory READ saveDirectory NOTIFY settingsChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool hasArticle READ hasArticle NOTIFY currentArticleChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(bool buffering READ buffering NOTIFY bufferingChanged)
    Q_PROPERTY(bool kokoroHealthy READ kokoroHealthy NOTIFY kokoroChanged)
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(int currentSentence READ currentSentence NOTIFY currentSentenceChanged)
    Q_PROPERTY(int sentenceCount READ sentenceCount NOTIFY sentenceCountChanged)
    Q_PROPERTY(int fontSize READ fontSize NOTIFY fontSizeChanged)
    Q_PROPERTY(QString readerFont READ readerFont NOTIFY readerFontChanged)
    Q_PROPERTY(QVariantList fonts READ fonts CONSTANT)
    Q_PROPERTY(double speed READ speed NOTIFY speedChanged)

public:
    explicit Backend(Theme *theme, QObject *parent = nullptr);

    QVariantList articles() const { return m_articles; }
    QVariantList playlists() const { return m_playlists; }
    QVariantList queue() const { return m_queue; }
    QVariantList tags() const { return m_tags; }
    QVariantList voices() const;
    QVariantList fonts() const;
    QVariantList selectedPlaylistItems() const { return m_selectedPlaylistItems; }
    QVariantMap currentArticle() const { return m_current; }
    QString selectedPlaylistId() const { return m_selectedPlaylistId; }
    QString filter() const { return m_filter; }
    QString search() const { return m_search; }
    QString selectedTag() const { return m_selectedTag; }
    QString section() const { return m_section; }
    QString status() const { return m_status; }
    QString ttsStatus() const;
    QString error() const { return m_error; }
    QString voice() const;
    QString kokoroStatus() const;
    QString kokoroEndpoint() const;
    QString saveDirectory() const;
    bool busy() const;
    bool hasArticle() const { return !m_currentId.isEmpty(); }
    bool playing() const;
    bool buffering() const;
    bool kokoroHealthy() const;
    int progress() const;
    int currentSentence() const;
    int sentenceCount() const;
    int fontSize() const { return m_fontSize; }
    QString readerFont() const { return m_readerFont; }
    double speed() const;

    Q_INVOKABLE void addUrl(const QString &url);
    Q_INVOKABLE void openUrl(const QString &url, bool autoplay = true);
    Q_INVOKABLE void openArticle(const QString &id, bool autoplay = false);
    Q_INVOKABLE void sentencesReady(const QVariantList &sentences);
    Q_INVOKABLE void handleReaderUrl(const QString &url);
    Q_INVOKABLE void openExternal(const QString &url);
    Q_INVOKABLE void openOriginal();

    Q_INVOKABLE void playPause();
    Q_INVOKABLE void nextSentence();
    Q_INVOKABLE void previousSentence();
    Q_INVOKABLE void playFromSentence(int index);
    Q_INVOKABLE void seekSentence(int index);
    Q_INVOKABLE void setSpeed(double speed);
    Q_INVOKABLE void setVoice(const QString &voice);

    Q_INVOKABLE void setFilter(const QString &filter);
    Q_INVOKABLE void setSearch(const QString &search);
    Q_INVOKABLE void setTag(const QString &tag);
    Q_INVOKABLE void setSection(const QString &section);
    Q_INVOKABLE void setFontSize(int size);
    Q_INVOKABLE void setReaderFont(const QString &family);

    Q_INVOKABLE void toggleCurrentRead();
    Q_INVOKABLE void markCurrentRead();
    Q_INVOKABLE void setCurrentTags(const QString &csv);
    Q_INVOKABLE void deleteCurrent();
    Q_INVOKABLE void copyMarkdown();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void clearError() { setError({}); }

    Q_INVOKABLE QString createPlaylist(const QString &name);
    Q_INVOKABLE void deletePlaylist(const QString &id);
    Q_INVOKABLE void renamePlaylist(const QString &id, const QString &name);
    Q_INVOKABLE void selectPlaylist(const QString &id);
    Q_INVOKABLE void addToPlaylist(const QString &playlistId, const QString &articleId);
    Q_INVOKABLE void addCurrentToPlaylist(const QString &playlistId);
    Q_INVOKABLE void removeFromPlaylist(const QString &playlistId, const QString &articleId);
    Q_INVOKABLE void movePlaylistItem(const QString &playlistId, int from, int to);
    Q_INVOKABLE void playPlaylist(const QString &id);

    Q_INVOKABLE void addToQueue(const QString &articleId);
    Q_INVOKABLE void addCurrentToQueue();
    Q_INVOKABLE void removeFromQueue(const QString &articleId);
    Q_INVOKABLE void clearQueue();
    Q_INVOKABLE void moveQueueItem(int from, int to);
    Q_INVOKABLE void playQueueAt(int index);

    Q_INVOKABLE QVariantMap settings() const;
    Q_INVOKABLE void applySettings(const QVariantMap &settings);
    Q_INVOKABLE void checkKokoro();
    Q_INVOKABLE void refreshVoices();
    Q_INVOKABLE void quit();

signals:
    void articlesChanged();
    void playlistsChanged();
    void queueChanged();
    void tagsChanged();
    void voicesChanged();
    void selectedPlaylistItemsChanged();
    void selectedPlaylistChanged();
    void currentArticleChanged();
    void filterChanged();
    void searchChanged();
    void selectedTagChanged();
    void sectionChanged();
    void statusChanged();
    void ttsStatusChanged();
    void errorChanged();
    void voiceChanged();
    void speedChanged();
    void kokoroChanged();
    void settingsChanged();
    void fontSizeChanged();
    void readerFontChanged();
    void busyChanged();
    void progressChanged();
    void playingChanged();
    void bufferingChanged();
    void currentSentenceChanged();
    void sentenceCountChanged();
    void readerLoad(const QString &html, const QString &baseUrl);
    void highlightSentence(int index);
    void toast(const QString &message);

private:
    void refreshAll();
    void refreshArticles();
    void refreshPlaylists();
    void refreshQueue();
    void refreshTags();
    void refreshCurrent();
    void refreshSelectedPlaylist();
    void loadSettings();
    void setStatus(const QString &status);
    void setError(const QString &error);
    void extracted(const QVariantMap &article);
    void extractionFailed(const QString &error);
    void articleFinished();
    QString buildReaderHtml(const QVariantMap &article) const;
    static QString sanitizeHtml(const QString &html);
    static QString readerDirection(const QString &language);
    static QStringList sentenceTexts(const QVariantList &sentences, QList<int> *blocks = nullptr);

    Theme *m_theme = nullptr;
    Library *m_library = nullptr;
    Extractor *m_extractor = nullptr;
    Kokoro *m_kokoro = nullptr;
    TtsEngine *m_tts = nullptr;

    QVariantList m_articles;
    QVariantList m_playlists;
    QVariantList m_queue;
    QVariantList m_tags;
    QVariantList m_selectedPlaylistItems;
    QVariantMap m_current;
    QString m_currentId;
    QString m_selectedPlaylistId;
    QString m_filter = "all";
    QString m_search;
    QString m_selectedTag;
    QString m_section = "library";
    QString m_status = tr("Ready");
    QString m_error;
    int m_fontSize = 19;
    QString m_readerFont = "IBM Plex Serif";
    int m_queueIndex = -1;
    int m_startSentence = 0;
    bool m_fromQueue = false;
    bool m_autoplayAfterWrap = false;
    bool m_autoplayAfterExtract = false;
    bool m_openAfterExtract = false;
    bool m_wrapping = false;
};
