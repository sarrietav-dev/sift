#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSqlDatabase>
#include <QTcpServer>
#include <QTcpSocket>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

#include "kokoro.h"
#include "library.h"
#include "speech.h"
#include "theme.h"
#include "translator.h"
#include "ttsengine.h"

namespace {
void writeFile(const QString &path, const QByteArray &contents) {
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(contents);
}

QVariantMap articleData(const QString &url, const QString &title, const QString &body) {
    QVariantMap data;
    data["url"] = url;
    data["canonicalUrl"] = url;
    data["title"] = title;
    data["author"] = "Test Author";
    data["published"] = "2025-01-02";
    data["domain"] = "example.com";
    data["language"] = "en";
    data["wordCount"] = body.split(' ', Qt::SkipEmptyParts).size();
    data["contentHtml"] = "<p>" + body + "</p>";
    data["markdown"] = body;
    data["contentText"] = body;
    data["excerpt"] = body.left(200);
    return data;
}

bool librarySave(Library &library, const QString &url, const QString &title,
                 const QString &body, QString *id, QString *error) {
    return library.saveArticle(articleData(url, title, body), id, error);
}

bool setTimestamps(const QString &connection, const QString &dbPath,
                   const QString &queueStamp,
                   const QList<QPair<QString, QString>> &played) {
    bool ok = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connection);
        db.setDatabaseName(dbPath);
        ok = db.open();
        if (ok) {
            QSqlQuery setting(db);
            setting.prepare("INSERT INTO settings(key, value) VALUES ('queue_updated_at', ?) "
                            "ON CONFLICT(key) DO UPDATE SET value = excluded.value");
            setting.addBindValue(queueStamp);
            ok = setting.exec();
            for (const auto &entry : played) {
                QSqlQuery update(db);
                update.prepare("UPDATE articles SET last_played_at = ?, updated_at = ? WHERE id = ?");
                update.addBindValue(entry.second);
                update.addBindValue(entry.second);
                update.addBindValue(entry.first);
                if (!update.exec())
                    ok = false;
            }
        }
    }
    QSqlDatabase::removeDatabase(connection);
    return ok;
}

// A stand-in for Kokoro-FastAPI: answers /v1/audio/speech with chunked PCM,
// split mid-sample the way a real chunked body may be, and records what was
// asked and how many requests were open at once. Texts listed in `hold` get
// no answer, so a test can watch a request being dropped.
class FakeKokoro : public QObject {
    Q_OBJECT

public:
    FakeKokoro() {
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = m_server.nextPendingConnection()) {
                ++open;
                maxOpen = qMax(maxOpen, open);
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] { read(socket); });
                connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
                    --open;
                    if (socket->property("held").toBool())
                        dropped << socket->property("text").toString();
                    socket->deleteLater();
                });
            }
        });
        m_server.listen(QHostAddress::LocalHost);
    }

    QString endpoint() const { return QString("http://127.0.0.1:%1").arg(m_server.serverPort()); }

    QStringList texts;
    QStringList dropped;
    QSet<QString> hold;
    int open = 0;
    int maxOpen = 0;

private:
    void read(QTcpSocket *socket) {
        QByteArray data = socket->property("buffer").toByteArray() + socket->readAll();
        socket->setProperty("buffer", data);
        const qsizetype headerEnd = data.indexOf("\r\n\r\n");
        if (headerEnd < 0)
            return;
        const QByteArray headers = data.left(headerEnd).toLower();
        const qsizetype lengthAt = headers.indexOf("content-length:");
        const int length = lengthAt < 0 ? 0 : headers.mid(lengthAt + 15, headers.indexOf("\r\n", lengthAt) - lengthAt - 15).trimmed().toInt();
        if (data.size() < headerEnd + 4 + length)
            return;
        const QJsonObject body = QJsonDocument::fromJson(data.mid(headerEnd + 4, length)).object();
        const QString text = body.value("input").toString();
        texts << text;
        socket->setProperty("text", text);
        if (hold.contains(text)) {
            socket->setProperty("held", true);
            return;
        }
        QByteArray pcm;
        for (qint16 sample : {1, 2, 3, 4})
            pcm.append(reinterpret_cast<const char *>(&sample), 2);
        QByteArray reply = "HTTP/1.1 200 OK\r\nContent-Type: audio/pcm\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n";
        for (const QByteArray &part : {pcm.left(3), pcm.mid(3)})
            reply += QByteArray::number(part.size(), 16) + "\r\n" + part + "\r\n";
        reply += "0\r\n\r\n";
        socket->write(reply);
        socket->disconnectFromHost();
    }

    QTcpServer m_server;
};

class SiftTests : public QObject {
    Q_OBJECT

private slots:
    void themeFallsBack() {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QString("black"));
        QCOMPARE(theme.dark(), true);
        QVERIFY(theme.panel() != theme.foreground());
        QVERIFY(theme.borderColor() != theme.foreground());
    }

    void themeReadsOmarchyColors() {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        writeFile(dir.path() + "/theme/colors.toml",
                  "mode = \"light\"\n"
                  "accent = \"#205EA6\"\n"
                  "background = \"#FFFCF0\"\n"
                  "foreground = \"#100F0F\"\n"
                  "muted = \"#878580\"\n"
                  "selection = \"#CECDC3\"\n"
                  "dark_background = \"#f2efe4\"\n"
                  "darker_background = \"#e5e2d8\"\n"
                  "lighter_background = \"#E6E4D9\"\n"
                  "red = \"#D14D41\"\n");
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#205ea6"));
        QCOMPARE(theme.dark(), false);
        QCOMPARE(theme.background(), QString("#fffcf0"));
        QCOMPARE(theme.panel(), QString("#f2efe4"));
        QCOMPARE(theme.borderColor(), QString("#e5e2d8"));
        QCOMPARE(theme.red(), QString("#d14d41"));
        QCOMPARE(theme.accentForeground(), QString("white"));
    }

    void themeAppearanceOverridesOmarchy() {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        writeFile(dir.path() + "/theme/colors.toml",
                  "mode = \"dark\"\naccent = \"#97786d\"\nbackground = \"#27241f\"\nforeground = \"#e1d5c2\"\n");
        Theme theme(dir.path());
        QCOMPARE(theme.background(), QString("#27241f"));
        theme.setAppearance("light");
        QCOMPARE(theme.dark(), false);
        QCOMPARE(theme.background(), Theme::builtIn(false).background);
        QCOMPARE(theme.target().value("background").toString(), Theme::builtIn(false).background);

        // A crossfade passes through blended colours and ends on the target.
        theme.setAnimated(true);
        QSignalSpy steps(&theme, &Theme::changed);
        theme.setAppearance("omarchy");
        QCOMPARE(theme.target().value("background").toString(), QString("#27241f"));
        QTRY_COMPARE(theme.background(), QString("#27241f"));
        QVERIFY(steps.size() > 2);

        // Mid-switch, Omarchy removes the theme folder; the colours hold.
        QFile::remove(dir.path() + "/theme/colors.toml");
        QTest::qWait(200);
        QCOMPARE(theme.background(), QString("#27241f"));

        theme.setAppearance("bogus");
        QCOMPARE(theme.appearance(), QString("omarchy"));
    }

    void translatorFollowsLanguage() {
        QTemporaryDir dir;
        writeFile(dir.path() + "/language", "pt-BR\n");
        writeFile(dir.path() + "/pt.json", R"({"Save": "Guardar", "Open": "Abrir"})");
        writeFile(dir.path() + "/pt-BR.json", R"({"Save": "Salvar"})");
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("pt-BR"));
        QCOMPARE(translator.translate("Main", "Save"), QString("Salvar"));
        QCOMPARE(translator.translate("Main", "Open"), QString("Abrir"));
        QVERIFY(translator.translate("Main", "Quit").isNull());
    }

    void translatorPicksPluralForms() {
        QTemporaryDir dir;
        writeFile(dir.path() + "/language", "ru\n");
        writeFile(dir.path() + "/en.json", R"({"%n clips": {"one": "%n clip", "other": "%n clips"}})");
        writeFile(dir.path() + "/ru.json", R"({"%n clips": {"one": "%n клип", "few": "%n клипа", "many": "%n клипов", "other": "%n клипа"}})");
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 21), QString("%n клип"));
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 22), QString("%n клипа"));
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 25), QString("%n клипов"));
    }

    void librarySavesMarkdownAndSearches() {
        QTemporaryDir dir;
        Library library(dir.path());
        QString id, error;
        QVERIFY2(library.saveArticle(articleData("https://example.com/a", "A useful article",
                                                 "The quick brown fox jumps over the lazy dog."),
                                     &id, &error), qPrintable(error));
        QVERIFY(!id.isEmpty());

        const QVariantMap article = library.article(id);
        QCOMPARE(article.value("title").toString(), QString("A useful article"));
        QCOMPARE(article.value("readState").toInt(), 0);
        const QString path = article.value("markdownPath").toString();
        QVERIFY(QFile::exists(path));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString markdown = QString::fromUtf8(file.readAll());
        QVERIFY(markdown.contains("title: \"A useful article\""));
        QVERIFY(markdown.contains("source: \"https://example.com/a\""));
        QVERIFY(markdown.contains("# A useful article"));

        QCOMPARE(library.articles("all", {}, "brown").size(), 1);
        QCOMPARE(library.articles("all", {}, "missing").size(), 0);
    }

    void libraryPreservesProgressAndTagsOnReclip() {
        QTemporaryDir dir;
        Library library(dir.path());
        QString id, error;
        QVERIFY(library.saveArticle(articleData("https://example.com/a", "First title", "One two three."),
                                    &id, &error));
        library.setReadState(id, 1);
        library.saveProgress(id, 7);
        library.setTags(id, {"research", "systems"});
        const QString taggedPath = library.article(id).value("markdownPath").toString();
        QFile taggedFile(taggedPath);
        QVERIFY(taggedFile.open(QIODevice::ReadOnly));
        QVERIFY(QString::fromUtf8(taggedFile.readAll()).contains("tags:\n  - \"research\"\n  - \"systems\""));
        taggedFile.close();

        QString secondId;
        QVERIFY(library.saveArticle(articleData("https://example.com/a", "Second title", "Four five six."),
                                    &secondId, &error));
        QCOMPARE(secondId, id);
        const QVariantMap article = library.article(id);
        QCOMPARE(article.value("title").toString(), QString("Second title"));
        QCOMPARE(article.value("readState").toInt(), 1);
        QCOMPARE(article.value("lastSentence").toInt(), 7);
        QCOMPARE(article.value("tags").toStringList(), QStringList({"research", "systems"}));
    }

    void prefetchReadsThroughTheNextParagraph() {
        const QList<int> blocks{0, 0, 0, 1, 1, 1, 1, 2, 2, 2};
        // From the start of paragraph 0: finish it and read all of paragraph 1.
        QCOMPARE(TtsEngine::prefetchIndices(blocks, 0), QList<int>({1, 2, 3, 4, 5, 6}));
        // From inside paragraph 1: finish it, then the whole of paragraph 2.
        QCOMPARE(TtsEngine::prefetchIndices(blocks, 3), QList<int>({4, 5, 6, 7, 8, 9}));
        // From the last sentence of paragraph 1: only paragraph 2 follows.
        QCOMPARE(TtsEngine::prefetchIndices(blocks, 6), QList<int>({7, 8, 9}));
        // Without paragraph information, fall back to a short sliding window.
        QCOMPARE(TtsEngine::prefetchIndices(QList<int>(10, -1), 0).size(), 8);
    }

    void prefetchLooksPastOneSentenceBlocks() {
        // A heading and a caption are blocks of their own. Stopping at the
        // heading left the following paragraph to synthesize while the heading
        // played, which is short enough to hear the gap.
        const QList<int> heading{0, 0, 0, 1, 2, 2, 2, 2, 2, 3, 3};
        QCOMPARE(TtsEngine::prefetchIndices(heading, 2), QList<int>({3, 4, 5, 6, 7, 8}));
        // Each list item is a block too: a bullet list should keep several
        // items queued instead of only the next one.
        const QList<int> bullets{0, 0, 1, 2, 3, 4, 5, 6, 7, 8};
        QCOMPARE(TtsEngine::prefetchIndices(bullets, 0), QList<int>({1, 2, 3, 4, 5, 6}));
        // A paragraph with enough sentences left still gets the whole next
        // one, but nothing beyond it.
        const QList<int> paragraphs{0, 0, 0, 0, 0, 0, 0, 1, 1, 2, 2};
        QCOMPARE(TtsEngine::prefetchIndices(paragraphs, 0), QList<int>({1, 2, 3, 4, 5, 6, 7, 8}));
    }

    void numeralsAfterALabelAreSpokenAsNumbers() {
        using speech::spokenNumerals;
        QCOMPARE(spokenNumerals("Part I of this book"), QString("Part one of this book"));
        QCOMPARE(spokenNumerals("Part II, “Auditing,” then"), QString("Part two, “Auditing,” then"));
        QCOMPARE(spokenNumerals("after World War II ended"), QString("after World War two ended"));
        QCOMPARE(spokenNumerals("Chapter IV: the next step"), QString("Chapter four: the next step"));
        QCOMPARE(spokenNumerals("(Part I)"), QString("(Part one)"));
        QCOMPARE(spokenNumerals("PART III"), QString("PART three"));
        QCOMPARE(spokenNumerals("Parts I and II"), QString("Parts one and two"));
        // Without a label a numeral may be a name or a letter.
        QCOMPARE(spokenNumerals("Henry VIII and I went"), QString("Henry VIII and I went"));
        // "I" after a lowercase label is the pronoun.
        QCOMPARE(spokenNumerals("the article I wrote"), QString("the article I wrote"));
        QCOMPARE(spokenNumerals("the war I remember"), QString("the war I remember"));
        // Not a canonical numeral, or one past what has a word.
        QCOMPARE(spokenNumerals("Type IIII"), QString("Type IIII"));
        QCOMPARE(spokenNumerals("Class C"), QString("Class C"));
        QCOMPARE(TtsEngine::cleanForSpeech("Part  II [12] begins."), QString("Part two begins."));
    }

    void speechIsAskedForOneSentenceAtATime() {
        QTemporaryDir cache;
        qputenv("XDG_CACHE_HOME", cache.path().toUtf8());
        FakeKokoro server;
        Kokoro kokoro;
        kokoro.setEndpoint(server.endpoint());
        TtsEngine engine(&kokoro);

        // Opening an article asks for the title and then the first sentence,
        // never both at once, and nothing further until playback starts.
        engine.setSentences({"One.", "Two.", "Three."}, {0, 0, 1}, 0, "A title.");
        QTRY_COMPARE(server.texts, QStringList({"A title.", "One."}));
        QTRY_COMPARE(server.open, 0);
        QCOMPARE(server.maxOpen, 1);
        QTest::qWait(100);
        QCOMPARE(server.texts.size(), 2);

        // Both clips are cached as PCM; the same article opens without asking.
        QDirIterator files(cache.path(), QStringList{"*.pcm"}, QDir::Files, QDirIterator::Subdirectories);
        int cached = 0;
        while (files.hasNext()) {
            QCOMPARE(QFileInfo(files.next()).size(), 8);
            ++cached;
        }
        QCOMPARE(cached, 2);
        engine.setSentences({"One.", "Two.", "Three."}, {0, 0, 1}, 0, "A title.");
        QTest::qWait(100);
        QCOMPARE(server.texts.size(), 2);

        // Moving the start drops the request for a sentence no longer wanted.
        server.hold.insert("Two.");
        engine.setStartSentence(1);
        QTRY_COMPARE(server.texts.size(), 3);
        engine.setStartSentence(2);
        QTRY_COMPARE(server.dropped, QStringList({"Two."}));
        QTRY_COMPARE(server.texts.last(), QString("Three."));
        QCOMPARE(server.maxOpen, 1);
        qunsetenv("XDG_CACHE_HOME");
    }

    void announcementsNameTheAuthor() {
        QCOMPARE(TtsEngine::announcement("In a world of panic", "Dan Maccarone"),
                 QString("In a world of panic. By Dan Maccarone."));
        QCOMPARE(TtsEngine::announcement("  A  spaced   title ", " A Writer "),
                 QString("A spaced title. By A Writer."));
        // Without a byline, the title stands alone; without a title there is
        // nothing worth announcing.
        QCOMPARE(TtsEngine::announcement("Just a title", ""), QString("Just a title."));
        QCOMPARE(TtsEngine::announcement("", "A Writer"), QString());
    }

    void libraryPlaylistsAndQueue() {
        QTemporaryDir dir;
        Library library(dir.path());
        QString first, second, error;
        QVERIFY(library.saveArticle(articleData("https://example.com/1", "One", "First body."), &first, &error));
        QVERIFY(library.saveArticle(articleData("https://example.com/2", "Two", "Second body."), &second, &error));

        const QString playlist = library.createPlaylist("Later");
        QVERIFY(!playlist.isEmpty());
        QVERIFY(library.addToPlaylist(playlist, first));
        QVERIFY(library.addToPlaylist(playlist, second));
        QCOMPARE(library.playlistItems(playlist).size(), 2);
        QVERIFY(library.movePlaylistItem(playlist, 0, 1));
        QCOMPARE(library.playlistItems(playlist).at(0).toMap().value("id").toString(), second);
        QVERIFY(library.removeFromPlaylist(playlist, first));
        QCOMPARE(library.playlistItems(playlist).size(), 1);

        QVERIFY(library.addToQueue(first));
        QVERIFY(library.addToQueue(second));
        QCOMPARE(library.queue().size(), 2);
        QVERIFY(library.moveQueueItem(0, 1));
        QCOMPARE(library.queue().at(0).toMap().value("id").toString(), second);
        QVERIFY(library.removeFromQueue(second));
        QCOMPARE(library.queue().size(), 1);
        QCOMPARE(library.queue().at(0).toMap().value("id").toString(), first);
        QVERIFY(!library.article(second).isEmpty());
        library.clearQueue();
        QCOMPARE(library.queue().size(), 0);
        QVERIFY(!library.article(first).isEmpty());
        QCOMPARE(library.playlistItems(playlist).size(), 1);
    }

    void librarySortsQueue() {
        QTemporaryDir dir;
        Library library(dir.path());
        QString beta, alpha, gamma, error;
        QVariantMap data = articleData("https://example.com/b", "Beta", "Body.");
        data["author"] = "Ada";
        QVERIFY(library.saveArticle(data, &beta, &error));
        data = articleData("https://example.com/a", "alpha", "Body.");
        data["author"] = "Zed";
        QVERIFY(library.saveArticle(data, &alpha, &error));
        data = articleData("https://example.com/c", "Gamma", "Body.");
        data["author"] = "Ada";
        QVERIFY(library.saveArticle(data, &gamma, &error));

        // The clips happen in the same second, so set the dates by hand.
        const QString connection = "sift_sort_test";
        {
            QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connection);
            db.setDatabaseName(dir.path() + "/sift.db");
            QVERIFY(db.open());
            const QList<QPair<QString, QString>> dates{
                {beta, "2025-03-01T00:00:00Z"}, {alpha, "2025-01-01T00:00:00Z"},
                {gamma, "2025-02-01T00:00:00Z"}};
            for (const auto &entry : dates) {
                QSqlQuery update(db);
                update.prepare("UPDATE articles SET clipped_at = ? WHERE id = ?");
                update.addBindValue(entry.second);
                update.addBindValue(entry.first);
                QVERIFY(update.exec());
            }
        }
        QSqlDatabase::removeDatabase(connection);

        QVERIFY(library.addToQueue(beta));
        QVERIFY(library.addToQueue(gamma));
        QVERIFY(library.addToQueue(alpha));
        auto ids = [&library] {
            QStringList result;
            for (const QVariant &item : library.queue())
                result << item.toMap().value("id").toString();
            return result;
        };

        QVERIFY(library.sortQueue("title"));
        QCOMPARE(ids(), QStringList({alpha, beta, gamma}));
        QVERIFY(library.sortQueue("newest"));
        QCOMPARE(ids(), QStringList({beta, gamma, alpha}));
        QVERIFY(library.sortQueue("oldest"));
        QCOMPARE(ids(), QStringList({alpha, gamma, beta}));
        QVERIFY(library.sortQueue("author"));
        QCOMPARE(ids(), QStringList({beta, gamma, alpha}));
        QVERIFY(!library.sortQueue("unsupported"));
        QCOMPARE(ids(), QStringList({beta, gamma, alpha}));
    }

    void libraryMergesSnapshots() {
        QTemporaryDir firstDir;
        QTemporaryDir secondDir;
        Library first(firstDir.path());
        Library second(secondDir.path());

        QString firstA, firstB, secondA, secondC, error;
        QVERIFY(librarySave(first, "https://example.com/a", "Alpha", "First body.", &firstA, &error));
        QVERIFY(librarySave(first, "https://example.com/b", "Beta", "Second body.", &firstB, &error));
        QVERIFY(librarySave(second, "https://example.com/a", "Alpha again", "Third body.", &secondA, &error));
        QVERIFY(librarySave(second, "https://example.com/c", "Gamma", "Fourth body.", &secondC, &error));
        QVERIFY(firstA != secondA);

        first.setReadState(firstA, 1);
        first.saveProgress(firstA, 4);
        first.setTags(firstA, {"research"});
        QVERIFY(first.addToQueue(firstA));
        QVERIFY(first.addToQueue(firstB));

        second.saveProgress(secondA, 9);
        QVERIFY(second.addToQueue(secondC));

        // Fix the clocks so the merge rules are checked, not the test runtime.
        QVERIFY(setTimestamps("sift_merge_first", firstDir.path() + "/sift.db",
                              "2025-01-01T00:00:00Z", {{firstA, "2025-01-01T00:00:00Z"}}));
        QVERIFY(setTimestamps("sift_merge_second", secondDir.path() + "/sift.db",
                              "2025-02-01T00:00:00Z", {{secondA, "2025-02-01T00:00:00Z"}}));

        const QString snapshot = firstDir.path() + "/snapshot.db";
        QVERIFY2(first.exportSnapshot(snapshot, &error), qPrintable(error));
        QVERIFY(QFile::exists(snapshot));

        QVariantMap summary;
        QVERIFY2(second.mergeSnapshot(snapshot, &summary, &error), qPrintable(error));
        QCOMPARE(summary.value("added").toInt(), 1);
        QCOMPARE(summary.value("deduped").toInt(), 1);
        QCOMPARE(second.articles("all").size(), 3);

        // The same URL keeps one row. The later state wins progress and
        // metadata, while the union keeps the tag and the reading state.
        const QVariantMap mergedA = second.article(secondA);
        QCOMPARE(mergedA.value("title").toString(), QString("Alpha again"));
        QCOMPARE(mergedA.value("readState").toInt(), 1);
        QCOMPARE(mergedA.value("lastSentence").toInt(), 9);
        QCOMPARE(mergedA.value("tags").toStringList(), QStringList({"research"}));

        // The newer queue keeps its order and appends what only the other
        // machine had.
        QStringList queueIds;
        for (const QVariant &item : second.queue())
            queueIds << item.toMap().value("id").toString();
        QCOMPARE(queueIds, QStringList({secondC, secondA, firstB}));

        // The search index is rebuilt from the merged rows.
        QCOMPARE(second.articles("all", {}, "Beta").size(), 1);

        QVariantMap again;
        QVERIFY2(second.mergeSnapshot(snapshot, &again, &error), qPrintable(error));
        QCOMPARE(again.value("added").toInt(), 0);
        QCOMPARE(again.value("updated").toInt(), 0);
        QCOMPARE(second.articles("all").size(), 3);
        QCOMPARE(second.queue().size(), 3);
    }

    void libraryDeletesArticleAndFiles() {
        QTemporaryDir dir;
        Library library(dir.path());
        QString id, error;
        QVERIFY(library.saveArticle(articleData("https://example.com/a", "Delete me", "Body."), &id, &error));
        const QVariantMap article = library.article(id);
        const QString markdown = article.value("markdownPath").toString();
        const QString content = article.value("contentPath").toString();
        QVERIFY(QFile::exists(markdown));
        QVERIFY(QFile::exists(content));
        QVERIFY(library.deleteArticle(id));
        QVERIFY(library.article(id).isEmpty());
        QVERIFY(!QFile::exists(markdown));
        QVERIFY(!QFile::exists(content));
    }
};
}

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    SiftTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "sift_tests.moc"
