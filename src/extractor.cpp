#include "extractor.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QUrl>
#include <QWebEngineLoadingInfo>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QWebEngineSettings>
#include <QWebEngineUrlRequestInterceptor>

namespace {

// Defuddle reads the DOM and computed styles. None of these requests can
// change the article text, and on a news site they are most of the traffic and
// most of the main-thread work. Image elements stay in the DOM with their
// attributes, so the saved article still points at them.
class QuietInterceptor : public QWebEngineUrlRequestInterceptor {
public:
    using QWebEngineUrlRequestInterceptor::QWebEngineUrlRequestInterceptor;

    void interceptRequest(QWebEngineUrlRequestInfo &info) override {
        switch (info.resourceType()) {
        case QWebEngineUrlRequestInfo::ResourceTypeMainFrame:
            return;
        case QWebEngineUrlRequestInfo::ResourceTypeSubFrame:
        case QWebEngineUrlRequestInfo::ResourceTypeImage:
        case QWebEngineUrlRequestInfo::ResourceTypeFontResource:
        case QWebEngineUrlRequestInfo::ResourceTypeObject:
        case QWebEngineUrlRequestInfo::ResourceTypeMedia:
        case QWebEngineUrlRequestInfo::ResourceTypePrefetch:
        case QWebEngineUrlRequestInfo::ResourceTypeFavicon:
        case QWebEngineUrlRequestInfo::ResourceTypePing:
        case QWebEngineUrlRequestInfo::ResourceTypeCspReport:
        case QWebEngineUrlRequestInfo::ResourceTypePluginResource:
        case QWebEngineUrlRequestInfo::ResourceTypeServiceWorker:
        case QWebEngineUrlRequestInfo::ResourceTypeSharedWorker:
        case QWebEngineUrlRequestInfo::ResourceTypeWebSocket:
            info.block(true);
            return;
        default:
            break;
        }
        if (isTracker(info.requestUrl().host()))
            info.block(true);
    }

private:
    static bool isTracker(const QString &host) {
        static const QStringList hosts = {
            "doubleclick.net", "googlesyndication.com", "googletagmanager.com",
            "googletagservices.com", "google-analytics.com", "googleadservices.com",
            "adservice.google.com", "amazon-adsystem.com", "adnxs.com", "criteo.com",
            "criteo.net", "taboola.com", "outbrain.com", "scorecardresearch.com",
            "chartbeat.com", "chartbeat.net", "hotjar.com", "segment.com", "segment.io",
            "quantserve.com", "moatads.com", "rubiconproject.com", "pubmatic.com",
            "openx.net", "casalemedia.com", "teads.tv", "connect.facebook.net",
            "bat.bing.com", "cdn.permutive.com", "permutive.app", "newrelic.com",
            "nr-data.net", "sentry.io", "optimizely.com", "parsely.com", "parse.ly",
            "krxd.net", "bluekai.com", "adsrvr.org", "3lift.com", "media.net",
            "sharethrough.com", "indexww.com", "smartadserver.com", "yieldmo.com",
            "doubleverify.com", "adsafeprotected.com", "branch.io", "clarity.ms",
        };
        for (const QString &blocked : hosts) {
            if (host == blocked || host.endsWith(QLatin1Char('.') + blocked))
                return true;
        }
        return false;
    }
};

// Cheap enough to run every tick: it tells whether the DOM is parsed and
// whether script is still changing the page's text.
const QString kProbe = QStringLiteral(
    "(function(){var b=document.body;"
    "return [location.protocol, document.readyState, b ? b.textContent.length : 0].join('|');})()");

// Defuddle is injected as a user script when the document is created, so it
// compiles while the HTML is still arriving and is ready the moment the DOM is.
const QString kParse = QStringLiteral(R"JS(
(function () {
  try {
    // An error page (404, 403, 500) is not an article, however much text it has.
    var navigation = performance.getEntriesByType('navigation')[0];
    var status = navigation && navigation.responseStatus || 0;
    if (status >= 400) {
      return JSON.stringify({ ok: false, status: status });
    }
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
    // An inert document: parsing the article here does not fetch its images.
    var holder = document.implementation.createHTMLDocument('').body;
    holder.innerHTML = result.content;
    var text = (holder.textContent || '').replace(/\s+/g, ' ').trim();
    var words = result.wordCount || (text ? text.split(/\s+/).length : 0);
    return JSON.stringify({
      ok: true,
      parseTime: result.parseTime || 0,
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

// A parse at DOM-ready is trusted when it found at least this much text. Less
// usually means the page is a script-rendered shell, so sift waits for it.
constexpr int kEarlyWords = 200;
constexpr int kPollMs = 100;
// After the load finishes, the text has to hold still for this many ticks.
constexpr int kQuietTicksAfterLoad = 3;
// When that still finds little, the article may be arriving by XHR after the
// load event. Wait for a longer quiet spell before settling for it.
constexpr int kQuietTicksForThinPage = 12;
// Pages that never finish loading (endless ad auctions, long polling) are
// parsed once the text has been still this long.
constexpr int kQuietTicksWhileLoading = 10;
constexpr qint64 kPatienceMs = 8000;

} // namespace

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
    m_interceptor = new QuietInterceptor(this);
    m_page->setUrlRequestInterceptor(m_interceptor);
    auto *pageSettings = m_page->settings();
    pageSettings->setAttribute(QWebEngineSettings::AutoLoadImages, false);
    pageSettings->setAttribute(QWebEngineSettings::WebGLEnabled, false);
    pageSettings->setAttribute(QWebEngineSettings::Accelerated2dCanvasEnabled, false);
    pageSettings->setAttribute(QWebEngineSettings::PdfViewerEnabled, false);
    pageSettings->setAttribute(QWebEngineSettings::AutoLoadIconsForPage, false);
    pageSettings->setAttribute(QWebEngineSettings::PlaybackRequiresUserGesture, true);

    QFile bundle(":/defuddle.full.js");
    if (bundle.open(QIODevice::ReadOnly)) {
        QWebEngineScript defuddle;
        defuddle.setName("sift-defuddle");
        defuddle.setSourceCode(QString::fromUtf8(bundle.readAll()));
        defuddle.setInjectionPoint(QWebEngineScript::DocumentCreation);
        defuddle.setWorldId(QWebEngineScript::ApplicationWorld);
        defuddle.setRunsOnSubFrames(false);
        m_page->scripts().insert(defuddle);
    }

    connect(m_page, &QWebEnginePage::loadingChanged, this, &Extractor::loadingChanged);
    connect(m_page, &QWebEnginePage::loadProgress, this, [this](int progress) {
        if (!m_busy)
            return;
        m_progress = qBound(m_progress, progress, 95);
        emit progressChanged();
    });
    connect(m_page, &QWebEnginePage::renderProcessTerminated, this,
            [this](QWebEnginePage::RenderProcessTerminationStatus, int) {
                if (m_busy)
                    fail(tr("The page renderer stopped before sift could read it."));
            });

    m_poll.setInterval(kPollMs);
    connect(&m_poll, &QTimer::timeout, this, &Extractor::poll);

    m_timeout.setSingleShot(true);
    m_timeout.setInterval(45000);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        if (!m_busy)
            return;
        if (!m_best.isEmpty())
            finish(m_best);
        else
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

void Extractor::trace(const QString &message) const {
    static const bool enabled = qEnvironmentVariableIsSet("SIFT_TRACE_EXTRACT");
    if (enabled)
        QTextStream(stderr) << QStringLiteral("[extract %1ms] ").arg(m_clock.elapsed()) << message << '\n';
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
    m_best.clear();
    m_lastError.clear();
    m_loadDone = false;
    m_loadFailed = false;
    m_inFlight = false;
    m_triedEarly = false;
    m_patient = false;
    m_lastTextLength = -1;
    m_stableTicks = 0;
    m_domReadyAt = -1;
    m_busy = true;
    m_progress = 0;
    m_clock.start();
    emit busyChanged();
    emit progressChanged();
    if (m_page->lifecycleState() != QWebEnginePage::LifecycleState::Active)
        m_page->setLifecycleState(QWebEnginePage::LifecycleState::Active);
    m_timeout.start();
    m_page->load(QUrl(target));
    m_poll.start();
    trace("load " + target);
}

void Extractor::cancel() {
    if (m_busy) {
        m_busy = false;
        m_progress = 0;
        emit busyChanged();
        emit progressChanged();
    }
    idle();
}

// Stops the network and freezes the page, so the last site's scripts do not
// keep running in the background while the user reads and listens.
void Extractor::idle() {
    m_timeout.stop();
    m_poll.stop();
    if (!m_page)
        return;
    m_page->triggerAction(QWebEnginePage::Stop);
    if (m_page->lifecycleState() == QWebEnginePage::LifecycleState::Active
        && !m_page->isVisible()) {
        m_page->setLifecycleState(QWebEnginePage::LifecycleState::Frozen);
    }
}

void Extractor::loadingChanged(const QWebEngineLoadingInfo &info) {
    if (!m_busy)
        return;
    switch (info.status()) {
    case QWebEngineLoadingInfo::LoadSucceededStatus:
        m_loadDone = true;
        trace("load finished");
        break;
    case QWebEngineLoadingInfo::LoadFailedStatus:
        m_loadDone = true;
        m_loadFailed = true;
        trace(QStringLiteral("load failed: %1 (%2)").arg(info.errorString()).arg(info.errorCode()));
        break;
    default:
        return;
    }
    poll();
}

void Extractor::poll() {
    if (!m_busy || m_inFlight)
        return;
    const int generation = m_generation;
    m_inFlight = true;
    m_page->runJavaScript(kProbe, QWebEngineScript::ApplicationWorld, [this, generation](const QVariant &value) {
        if (!m_busy || generation != m_generation)
            return;
        m_inFlight = false;
        const QStringList parts = value.toString().split('|');
        const bool document = parts.size() == 3
            && (parts.at(0) == "http:" || parts.at(0) == "https:")
            && parts.at(1) != "loading";
        if (!document) {
            // Still on the previous page, or Chromium's error page.
            if (m_loadFailed)
                fail(tr("Could not load %1.").arg(QUrl(m_url).host()));
            return;
        }
        const int textLength = parts.at(2).toInt();
        if (m_domReadyAt < 0) {
            m_domReadyAt = m_clock.elapsed();
            trace("dom ready");
        }
        if (!m_triedEarly) {
            m_triedEarly = true;
            m_lastTextLength = textLength;
            parsePage(false);
            return;
        }
        m_stableTicks = textLength == m_lastTextLength ? m_stableTicks + 1 : 0;
        m_lastTextLength = textLength;
        const int quiet = m_patient ? kQuietTicksForThinPage : kQuietTicksAfterLoad;
        const bool settled = m_loadDone && m_stableTicks >= quiet;
        const bool stuck = m_clock.elapsed() - m_domReadyAt > kPatienceMs
            && m_stableTicks >= kQuietTicksWhileLoading;
        if (settled || stuck)
            parsePage(true);
    });
}

void Extractor::parsePage(bool final) {
    const int generation = m_generation;
    m_inFlight = true;
    m_page->runJavaScript(kParse, QWebEngineScript::ApplicationWorld, [this, generation, final](const QVariant &value) {
        if (!m_busy || generation != m_generation)
            return;
        m_inFlight = false;
        parsed(value.toString(), final);
    });
}

void Extractor::parsed(const QString &json, bool final) {
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8());
    QVariantMap article;
    if (!document.isObject()) {
        m_lastError = tr("The article extractor returned an unreadable result.");
    } else {
        const QJsonObject object = document.object();
        if (object.value("ok").toBool()) {
            article = object.value("data").toObject().toVariantMap();
            trace(QStringLiteral("%1 parse: %2 words, defuddle %3ms")
                      .arg(final ? "final" : "early")
                      .arg(article.value("wordCount").toInt())
                      .arg(object.value("parseTime").toDouble()));
        } else if (object.value("status").toInt() >= 400) {
            trace(QStringLiteral("http status %1").arg(object.value("status").toInt()));
            fail(tr("Could not load %1.").arg(QUrl(m_url).host()));
            return;
        } else {
            m_lastError = object.value("error").toString();
            trace("parse failed: " + m_lastError);
        }
    }

    if (!article.isEmpty()
        && article.value("wordCount").toInt() >= m_best.value("wordCount").toInt()) {
        m_best = article;
    }
    if (!final && article.value("wordCount").toInt() >= kEarlyWords) {
        finish(article);
        return;
    }
    if (!final)
        return; // Keep polling until the page settles.
    if (!m_patient && m_best.value("wordCount").toInt() < kEarlyWords) {
        m_patient = true;
        m_stableTicks = 0;
        return;
    }
    if (!m_best.isEmpty()) {
        finish(m_best);
        return;
    }
    fail(m_lastError.isEmpty() ? tr("No article content was found on this page.") : m_lastError);
}

void Extractor::finish(const QVariantMap &article) {
    trace("done");
    m_busy = false;
    m_progress = 100;
    idle();
    emit busyChanged();
    emit progressChanged();
    emit finished(article);
}

void Extractor::fail(const QString &error) {
    trace("failed: " + error);
    m_busy = false;
    m_progress = 0;
    idle();
    emit busyChanged();
    emit progressChanged();
    emit failed(error);
}
