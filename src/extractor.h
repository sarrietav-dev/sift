#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <QVariantMap>

class QWebEngineLoadingInfo;
class QWebEnginePage;
class QWebEngineUrlRequestInterceptor;

// Loads a public page in a hidden Chromium page and runs Defuddle (the same
// extractor Obsidian Web Clipper uses) inside it. The visible reader is a
// separate WebEngine view.
//
// Web Clipper is fast because the page is already loaded in the user's tab;
// it only has to run Defuddle. sift has to fetch the page, so it gets as close
// as it can: Defuddle is compiled into the page while the HTML streams in, the
// article is parsed as soon as the DOM is ready rather than after every ad,
// image and iframe has loaded, and requests that cannot change the article
// text (images, media, fonts, frames, trackers) are never made. Pages that
// build their article with script fall back to waiting for the load to finish
// and the text to stop changing.
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
    void loadingChanged(const QWebEngineLoadingInfo &info);
    void poll();
    void parsePage(bool final);
    void parsed(const QString &json, bool final);
    void finish(const QVariantMap &article);
    void fail(const QString &error);
    void idle();
    void trace(const QString &message) const;
    static QString normalizedUrl(const QString &url);

    QWebEnginePage *m_page = nullptr;
    QWebEngineUrlRequestInterceptor *m_interceptor = nullptr;
    QTimer m_timeout;
    QTimer m_poll;
    QElapsedTimer m_clock;
    QString m_url;
    QVariantMap m_best;
    QString m_lastError;
    bool m_busy = false;
    bool m_loadDone = false;
    bool m_loadFailed = false;
    bool m_inFlight = false;
    bool m_triedEarly = false;
    bool m_patient = false;
    int m_progress = 0;
    int m_generation = 0;
    int m_lastTextLength = -1;
    int m_stableTicks = 0;
    qint64 m_domReadyAt = -1;
};
