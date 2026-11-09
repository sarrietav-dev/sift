#pragma once

#include <QObject>
#include <QTimer>
#include <QVariantMap>

class QWebEnginePage;

// Loads a public page in a hidden Chromium page and runs Defuddle (the same
// extractor Obsidian Web Clipper uses) inside it. The visible reader is a
// separate WebEngine view.
class Extractor : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)

public:
    explicit Extractor(QObject *parent = nullptr);

    bool busy() const { return m_busy; }
    int progress() const { return m_progress; }

    Q_INVOKABLE void extract(const QString &url);
    Q_INVOKABLE void cancel();

signals:
    void busyChanged();
    void progressChanged();
    void finished(const QVariantMap &article);
    void failed(const QString &error);

private:
    void loadFinished(bool ok);
    void injectDefuddle();
    void parsePage();
    void finish(const QVariantMap &article);
    void fail(const QString &error);
    static QString normalizedUrl(const QString &url);

    QWebEnginePage *m_page = nullptr;
    QTimer m_timeout;
    QTimer m_settle;
    QString m_url;
    bool m_busy = false;
    int m_progress = 0;
    int m_generation = 0;
};
