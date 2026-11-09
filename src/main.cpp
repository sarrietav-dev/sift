// sift — read the web, aloud, with Kokoro.

#include <QClipboard>
#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <QtWebEngineQuick/QtWebEngineQuick>

#include "backend.h"
#include "extractor.h"
#include "library.h"
#include "theme.h"
#include "translator.h"

#include <unistd.h>

namespace {
struct Cli {
    QString command = "gui";
    QString target;
    QString filter;
    bool json = false;
    bool help = false;
};

Cli parseCli(int argc, char *argv[]) {
    Cli cli;
    QStringList args;
    for (int i = 1; i < argc; ++i)
        args << QString::fromLocal8Bit(argv[i]);
    if (args.isEmpty())
        return cli;

    static const QStringList commands{"add", "list", "export", "play", "open", "queue", "help", "gui"};
    const QString first = args.first();
    if (first == "--help" || first == "-h") {
        cli.help = true;
        return cli;
    }
    if (first == "--version" || first == "-v") {
        cli.command = "version";
        return cli;
    }
    if (commands.contains(first)) {
        cli.command = first;
        args.removeFirst();
        if (cli.command == "queue" && !args.isEmpty() && args.first() == "add")
            args.removeFirst();
    } else if (first.startsWith("-")) {
        cli.help = true;
        return cli;
    } else {
        cli.command = "open";
        cli.target = first;
        args.removeFirst();
    }

    for (int i = 0; i < args.size(); ++i) {
        const QString arg = args.at(i);
        if (arg == "--json")
            cli.json = true;
        else if (arg == "--unread")
            cli.filter = "unread";
        else if (arg == "--reading")
            cli.filter = "reading";
        else if (arg == "--read")
            cli.filter = "read";
        else if (arg == "--help" || arg == "-h")
            cli.help = true;
        else if (!arg.startsWith("-") && cli.target.isEmpty())
            cli.target = arg;
    }
    return cli;
}

QString usage() {
    return QStringLiteral(
        "sift — read the web with Kokoro.\n\n"
        "Usage:\n"
        "  sift                              open the app\n"
        "  sift <url>                        open a page\n"
        "  sift add <url> [--json]\n"
        "  sift list [--unread|--reading|--read] [--json]\n"
        "  sift export <id|url>              Markdown to stdout\n"
        "  sift play <id|url>                open and start reading\n"
        "  sift queue add <id|url>           append to the queue\n\n"
        "The Kokoro endpoint is configured in sift's Settings.\n");
}

QString socketName() {
    return QStringLiteral("sift-%1").arg(static_cast<qlonglong>(getuid()));
}

bool forward(const QJsonObject &message) {
    QLocalSocket socket;
    socket.connectToServer(socketName());
    if (!socket.waitForConnected(250))
        return false;
    socket.write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
    const bool ok = socket.waitForBytesWritten(500);
    socket.disconnectFromServer();
    return ok;
}

void notifyRefresh() {
    QJsonObject message;
    message["cmd"] = "refresh";
    forward(message);
}

QString articleIdFor(Library &library, const QString &target) {
    if (target.isEmpty())
        return {};
    const QVariantList matches = library.articles("all");
    for (const QVariant &entry : matches) {
        const QVariantMap article = entry.toMap();
        if (article.value("id").toString() == target
            || article.value("url").toString() == target
            || article.value("canonicalUrl").toString() == target) {
            return article.value("id").toString();
        }
    }
    return target;
}

int runList(const Cli &cli, int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("sift");
    Library library;
    const QVariantList articles = library.articles(cli.filter.isEmpty() ? "all" : cli.filter);
    if (cli.json) {
        QJsonArray array;
        for (const QVariant &entry : articles) {
            const QVariantMap article = entry.toMap();
            QJsonObject object;
            object["id"] = article.value("id").toString();
            object["title"] = article.value("title").toString();
            object["url"] = article.value("url").toString();
            object["domain"] = article.value("domain").toString();
            object["readState"] = article.value("readState").toInt();
            object["lastSentence"] = article.value("lastSentence").toInt();
            object["wordCount"] = article.value("wordCount").toInt();
            array.append(object);
        }
        QTextStream(stdout) << QJsonDocument(array).toJson(QJsonDocument::Indented);
        return 0;
    }
    for (const QVariant &entry : articles) {
        const QVariantMap article = entry.toMap();
        const QString state = article.value("readState").toInt() == 2 ? "read"
            : article.value("readState").toInt() == 1 ? "reading" : "unread";
        QTextStream(stdout) << article.value("title").toString() << "  [" << state << "]  "
                            << article.value("domain").toString() << "  "
                            << article.value("id").toString() << '\n';
    }
    return 0;
}

int runExport(const Cli &cli, int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("sift");
    Library library;
    const QString id = articleIdFor(library, cli.target);
    const QString markdown = library.articleMarkdown(id);
    if (markdown.isEmpty()) {
        QTextStream(stderr) << "sift: no article at " << cli.target << '\n';
        return 1;
    }
    QTextStream(stdout) << markdown;
    return 0;
}

int runQueueAdd(const Cli &cli, int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("sift");
    Library library;
    const QString id = articleIdFor(library, cli.target);
    if (id.isEmpty() || !library.addToQueue(id)) {
        QTextStream(stderr) << "sift: no article at " << cli.target << '\n';
        return 1;
    }
    notifyRefresh();
    if (cli.json) {
        QJsonObject object;
        object["ok"] = true;
        object["id"] = id;
        QTextStream(stdout) << QJsonDocument(object).toJson(QJsonDocument::Compact) << '\n';
    }
    return 0;
}

int runAdd(const Cli &cli, int argc, char **argv) {
    QtWebEngineQuick::initialize();
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName("sift");
    QCoreApplication::setApplicationVersion("0.1.0");

    Library library;
    Extractor extractor;
    int exitCode = 1;

    QEventLoop loop;
    QObject::connect(&extractor, &Extractor::failed, &loop, [&](const QString &error) {
        QTextStream(stderr) << "sift: " << error << '\n';
        exitCode = 1;
        loop.quit();
    });
    QObject::connect(&extractor, &Extractor::finished, &loop, [&](const QVariantMap &article) {
        QString id;
        QString error;
        if (!library.saveArticle(article, &id, &error)) {
            QTextStream(stderr) << "sift: " << error << '\n';
            exitCode = 1;
            loop.quit();
            return;
        }
        notifyRefresh();
        if (cli.json) {
            QJsonObject object;
            object["ok"] = true;
            object["id"] = id;
            object["title"] = article.value("title").toString();
            object["url"] = article.value("url").toString();
            object["markdown"] = library.article(id).value("markdownPath").toString();
            QTextStream(stdout) << QJsonDocument(object).toJson(QJsonDocument::Compact) << '\n';
        } else {
            QTextStream(stdout) << article.value("title").toString() << '\n';
        }
        exitCode = 0;
        loop.quit();
    });

    QTimer::singleShot(60000, &loop, [&] {
        QTextStream(stderr) << "sift: extraction timed out\n";
        exitCode = 1;
        loop.quit();
    });

    extractor.extract(cli.target);
    loop.exec();
    return exitCode;
}

int runGui(const Cli &cli, int argc, char **argv) {
    QGuiApplication app(argc, argv);
    app.setApplicationName("sift");
    app.setApplicationVersion("0.1.0");
    app.setDesktopFileName("sift");
    app.setWindowIcon(QIcon::fromTheme("sift"));

    const bool wantsPlay = cli.command == "play";
    const bool wantsOpen = cli.command == "open";

    if (wantsPlay || wantsOpen || (cli.command == "gui" && cli.target.isEmpty())) {
        QJsonObject message;
        if (wantsPlay) {
            message["cmd"] = "play";
            message["target"] = cli.target;
        } else if (wantsOpen) {
            message["cmd"] = "open";
            message["url"] = cli.target;
        } else {
            message["cmd"] = "focus";
        }
        if (forward(message))
            return 0;
    }

    QQuickStyle::setStyle("Basic");

    Translator translator;
    app.installTranslator(&translator);
    Theme theme;
    Backend backend(&theme);

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    QQuickWindow *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());

    QLocalServer server;
    QLocalServer::removeServer(socketName());
    if (server.listen(socketName())) {
        QObject::connect(&server, &QLocalServer::newConnection, &app, [&] {
            while (QLocalSocket *socket = server.nextPendingConnection()) {
                QObject::connect(socket, &QLocalSocket::readyRead, socket, [&, socket] {
                    const QByteArray line = socket->readAll().trimmed();
                    const QJsonObject message = QJsonDocument::fromJson(line).object();
                    const QString cmd = message.value("cmd").toString();
                    if (cmd == "refresh") {
                        backend.refresh();
                    } else if (cmd == "focus") {
                        if (window)
                            window->requestActivate();
                    } else if (cmd == "open") {
                        backend.openUrl(message.value("url").toString(), true);
                        if (window) window->requestActivate();
                    } else if (cmd == "play") {
                        const QString target = message.value("target").toString();
                        if (target.startsWith("http://") || target.startsWith("https://"))
                            backend.openUrl(target, true);
                        else
                            backend.openArticle(target, true);
                        if (window) window->requestActivate();
                    }
                    socket->disconnectFromServer();
                    socket->deleteLater();
                });
            }
        });
    }

    if (wantsPlay && !cli.target.isEmpty()) {
        QTimer::singleShot(0, &backend, [&] {
            if (cli.target.startsWith("http://") || cli.target.startsWith("https://"))
                backend.openUrl(cli.target, true);
            else
                backend.openArticle(cli.target, true);
        });
    } else if (wantsOpen && !cli.target.isEmpty()) {
        QTimer::singleShot(0, &backend, [&] { backend.openUrl(cli.target, true); });
    }

    return app.exec();
}
} // namespace

int main(int argc, char *argv[]) {
    const Cli cli = parseCli(argc, argv);

    if (cli.help) {
        QTextStream(stdout) << usage();
        return 0;
    }
    if (cli.command == "version") {
        QTextStream(stdout) << "sift 0.1.0\n";
        return 0;
    }
    if (cli.command == "list")
        return runList(cli, argc, argv);
    if (cli.command == "export")
        return runExport(cli, argc, argv);
    if (cli.command == "queue")
        return runQueueAdd(cli, argc, argv);
    if (cli.command == "add") {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        QByteArray flags = qgetenv("QTWEBENGINE_CHROMIUM_FLAGS");
        if (!flags.contains("--disable-gpu"))
            flags += " --disable-gpu";
        qputenv("QTWEBENGINE_CHROMIUM_FLAGS", flags);
        return runAdd(cli, argc, argv);
    }

    QtWebEngineQuick::initialize();
    return runGui(cli, argc, argv);
}
