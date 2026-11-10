#include "backend.h"

#include "extractor.h"
#include "kokoro.h"
#include "library.h"
#include "theme.h"
#include "ttsengine.h"
#include "windowwatcher.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QDate>
#include <QDateTime>
#include <QFontDatabase>
#include <QLocale>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>

Backend::Backend(Theme *theme, QObject *parent)
    : QObject(parent),
      m_theme(theme),
      m_library(new Library({}, this)),
      m_extractor(new Extractor(this)),
      m_kokoro(new Kokoro(this)),
      m_tts(new TtsEngine(m_kokoro, this)) {
    connect(m_library, &Library::changed, this, &Backend::refreshAll);
    connect(m_extractor, &Extractor::busyChanged, this, &Backend::busyChanged);
    connect(m_extractor, &Extractor::progressChanged, this, &Backend::progressChanged);
    connect(m_extractor, &Extractor::finished, this, &Backend::extracted);
    connect(m_extractor, &Extractor::failed, this, &Backend::extractionFailed);

    connect(m_kokoro, &Kokoro::voicesChanged, this, &Backend::voicesChanged);
    connect(m_kokoro, &Kokoro::healthChanged, this, &Backend::kokoroChanged);
    connect(m_kokoro, &Kokoro::statusChanged, this, &Backend::kokoroChanged);
    connect(m_kokoro, &Kokoro::endpointChanged, this, &Backend::settingsChanged);

    connect(m_tts, &TtsEngine::playingChanged, this, &Backend::playingChanged);
    connect(m_tts, &TtsEngine::bufferingChanged, this, &Backend::bufferingChanged);
    connect(m_tts, &TtsEngine::currentChanged, this, &Backend::currentSentenceChanged);
    connect(m_tts, &TtsEngine::countChanged, this, &Backend::sentenceCountChanged);
    connect(m_tts, &TtsEngine::voiceChanged, this, &Backend::voiceChanged);
    connect(m_tts, &TtsEngine::speedChanged, this, &Backend::speedChanged);
    connect(m_tts, &TtsEngine::statusChanged, this, &Backend::ttsStatusChanged);
    connect(m_tts, &TtsEngine::sentenceChanged, this, [this](int index) {
        if (!m_currentId.isEmpty())
            m_library->saveProgress(m_currentId, index);
        emit highlightSentence(index);
    });
    connect(m_tts, &TtsEngine::error, this, [this](const QString &message) {
        setError(message);
    });
    connect(m_tts, &TtsEngine::articleFinished, this, &Backend::articleFinished);

    loadSettings();
    refreshAll();
    m_kokoro->checkHealth();
    m_kokoro->refreshVoices();
}

void Backend::watchWindow(QWindow *window) {
    auto *watcher = new WindowWatcher(this);
    watcher->setWindow(window);
    connect(watcher, &WindowWatcher::hidden, this, &Backend::readerSuspended);
    connect(watcher, &WindowWatcher::shown, this, &Backend::readerNeedsRepaint);
}

void Backend::loadSettings() {
    m_fontSize = qBound(14, m_library->setting("font_size", "19").toInt(), 32);
    const QString savedFont = m_library->setting("reader_font", "IBM Plex Serif");
    if (!savedFont.trimmed().isEmpty())
        m_readerFont = savedFont;
    if (m_theme)
        m_theme->setAppearance(m_library->setting("appearance", "omarchy"));
    m_kokoro->setEndpoint(m_library->setting("kokoro_url", "http://127.0.0.1:8880"));
    m_tts->setVoice(m_library->setting("voice", "af_heart"));
    m_tts->setSpeed(m_library->setting("speed", "1.0").toDouble());
    m_tts->setCacheLimitMb(m_library->setting("cache_limit_mb", "1024").toInt());
}

QVariantList Backend::voices() const { return m_kokoro->voices(); }

QVariantList Backend::fonts() const {
    QVariantList families;
    const QStringList available = QFontDatabase::families();
    for (const QString &family : available)
        families << family;
    return families;
}
QString Backend::voice() const { return m_tts->voice(); }
QString Backend::ttsStatus() const { return m_tts->status(); }
QString Backend::kokoroStatus() const { return m_kokoro->status(); }
QString Backend::kokoroEndpoint() const { return m_kokoro->endpoint(); }
QString Backend::saveDirectory() const { return m_library->saveDirectory(); }
bool Backend::busy() const { return m_extractor->busy(); }
bool Backend::playing() const { return m_tts->playing(); }
bool Backend::buffering() const { return m_tts->buffering(); }
bool Backend::kokoroHealthy() const { return m_kokoro->healthy(); }
int Backend::progress() const { return m_extractor->progress(); }
int Backend::currentSentence() const { return m_tts->current(); }
int Backend::sentenceCount() const { return m_tts->count(); }
double Backend::speed() const { return m_tts->speed(); }

void Backend::setStatus(const QString &status) {
    if (status == m_status)
        return;
    m_status = status;
    emit statusChanged();
}

void Backend::setError(const QString &error) {
    m_error = error;
    emit errorChanged();
    if (!error.isEmpty())
        setStatus(error);
}

void Backend::refreshAll() {
    refreshArticles();
    refreshTags();
    refreshPlaylists();
    refreshQueue();
    refreshSelectedPlaylist();
    refreshCurrent();
}

void Backend::refreshArticles() {
    m_articles = m_library->articles(m_filter, m_selectedTag, m_search);
    emit articlesChanged();
}

void Backend::refreshPlaylists() {
    m_playlists = m_library->playlists();
    emit playlistsChanged();
}

void Backend::refreshQueue() {
    m_queue = m_library->queue();
    emit queueChanged();
}

void Backend::refreshTags() {
    m_tags = m_library->tags();
    emit tagsChanged();
}

void Backend::refreshCurrent() {
    if (m_currentId.isEmpty())
        return;
    const QVariantMap article = m_library->article(m_currentId);
    if (article.isEmpty()) {
        m_current.clear();
        m_currentId.clear();
        m_tts->stop();
        m_tts->setSentences({});
        emit currentArticleChanged();
        return;
    }
    m_current = article;
    emit currentArticleChanged();
}

void Backend::refreshSelectedPlaylist() {
    if (m_selectedPlaylistId.isEmpty()) {
        m_selectedPlaylistItems.clear();
        emit selectedPlaylistItemsChanged();
        return;
    }
    m_selectedPlaylistItems = m_library->playlistItems(m_selectedPlaylistId);
    emit selectedPlaylistItemsChanged();
}

void Backend::addUrl(const QString &url) {
    m_autoplayAfterExtract = false;
    m_openAfterExtract = false;
    m_error.clear();
    emit errorChanged();
    setStatus(tr("Fetching %1…").arg(QUrl(url).host().isEmpty() ? url : QUrl(url).host()));
    m_extractor->extract(url);
}

void Backend::openUrl(const QString &url, bool autoplay) {
    m_autoplayAfterExtract = autoplay;
    m_openAfterExtract = true;
    m_error.clear();
    emit errorChanged();
    setStatus(tr("Fetching %1…").arg(QUrl(url).host().isEmpty() ? url : QUrl(url).host()));
    m_extractor->extract(url);
}

void Backend::extracted(const QVariantMap &article) {
    QString id;
    QString error;
    if (!m_library->saveArticle(article, &id, &error)) {
        setError(error.isEmpty() ? tr("Could not save this page.") : error);
        return;
    }
    m_error.clear();
    emit errorChanged();
    if (!m_library->addToQueue(id)) {
        setError(tr("The page was saved, but could not be added to the queue."));
        return;
    }
    refreshAll();
    setStatus(tr("Added to queue"));
    emit toast(tr("Added to queue"));
    if (m_openAfterExtract || !hasArticle())
        openArticle(id, m_autoplayAfterExtract);
    m_autoplayAfterExtract = false;
    m_openAfterExtract = false;
}

void Backend::extractionFailed(const QString &error) {
    setError(error);
}

void Backend::openArticle(const QString &id, bool autoplay) {
    if (id.isEmpty())
        return;
    setError({});
    const QVariantMap article = m_library->article(id);
    if (article.isEmpty()) {
        setError(tr("That page is no longer in the library."));
        return;
    }
    if (!article.value("hasContent").toBool()) {
        setError(tr("That page has no saved article text."));
        return;
    }
    const int readState = article.value("readState").toInt();
    m_tts->stop();
    m_fromQueue = false;
    m_currentId = id;
    m_library->setSetting("last_article", id);
    m_current = article;
    m_startSentence = readState == 2 ? 0 : article.value("lastSentence").toInt();
    m_autoplayAfterWrap = autoplay;
    m_wrapping = true;
    ++m_renderToken;
    if (readState == 0) {
        m_library->setReadState(id, 1);
        m_current["readState"] = 1;
    }
    emit currentArticleChanged();

    const QVariantMap payload = readerPayload(article);
    if (payload.isEmpty()) {
        m_wrapping = false;
        setError(tr("Could not build the reader view."));
        return;
    }
    emit readerRender(payload);
    setStatus(tr("Ready"));
}

void Backend::restoreLastArticle() {
    if (hasArticle())
        return;
    const QString id = m_library->setting("last_article");
    if (id.isEmpty())
        return;
    const QVariantMap article = m_library->article(id);
    if (article.isEmpty() || !article.value("hasContent").toBool())
        return;
    openArticle(id, false);
    // Starting from a queued article should keep advancing through the queue.
    const QVariantList currentQueue = m_library->queue();
    for (int i = 0; i < currentQueue.size(); ++i) {
        if (currentQueue.at(i).toMap().value("id").toString() == id) {
            m_queueIndex = i;
            m_fromQueue = true;
            break;
        }
    }
}

QStringList Backend::sentenceTexts(const QVariantList &sentences, QList<int> *blocks) {
    QStringList texts;
    for (const QVariant &entry : sentences) {
        const QVariantMap map = entry.canConvert<QVariantMap>() ? entry.toMap() : QVariantMap();
        const QString text = (map.isEmpty() ? entry.toString() : map.value("text").toString()).trimmed();
        if (text.isEmpty())
            continue;
        texts << text;
        if (blocks)
            blocks->append(map.value("block", -1).toInt());
    }
    return texts;
}

void Backend::sentencesReady(int token, const QVariantList &sentences) {
    // A render that finishes after a newer article was opened is stale.
    if (!m_wrapping || token != m_renderToken)
        return;
    m_wrapping = false;
    QList<int> blocks;
    const QStringList texts = sentenceTexts(sentences, &blocks);
    if (texts.isEmpty()) {
        setError(tr("That page has no readable sentences."));
        return;
    }
    m_tts->setSentences(texts, blocks, m_startSentence);
    if (m_autoplayAfterWrap) {
        m_autoplayAfterWrap = false;
        m_tts->play();
    } else {
        emit highlightSentence(m_tts->current());
    }
}

void Backend::handleReaderUrl(const QString &url) {
    const QUrl parsed(url);
    if (parsed.host() != "sift.invalid")
        return;
    const QString path = parsed.path();
    if (path.startsWith("/sentence/")) {
        bool ok = false;
        const int index = path.mid(QStringLiteral("/sentence/").size()).toInt(&ok);
        if (ok)
            playFromSentence(index);
        return;
    }
    if (path.startsWith("/link/")) {
        const QString target = QUrl::fromPercentEncoding(path.mid(QStringLiteral("/link/").size()).toUtf8());
        const bool newArticle = QUrlQuery(parsed).queryItemValue("new") == "1";
        if (newArticle)
            openUrl(target, true);
        else
            QDesktopServices::openUrl(QUrl(target));
    }
}

void Backend::openExternal(const QString &url) {
    QDesktopServices::openUrl(QUrl(url));
}

void Backend::openOriginal() {
    const QString url = m_current.value("url").toString();
    if (!url.isEmpty())
        QDesktopServices::openUrl(QUrl(url));
}

void Backend::playPause() { m_tts->toggle(); }
void Backend::nextSentence() { m_tts->next(); }
void Backend::previousSentence() { m_tts->previous(); }
void Backend::playFromSentence(int index) { m_tts->playFrom(index); }
void Backend::seekSentence(int index) {
    if (m_tts->playing())
        m_tts->playFrom(index);
    else
        m_tts->setStartSentence(index);
}

void Backend::setSpeed(double speed) {
    m_tts->setSpeed(speed);
    m_library->setSetting("speed", QString::number(m_tts->speed(), 'f', 2));
}

void Backend::setVoice(const QString &voice) {
    m_tts->setVoice(voice);
    m_library->setSetting("voice", m_tts->voice());
}

void Backend::setFilter(const QString &filter) {
    if (filter == m_filter)
        return;
    m_filter = filter;
    emit filterChanged();
    refreshArticles();
}

void Backend::setSearch(const QString &search) {
    if (search == m_search)
        return;
    m_search = search;
    emit searchChanged();
    refreshArticles();
}

void Backend::setTag(const QString &tag) {
    if (tag == m_selectedTag)
        return;
    m_selectedTag = tag;
    emit selectedTagChanged();
    refreshArticles();
}

void Backend::setSection(const QString &section) {
    if (section == m_section)
        return;
    m_section = section;
    emit sectionChanged();
}

void Backend::setFontSize(int size) {
    const int clean = qBound(14, size, 32);
    if (clean == m_fontSize)
        return;
    m_fontSize = clean;
    m_library->setSetting("font_size", QString::number(clean));
    emit fontSizeChanged();
    emit settingsChanged();
}

void Backend::setReaderFont(const QString &family) {
    const QString clean = family.trimmed();
    if (clean.isEmpty() || clean == m_readerFont)
        return;
    m_readerFont = clean;
    m_library->setSetting("reader_font", clean);
    emit readerFontChanged();
    emit settingsChanged();
}

void Backend::setAppearance(const QString &appearance) {
    if (!m_theme)
        return;
    m_theme->setAppearance(appearance);
    m_library->setSetting("appearance", m_theme->appearance());
}

void Backend::toggleCurrentRead() {
    if (m_currentId.isEmpty())
        return;
    const int next = m_current.value("readState").toInt() == 2 ? 0 : 2;
    m_library->setReadState(m_currentId, next);
    m_current["readState"] = next;
    emit currentArticleChanged();
    setStatus(next == 2 ? tr("Marked read") : tr("Marked unread"));
}

void Backend::markCurrentRead() {
    if (m_currentId.isEmpty())
        return;
    m_library->setReadState(m_currentId, 2);
    m_current["readState"] = 2;
    emit currentArticleChanged();
}

void Backend::setCurrentTags(const QString &csv) {
    if (m_currentId.isEmpty())
        return;
    QStringList tags;
    const auto parts = csv.split(',', Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        const QString tag = part.trimmed();
        if (!tag.isEmpty() && !tags.contains(tag, Qt::CaseInsensitive))
            tags << tag;
    }
    m_library->setTags(m_currentId, tags);
    refreshCurrent();
    refreshTags();
    refreshArticles();
    setStatus(tags.isEmpty() ? tr("Tags cleared") : tr("Tags saved"));
}

void Backend::deleteCurrent() {
    if (m_currentId.isEmpty())
        return;
    const QString title = m_current.value("title").toString();
    const QString id = m_currentId;
    m_currentId.clear();
    m_current.clear();
    m_fromQueue = false;
    m_tts->stop();
    m_tts->setSentences({});
    m_library->deleteArticle(id);
    emit currentArticleChanged();
    refreshAll();
    setStatus(tr("Deleted %1").arg(title));
}

void Backend::copyMarkdown() {
    if (m_currentId.isEmpty())
        return;
    const QString markdown = m_library->articleMarkdown(m_currentId);
    if (markdown.isEmpty()) {
        setError(tr("The Markdown export is missing."));
        return;
    }
    QGuiApplication::clipboard()->setText(markdown);
    setStatus(tr("Markdown copied"));
    emit toast(tr("Markdown copied"));
}

void Backend::refresh() {
    refreshAll();
    m_kokoro->checkHealth();
    m_kokoro->refreshVoices();
}

QString Backend::createPlaylist(const QString &name) {
    const QString id = m_library->createPlaylist(name);
    if (id.isEmpty()) {
        setError(tr("Give the playlist a name."));
        return {};
    }
    refreshPlaylists();
    selectPlaylist(id);
    setStatus(tr("Playlist created"));
    return id;
}

void Backend::deletePlaylist(const QString &id) {
    if (m_library->deletePlaylist(id)) {
        if (m_selectedPlaylistId == id) {
            m_selectedPlaylistId.clear();
            emit selectedPlaylistChanged();
        }
        refreshPlaylists();
        refreshSelectedPlaylist();
        setStatus(tr("Playlist deleted"));
    }
}

void Backend::renamePlaylist(const QString &id, const QString &name) {
    if (m_library->renamePlaylist(id, name)) {
        refreshPlaylists();
        setStatus(tr("Playlist renamed"));
    }
}

void Backend::selectPlaylist(const QString &id) {
    if (id == m_selectedPlaylistId)
        return;
    m_selectedPlaylistId = id;
    emit selectedPlaylistChanged();
    refreshSelectedPlaylist();
}

void Backend::addToPlaylist(const QString &playlistId, const QString &articleId) {
    if (m_library->addToPlaylist(playlistId, articleId)) {
        refreshPlaylists();
        refreshSelectedPlaylist();
        setStatus(tr("Added to playlist"));
    }
}

void Backend::addCurrentToPlaylist(const QString &playlistId) {
    if (!m_currentId.isEmpty())
        addToPlaylist(playlistId, m_currentId);
}

void Backend::removeFromPlaylist(const QString &playlistId, const QString &articleId) {
    if (m_library->removeFromPlaylist(playlistId, articleId)) {
        refreshPlaylists();
        refreshSelectedPlaylist();
        setStatus(tr("Removed from playlist"));
    }
}

void Backend::movePlaylistItem(const QString &playlistId, int from, int to) {
    if (m_library->movePlaylistItem(playlistId, from, to)) {
        refreshSelectedPlaylist();
        refreshPlaylists();
    }
}

void Backend::playPlaylist(const QString &id) {
    const QVariantList items = m_library->playlistItems(id);
    QStringList ids;
    for (const QVariant &item : items)
        ids << item.toMap().value("id").toString();
    if (ids.isEmpty()) {
        setStatus(tr("That playlist is empty"));
        return;
    }
    m_library->setQueue(ids);
    refreshQueue();
    playQueueAt(0);
}

void Backend::addToQueue(const QString &articleId) {
    if (m_library->addToQueue(articleId)) {
        refreshQueue();
        setStatus(tr("Added to queue"));
    }
}

void Backend::addCurrentToQueue() {
    if (!m_currentId.isEmpty())
        addToQueue(m_currentId);
}

void Backend::removeFromQueue(const QString &articleId) {
    m_library->removeFromQueue(articleId);
    refreshQueue();
}

void Backend::clearQueue() {
    m_fromQueue = false;
    m_queueIndex = -1;
    m_library->clearQueue();
    refreshQueue();
    setStatus(tr("Queue cleared"));
}

void Backend::moveQueueItem(int from, int to) {
    if (m_library->moveQueueItem(from, to))
        refreshQueue();
}

void Backend::playQueueAt(int index) {
    const QVariantList currentQueue = m_library->queue();
    if (index < 0 || index >= currentQueue.size())
        return;
    m_queue = currentQueue;
    m_queueIndex = index;
    openArticle(currentQueue.at(index).toMap().value("id").toString(), true);
    m_fromQueue = true;
}

void Backend::articleFinished() {
    if (!m_currentId.isEmpty()) {
        m_library->setReadState(m_currentId, 2);
        m_current["readState"] = 2;
        emit currentArticleChanged();
    }
    const QVariantList currentQueue = m_library->queue();
    int index = -1;
    for (int i = 0; i < currentQueue.size(); ++i) {
        if (currentQueue.at(i).toMap().value("id").toString() == m_currentId) {
            index = i;
            break;
        }
    }
    if (index < 0)
        index = m_queueIndex;
    m_queueIndex = index;
    const bool autoAdvance = m_library->setting("auto_advance", "1") == "1";
    if (m_fromQueue && autoAdvance && index + 1 < currentQueue.size()) {
        playQueueAt(index + 1);
        return;
    }
    m_fromQueue = false;
    setStatus(tr("Finished"));
    emit toast(tr("Finished reading"));
}

QVariantMap Backend::settings() const {
    QVariantMap values;
    values["kokoroUrl"] = m_kokoro->endpoint();
    values["saveDir"] = m_library->saveDirectory();
    values["voice"] = m_tts->voice();
    values["speed"] = m_tts->speed();
    values["cacheLimitMb"] = m_library->setting("cache_limit_mb", "1024").toInt();
    values["fontSize"] = m_fontSize;
    values["readerFont"] = m_readerFont;
    values["autoAdvance"] = m_library->setting("auto_advance", "1") == "1";
    return values;
}

void Backend::applySettings(const QVariantMap &settings) {
    if (settings.contains("kokoroUrl"))
        m_library->setSetting("kokoro_url", settings.value("kokoroUrl").toString().trimmed());
    if (settings.contains("saveDir"))
        m_library->setSetting("save_dir", settings.value("saveDir").toString().trimmed());
    if (settings.contains("voice")) {
        m_tts->setVoice(settings.value("voice").toString());
        m_library->setSetting("voice", m_tts->voice());
    }
    if (settings.contains("speed")) {
        m_tts->setSpeed(settings.value("speed").toDouble());
        m_library->setSetting("speed", QString::number(m_tts->speed(), 'f', 2));
    }
    if (settings.contains("cacheLimitMb")) {
        const int limit = qMax(64, settings.value("cacheLimitMb").toInt());
        m_library->setSetting("cache_limit_mb", QString::number(limit));
        m_tts->setCacheLimitMb(limit);
    }
    if (settings.contains("fontSize"))
        setFontSize(settings.value("fontSize").toInt());
    if (settings.contains("readerFont"))
        setReaderFont(settings.value("readerFont").toString());
    if (settings.contains("autoAdvance"))
        m_library->setSetting("auto_advance", settings.value("autoAdvance").toBool() ? "1" : "0");
    if (settings.contains("kokoroUrl")) {
        m_kokoro->setEndpoint(settings.value("kokoroUrl").toString());
        m_kokoro->checkHealth();
        m_kokoro->refreshVoices();
    }
    emit settingsChanged();
    setStatus(tr("Settings saved"));
}

void Backend::checkKokoro() { m_kokoro->checkHealth(); }
void Backend::refreshVoices() { m_kokoro->refreshVoices(); }
void Backend::quit() { QCoreApplication::quit(); }

QString Backend::readerDirection(const QString &language) {
    const QString code = language.toLower();
    if (code.startsWith("ar") || code.startsWith("he") || code.startsWith("fa") || code.startsWith("ur"))
        return "rtl";
    return "ltr";
}

QString Backend::sanitizeHtml(const QString &input) {
    QString html = input;
    const auto options = QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption;
    html.remove(QRegularExpression("<script\\b[^>]*>.*?</script\\s*>", options));
    html.remove(QRegularExpression("<style\\b[^>]*>.*?</style\\s*>", options));
    html.remove(QRegularExpression("<iframe\\b[^>]*>.*?</iframe\\s*>", options));
    html.remove(QRegularExpression("<object\\b[^>]*>.*?</object\\s*>", options));
    html.remove(QRegularExpression("<form\\b[^>]*>.*?</form\\s*>", options));
    html.remove(QRegularExpression("<embed\\b[^>]*>", QRegularExpression::CaseInsensitiveOption));
    html.remove(QRegularExpression("<link\\b[^>]*>", QRegularExpression::CaseInsensitiveOption));
    html.remove(QRegularExpression("<meta\\b[^>]*>", QRegularExpression::CaseInsensitiveOption));
    html.remove(QRegularExpression("\\son\\w+\\s*=\\s*(?:\"[^\"]*\"|'[^']*'|[^\\s>]+)",
                                   QRegularExpression::CaseInsensitiveOption));
    html.remove(QRegularExpression("\\ssrcdoc\\s*=\\s*(?:\"[^\"]*\"|'[^']*'|[^\\s>]+)",
                                   QRegularExpression::CaseInsensitiveOption));
    html.remove(QRegularExpression("\\sautoplay\\b", QRegularExpression::CaseInsensitiveOption));
    html.replace(QRegularExpression("javascript:", QRegularExpression::CaseInsensitiveOption), "");
    return html;
}

QString Backend::formatPublished(const QString &published) {
    const QString value = published.trimmed();
    if (value.isEmpty())
        return {};
    QDate date = QDateTime::fromString(value, Qt::ISODate).date();
    if (!date.isValid())
        date = QDate::fromString(value.left(10), Qt::ISODate);
    if (!date.isValid())
        return value;
    // The locale's long format without the weekday reads like a byline.
    QString format = QLocale().dateFormat(QLocale::LongFormat);
    format.remove(QRegularExpression("dddd[,.]?\\s*"));
    return QLocale().toString(date, format.trimmed());
}

QVariantMap Backend::readerPayload(const QVariantMap &article) const {
    const QString id = article.value("id").toString();
    const QString content = sanitizeHtml(m_library->articleHtml(id));
    if (content.trimmed().isEmpty())
        return {};

    const QString language = article.value("language").toString().isEmpty()
        ? QStringLiteral("en") : article.value("language").toString();
    QString safeLanguage = language;
    safeLanguage.remove(QRegularExpression("[^A-Za-z0-9-]"));

    QVariantList meta;
    auto addMeta = [&meta](const QString &text, const QString &kind) {
        if (!text.trimmed().isEmpty())
            meta << QVariantMap{{"text", text.trimmed()}, {"kind", kind}};
    };
    addMeta(article.value("author").toString(), "author");
    addMeta(formatPublished(article.value("published").toString()), "date");
    addMeta(article.value("domain").toString(), "domain");
    const int words = article.value("wordCount").toInt();
    if (words > 0)
        addMeta(tr("%1 min read").arg(qMax(1, (words + 219) / 220)), "time");

    return QVariantMap{
        {"token", m_renderToken},
        {"id", id},
        {"title", article.value("title").toString()},
        {"meta", meta},
        {"lang", safeLanguage.isEmpty() ? QStringLiteral("en") : safeLanguage},
        {"dir", readerDirection(language)},
        {"baseUrl", article.value("url").toString()},
        {"html", content},
    };
}

QString Backend::readerShell() const {
    QFile cssFile(":/reader.css");
    QFile jsFile(":/reader.js");
    if (!cssFile.open(QIODevice::ReadOnly) || !jsFile.open(QIODevice::ReadOnly))
        return {};
    const QString css = QString::fromUtf8(cssFile.readAll());
    const QString js = QString::fromUtf8(jsFile.readAll());

    const QString background = m_theme ? m_theme->background() : "#1a1a1a";
    const QString foreground = m_theme ? m_theme->foreground() : "#f0f0f0";
    const QString muted = m_theme ? m_theme->muted() : "#888888";
    const QString accent = m_theme ? m_theme->accent() : "#FFD60A";
    const QString selection = m_theme ? m_theme->selection() : "#444444";
    const QString panel = m_theme ? m_theme->panel() : "#222222";
    const QString border = m_theme ? m_theme->borderColor() : "#333333";
    const int radius = m_theme ? m_theme->radius() : 0;
    const bool dark = m_theme ? m_theme->dark() : true;

    QString cssFont = m_readerFont;
    cssFont.remove(QRegularExpression("[\\\";{}]"));
    const QString bodyFont = QStringLiteral("\"%1\", system-ui, sans-serif").arg(cssFont);
    const QString variables = QStringLiteral(
        ":root{--sift-bg:%1;--sift-fg:%2;--sift-muted:%3;--sift-accent:%4;"
        "--sift-selection:%5;--sift-panel:%6;--sift-border:%7;--sift-radius:%8px;"
        "--sift-font-size:%9px;--sift-body-font:%10;}")
        .arg(background, foreground, muted, accent, selection, panel, border)
        .arg(radius)
        .arg(m_fontSize)
        .arg(bodyFont);

    // Only the shell's own script carries the nonce. Anything inline that
    // slips through sanitising, such as an event handler attribute, cannot
    // run. Qt's runJavaScript is not subject to the page's policy.
    const QString nonce = QUuid::createUuid().toString(QUuid::Id128);
    return QStringLiteral(
        "<!doctype html><html lang=\"en\" dir=\"ltr\" class=\"%1\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
        "<meta name=\"referrer\" content=\"no-referrer\">"
        "<meta http-equiv=\"Content-Security-Policy\" content=\"default-src 'none'; "
        "img-src http: https: data:; media-src http: https: data:; style-src 'unsafe-inline'; "
        "script-src 'nonce-%2'; font-src data:; object-src 'none'; frame-src 'none'; "
        "form-action 'none'; connect-src 'none';\">"
        "<style>%4</style><style>%3</style></head>"
        "<body><main id=\"sift-wrap\"><header id=\"sift-header\">"
        "<h1 id=\"sift-title\"></h1><div id=\"sift-meta\"></div></header>"
        "<article id=\"sift-content\"></article></main>"
        "<script nonce=\"%2\">%5</script></body></html>")
        .arg(dark ? "theme-dark" : "theme-light", nonce, variables, css, js);
}
