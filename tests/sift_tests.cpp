#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QtTest>

#include "library.h"
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
