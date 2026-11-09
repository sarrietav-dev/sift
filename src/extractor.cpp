#include "extractor.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QUrl>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineScript>
#include <QWebEngineSettings>

Extractor::Extractor(QObject *parent)
    : QObject(parent) {
    auto *profile = QWebEngineProfile::defaultProfile();
    profile->setHttpUserAgent(
        "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) "
        "Chrome/131.0.0.0 Safari/537.36");
    auto *settings = profile->settings();
    settings->setAttribute(QWebEngineSettings::JavascriptEnabled, true);
    settings->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, true);
    settings->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, true);
    settings->setAttribute(QWebEngineSettings::JavascriptCanOpenWindows, false);
    settings->setAttribute(QWebEngineSettings::PluginsEnabled, false);

    m_page = new QWebEnginePage(profile, this);
    connect(m_page, &QWebEnginePage::loadFinished, this, &Extractor::loadFinished);
    connect(m_page, &QWebEnginePage::loadProgress, this, [this](int progress) {
        if (!m_busy)
            return;
        m_progress = qBound(0, progress, 95);
        emit progressChanged();
    });
    connect(m_page, &QWebEnginePage::renderProcessTerminated, this,
            [this](QWebEnginePage::RenderProcessTerminationStatus, int) {
                if (m_busy)
                    fail(tr("The page renderer stopped before sift could read it."));
            });

    m_settle.setSingleShot(true);
    m_settle.setInterval(1200);
    connect(&m_settle, &QTimer::timeout, this, &Extractor::injectDefuddle);

    m_timeout.setSingleShot(true);
    m_timeout.setInterval(45000);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        if (m_busy)
            fail(tr("The page took too long to load."));
    });
}

QString Extractor::normalizedUrl(const QString &url) {
    QString value = url.trimmed();
    if (value.isEmpty())
        return {};
    if (!value.contains("://"))
        value = "https://" + value;
    const QUrl parsed = QUrl::fromUserInput(value);
    if (!parsed.isValid() || (parsed.scheme() != "http" && parsed.scheme() != "https"))
        return {};
    return parsed.toString();
}

void Extractor::extract(const QString &url) {
    const QString target = normalizedUrl(url);
    if (target.isEmpty()) {
        emit failed(tr("Enter a valid http or https address."));
        return;
    }
    cancel();
    ++m_generation;
    m_url = target;
    m_busy = true;
    m_progress = 0;
    emit busyChanged();
    emit progressChanged();
    m_timeout.start();
    m_page->load(QUrl(target));
}

void Extractor::cancel() {
    m_timeout.stop();
    m_settle.stop();
    if (m_page)
        m_page->triggerAction(QWebEnginePage::Stop);
    if (m_busy) {
        m_busy = false;
        m_progress = 0;
        emit busyChanged();
        emit progressChanged();
    }
}

void Extractor::loadFinished(bool ok) {
    if (!m_busy)
        return;
    if (!ok) {
        fail(tr("Could not load %1.").arg(QUrl(m_url).host()));
        return;
    }
    m_progress = 95;
    emit progressChanged();
    m_settle.start();
}

void Extractor::injectDefuddle() {
    if (!m_busy)
        return;
    const int generation = m_generation;
    QFile bundle(":/defuddle.full.js");
    if (!bundle.open(QIODevice::ReadOnly)) {
        fail(tr("The bundled article extractor is missing."));
        return;
    }
    const QString source = QString::fromUtf8(bundle.readAll());
    m_page->runJavaScript(source, QWebEngineScript::ApplicationWorld, [this, generation](const QVariant &) {
        if (!m_busy || generation != m_generation)
            return;
        parsePage();
    });
}

void Extractor::parsePage() {
    const int generation = m_generation;
    const QString script = QStringLiteral(R"JS(
(function () {
  try {
    if (!window.Defuddle) {
      return JSON.stringify({ ok: false, error: 'The article extractor did not load.' });
    }
    var canonicalEl = document.querySelector('link[rel="canonical"]');
    var canonical = canonicalEl && canonicalEl.href ? canonicalEl.href : location.href;
    var extractor = new window.Defuddle(document, {
      url: location.href,
      language: document.documentElement.lang || undefined
    });
    var result = extractor.parse();
    if (!result || !result.content) {
      return JSON.stringify({ ok: false, error: 'No article content was found on this page.' });
    }
    var holder = document.createElement('div');
    holder.innerHTML = result.content;
    var text = (holder.innerText || holder.textContent || '').replace(/\s+/g, ' ').trim();
    var words = result.wordCount || (text ? text.split(/\s+/).length : 0);
    return JSON.stringify({
      ok: true,
      data: {
        url: location.href,
        canonicalUrl: canonical,
        title: result.title || document.title || '',
        author: result.author || '',
        published: result.published || '',
        domain: result.domain || location.hostname || '',
        site: result.site || '',
        language: result.language || document.documentElement.lang || navigator.language || 'en',
        wordCount: words,
        contentHtml: result.content,
        markdown: result.contentMarkdown || '',
        contentText: text,
        excerpt: result.description || text.slice(0, 240),
        favicon: result.favicon || '',
        image: result.image || ''
      }
    });
  } catch (error) {
    return JSON.stringify({ ok: false, error: String(error && error.message ? error.message : error) });
  }
})()
)JS");
    m_page->runJavaScript(script, QWebEngineScript::ApplicationWorld, [this, generation](const QVariant &value) {
        if (!m_busy || generation != m_generation)
            return;
        const QString json = value.toString();
        const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8());
        if (!document.isObject()) {
            fail(tr("The article extractor returned an unreadable result."));
            return;
        }
        const QJsonObject object = document.object();
        if (!object.value("ok").toBool()) {
            QString error = object.value("error").toString();
            if (error.isEmpty())
                error = tr("No article content was found on this page.");
            fail(error);
            return;
        }
        m_progress = 100;
        emit progressChanged();
        finish(object.value("data").toObject().toVariantMap());
    });
}

void Extractor::finish(const QVariantMap &article) {
    m_timeout.stop();
    m_settle.stop();
    m_busy = false;
    emit busyChanged();
    emit progressChanged();
    emit finished(article);
}

void Extractor::fail(const QString &error) {
    m_timeout.stop();
    m_settle.stop();
    if (m_page)
        m_page->triggerAction(QWebEnginePage::Stop);
    m_busy = false;
    m_progress = 0;
    emit busyChanged();
    emit progressChanged();
    emit failed(error);
}
