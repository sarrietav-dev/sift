#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickItem>
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

        // Returning to a workspace must not blank the article behind the
        // startup cover or leave the WebEngine view hidden until a timer fires.
        QObject *reader = window->findChild<QObject *>("readerView");
        QVERIFY(reader);
        QObject *readerFrame = window->findChild<QObject *>("readerFrame");
        QVERIFY(readerFrame);
        QVERIFY(readerFrame->property("live").toBool());
        QSignalSpy coverVisibility(cover, SIGNAL(visibleChanged()));
        QSignalSpy readerLoads(reader, SIGNAL(loadingChanged(QWebEngineLoadingInfo)));
        QVERIFY(coverVisibility.isValid());
        QVERIFY(readerLoads.isValid());
        const int renderCount = readerRender.size();
        auto *frameItem = qobject_cast<QQuickItem *>(readerFrame);
        QVERIFY(frameItem);
        const auto articleImage = [&]() {
            const QImage image = window->grabWindow();
            const qreal scale = qreal(image.width()) / window->width();
            const QPointF origin = frameItem->mapToScene(QPointF());
            return image.copy(QRect(qRound(origin.x() * scale), qRound(origin.y() * scale),
                                    qRound(frameItem->width() * scale), qRound(frameItem->height() * scale)));
        };
        for (int i = 0; i < 3; ++i) {
            // Let a tiling compositor finish any pending size configuration.
            QTest::qWait(150);
            const QImage beforeRecovery = articleImage();
            QVERIFY(!beforeRecovery.isNull());
            backend.readerSuspended();
            QVERIFY(!readerFrame->property("live").toBool());
            backend.readerNeedsRepaint();
            QVERIFY(reader->property("visible").toBool());
            QVERIFY(!readerFrame->property("live").toBool());
            QVERIFY(window->property("readerPainted").toBool());
            QVERIFY(!cover->property("visible").toBool());
            QTest::qWait(50);
            QCOMPARE(articleImage(), beforeRecovery);
            QTRY_VERIFY(readerFrame->property("live").toBool());
        }
        // Switching away again during recovery must keep the cached texture
        // frozen, not let the previous return's timer capture a black frame.
        backend.readerNeedsRepaint();
        backend.readerSuspended();
        QTest::qWait(450);
        QVERIFY(!readerFrame->property("live").toBool());
        backend.readerNeedsRepaint();
        QTRY_VERIFY(readerFrame->property("live").toBool());
        QCOMPARE(coverVisibility.size(), 0);
        QCOMPARE(readerLoads.size(), 0);
        QCOMPARE(readerRender.size(), renderCount);

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

        // A new session reopens the last article, paused.
        {
            Backend restored(&theme);
            QCOMPARE(restored.hasArticle(), false);
            restored.restoreLastArticle();
            QCOMPARE(restored.currentArticle().value("id").toString(), id);
            QCOMPARE(restored.playing(), false);
        }

        backend.seekSentence(1);
        QCOMPARE(backend.currentSentence(), 1);
        QCOMPARE(backend.playing(), false);
        window->resize(560, 520);
        QTest::keyClick(window, Qt::Key_F);
        QTRY_COMPARE(window->property("focusMode").toBool(), false);

        // Shift+J and Shift+K move the selected page through the queue.
        {
            Library library;
            QVERIFY(library.addToQueue(secondId));
        }
        backend.refresh();
        QCOMPARE(backend.queue().size(), 2);
        QObject *queueList = window->findChild<QObject *>("queueList");
        QVERIFY(queueList);
        QTRY_COMPARE(queueList->property("count").toInt(), 2);
        queueList->setProperty("currentIndex", 0);
        QTest::keyClick(window, Qt::Key_J, Qt::ShiftModifier);
        QTRY_COMPARE(backend.queue().at(1).toMap().value("id").toString(), id);
        QCOMPARE(queueList->property("currentIndex").toInt(), 1);
        QTest::keyClick(window, Qt::Key_K, Qt::ShiftModifier);
        QTRY_COMPARE(backend.queue().at(0).toMap().value("id").toString(), id);

        // Dragging the first row's grip below the second reorders the queue.
        {
            auto *list = qobject_cast<QQuickItem *>(queueList);
            QVERIFY(list);
            const QPointF origin = list->mapToScene(QPointF(26, 32));
            QPoint at = origin.toPoint();
            QTest::mouseMove(window, at);
            QTest::qWait(50);
            QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, at);
            for (int step = 0; step < 12; ++step) {
                at.ry() += 7;
                QTest::mouseMove(window, at);
                QTest::qWait(16);
            }
            if (qEnvironmentVariableIsSet("SIFT_GUI_DRAG_SCREENSHOT"))
                QVERIFY(window->grabWindow().save(qEnvironmentVariable("SIFT_GUI_DRAG_SCREENSHOT")));
            QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, at);
            QTRY_COMPARE(backend.queue().at(1).toMap().value("id").toString(), id);
        }
        QTest::qWait(200);
        if (qEnvironmentVariableIsSet("SIFT_GUI_QUEUE_SCREENSHOT"))
            QVERIFY(window->grabWindow().save(qEnvironmentVariable("SIFT_GUI_QUEUE_SCREENSHOT")));
        window->resize(1180, 780);
        QObject *actionsMenu = window->findChild<QObject *>("actionsMenu");
        QVERIFY(actionsMenu);
        QVERIFY(QMetaObject::invokeMethod(actionsMenu, "open"));
        QTRY_VERIFY(actionsMenu->property("visible").toBool());
        QTest::qWait(200);
        if (qEnvironmentVariableIsSet("SIFT_GUI_MENU_SCREENSHOT"))
            QVERIFY(window->grabWindow().save(qEnvironmentVariable("SIFT_GUI_MENU_SCREENSHOT")));
        QVERIFY(QMetaObject::invokeMethod(actionsMenu, "close"));
        QTRY_VERIFY(!actionsMenu->property("visible").toBool());

        for (const char *name : {"addDialog", "voiceDialog", "settingsDialog"}) {
            QObject *dialog = window->findChild<QObject *>(name);
            QVERIFY2(dialog, name);
            QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
            QTRY_VERIFY(dialog->property("visible").toBool());
            QTest::qWait(100);
            if (QByteArray(name) == "settingsDialog" && qEnvironmentVariableIsSet("SIFT_GUI_SETTINGS_SCREENSHOT"))
                QVERIFY(window->grabWindow().save(qEnvironmentVariable("SIFT_GUI_SETTINGS_SCREENSHOT")));
            if (QByteArray(name) == "settingsDialog") {
                // The font list opens on the current font, filters as you
                // type, and picks the highlighted entry with Enter.
                QObject *fontBox = window->findChild<QObject *>("fontBox");
                QVERIFY(fontBox);
                QVERIFY(QMetaObject::invokeMethod(fontBox, "openPopup"));
                QTRY_VERIFY(fontBox->property("popupOpen").toBool());
                QTest::qWait(150);
                if (qEnvironmentVariableIsSet("SIFT_GUI_DROPDOWN_SCREENSHOT"))
                    QVERIFY(window->grabWindow().save(qEnvironmentVariable("SIFT_GUI_DROPDOWN_SCREENSHOT")));
                const QString target = backend.fonts().last().toString();
                for (const QChar &c : target)
                    QTest::keyClick(window, c.toLatin1());
                QTRY_VERIFY(!fontBox->property("filtered").toList().isEmpty());
                QVERIFY(fontBox->property("filtered").toList().size() < backend.fonts().size());
                QTest::keyClick(window, Qt::Key_Return);
                QTRY_VERIFY(!fontBox->property("popupOpen").toBool());
                QVERIFY(fontBox->property("currentText").toString().contains(target, Qt::CaseInsensitive));
            }
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
