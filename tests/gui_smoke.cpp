#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>
#include <QtWebEngineQuick/QtWebEngineQuick>

#include "backend.h"
#include "library.h"
#include "theme.h"
#include "translator.h"

class GuiSmoke : public QObject {
    Q_OBJECT
private slots:
    void readerAndDialogs() {
        QString id, error;
        {
            Library library;
            QVariantMap article{{"url", "https://example.com/test"},
                                {"canonicalUrl", "https://example.com/test"},
                                {"title", "The Quiet Art of Reading"}, {"domain", "example.com"},
                                {"author", "Example Author"}, {"language", "en"},
                                {"wordCount", 12},
                                {"contentHtml", "<p>Reading is a conversation with someone who is not in the room. The page waits, patient and exact, until you are ready to continue.</p>"
                                                "<p>We tend to treat attention as a resource to spend, but a good essay asks for something quieter: a willingness to follow one thought all the way to its end.</p>"
                                                "<h2>A small practice</h2><p>Set the page in front of you and let the room go soft. Read one paragraph twice. Notice where your mind pulls away, and bring it back without scolding yourself.</p>"
                                                "<blockquote>The reader who stops to think is not falling behind. They are doing the work.</blockquote>"},
                                {"markdown", "A first sentence. A second sentence."},
                                {"contentText", "A first sentence. A second sentence."}};
            QVERIFY2(library.saveArticle(article, &id, &error), qPrintable(error));
            QVERIFY(library.addToQueue(id));
        }
        Theme theme(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/missing");
        Translator translator;
        Backend backend(&theme);
        QVERIFY(!backend.fonts().isEmpty());
        const QString chosenFont = backend.fonts().contains("IBM Plex Serif")
            ? QStringLiteral("IBM Plex Serif") : backend.fonts().first().toString();
        backend.applySettings(QVariantMap{{"readerFont", chosenFont}});
        QCOMPARE(backend.readerFont(), chosenFont);
        QCOMPARE(backend.settings().value("readerFont").toString(), chosenFont);
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("theme", &theme);
        engine.rootContext()->setContextProperty("translator", &translator);
        engine.rootContext()->setContextProperty("backend", &backend);
        engine.load(QUrl("qrc:/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        QVERIFY(backend.readerShell().contains("--sift-body-font:"));
        QVERIFY(backend.readerShell().contains("script-src 'nonce-"));
        QSignalSpy readerRender(&backend, &Backend::readerRender);
        backend.openArticle(id, false);
        QCOMPARE(readerRender.size(), 1);
        const QVariantMap payload = readerRender.first().at(0).toMap();
        QCOMPARE(payload.value("title").toString(), QString("The Quiet Art of Reading"));
        QCOMPARE(payload.value("meta").toList().first().toMap().value("text").toString(), QString("Example Author"));
        QVERIFY(payload.value("html").toString().contains("A small practice"));
        QTRY_VERIFY_WITH_TIMEOUT(backend.sentenceCount() > 0, 15000);
        QTRY_VERIFY_WITH_TIMEOUT(window->property("readerPainted").toBool(), 5000);
        QObject *cover = window->findChild<QObject *>("readerCover");
        QVERIFY(cover);
        QTRY_VERIFY(!cover->property("visible").toBool());
        QCOMPARE(backend.queue().size(), 1);
        QCOMPARE(backend.currentArticle().value("id").toString(), id);
        QCOMPARE(backend.playing(), false);

        window->resize(1180, 780);
        QTest::qWait(300);
        const QImage screenshot = window->grabWindow();
        QVERIFY(!screenshot.isNull());
        if (qEnvironmentVariableIsSet("SIFT_GUI_SCREENSHOT"))
            QVERIFY(screenshot.save(qEnvironmentVariable("SIFT_GUI_SCREENSHOT")));

        // A second article renders into the same page without a reload.
        QString secondId;
        {
            Library library;
            QVariantMap second{{"url", "https://example.com/second"}, {"canonicalUrl", "https://example.com/second"},
                               {"title", "Second Thoughts"}, {"domain", "example.com"}, {"language", "en"},
                               {"wordCount", 6}, {"contentHtml", "<p>One sentence here. Another one there.</p>"},
                               {"markdown", "x"}, {"contentText", "x"}};
            QVERIFY2(library.saveArticle(second, &secondId, &error), qPrintable(error));
        }
        backend.openArticle(secondId, false);
        QTRY_COMPARE_WITH_TIMEOUT(backend.sentenceCount(), 2, 5000);
        backend.openArticle(id, false);
        QTRY_VERIFY_WITH_TIMEOUT(backend.sentenceCount() > 2, 5000);

        backend.seekSentence(1);
        QCOMPARE(backend.currentSentence(), 1);
        QCOMPARE(backend.playing(), false);
        window->resize(560, 520);
        QTest::keyClick(window, Qt::Key_F);
        QTRY_COMPARE(window->property("focusMode").toBool(), false);
        QTest::qWait(200);
        if (qEnvironmentVariableIsSet("SIFT_GUI_QUEUE_SCREENSHOT"))
            QVERIFY(window->grabWindow().save(qEnvironmentVariable("SIFT_GUI_QUEUE_SCREENSHOT")));
        for (const char *name : {"addDialog", "voiceDialog", "settingsDialog"}) {
            QObject *dialog = window->findChild<QObject *>(name);
            QVERIFY2(dialog, name);
            QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
            QTRY_VERIFY(dialog->property("visible").toBool());
            QTest::qWait(100);
            if (QByteArray(name) == "settingsDialog" && qEnvironmentVariableIsSet("SIFT_GUI_SETTINGS_SCREENSHOT"))
                QVERIFY(window->grabWindow().save(qEnvironmentVariable("SIFT_GUI_SETTINGS_SCREENSHOT")));
            QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
            QTRY_VERIFY(!dialog->property("visible").toBool());
        }
    }
};

int main(int argc, char **argv) {
    QTemporaryDir home;
    if (!home.isValid()) return 1;
    qputenv("HOME", home.path().toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + "/data").toUtf8());
    qputenv("XDG_CONFIG_HOME", (home.path() + "/config").toUtf8());
    qputenv("XDG_CACHE_HOME", (home.path() + "/cache").toUtf8());
    qputenv("QTWEBENGINE_DISABLE_SANDBOX", "1");
    QtWebEngineQuick::initialize();
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName("sift");
    QQuickStyle::setStyle("Basic");
    GuiSmoke test;
    return QTest::qExec(&test, argc, argv);
}

#include "gui_smoke.moc"
