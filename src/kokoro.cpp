#include "kokoro.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include <algorithm>
#include <memory>

Kokoro::Kokoro(QObject *parent)
    : QObject(parent),
      m_manager(new QNetworkAccessManager(this)) {
    loadDefaultVoices();
}

void Kokoro::setEndpoint(const QString &endpoint) {
    QString clean = endpoint.trimmed();
    while (clean.endsWith('/'))
        clean.chop(1);
    if (clean.isEmpty())
        clean = "http://127.0.0.1:8880";
    if (clean == m_endpoint)
        return;
    m_endpoint = clean;
    emit endpointChanged();
}

QVariantMap Kokoro::voiceInfo(const QString &id) {
    QVariantMap info;
    const QString base = id.section('+', 0, 0);
    const QString prefix = base.section('_', 0, 0);
    const QChar language = prefix.isEmpty() ? QChar('a') : prefix.at(0);
    const QChar gender = prefix.size() > 1 ? prefix.at(1) : QChar('f');

    QString languageCode;
    QString languageName;
    switch (language.unicode()) {
    case 'a': languageCode = "en-US"; languageName = QObject::tr("English (US)"); break;
    case 'b': languageCode = "en-GB"; languageName = QObject::tr("English (UK)"); break;
    case 'e': languageCode = "es"; languageName = QObject::tr("Spanish"); break;
    case 'f': languageCode = "fr"; languageName = QObject::tr("French"); break;
    case 'h': languageCode = "hi"; languageName = QObject::tr("Hindi"); break;
    case 'i': languageCode = "it"; languageName = QObject::tr("Italian"); break;
    case 'j': languageCode = "ja"; languageName = QObject::tr("Japanese"); break;
    case 'p': languageCode = "pt-BR"; languageName = QObject::tr("Portuguese (Brazil)"); break;
    case 'z': languageCode = "zh"; languageName = QObject::tr("Chinese (Mandarin)"); break;
    default: languageCode = "en-US"; languageName = QObject::tr("English (US)"); break;
    }

    info["id"] = id;
    info["name"] = id;
    info["languageCode"] = languageCode;
    info["language"] = languageName;
    info["gender"] = gender == 'm' ? QObject::tr("Male") : QObject::tr("Female");
    info["search"] = id + " " + languageCode + " " + languageName + " " + info.value("gender").toString();
    return info;
}

void Kokoro::loadDefaultVoices() {
    static const QStringList ids{
        "af_heart", "af_bella", "af_nicole", "af_sarah", "af_sky", "am_adam", "am_michael",
        "bf_emma", "bf_isabella", "bm_george", "bm_lewis",
        "ef_dora", "em_alex", "ff_siwis", "hf_alpha", "hm_omega", "if_sara", "im_nicola",
        "jf_alpha", "jm_kumo", "pf_dora", "pm_alex", "zf_xiaobei", "zm_yunjian"
    };
    m_voices.clear();
    for (const QString &id : ids)
        m_voices << voiceInfo(id);
    emit voicesChanged();
}

void Kokoro::setHealthy(bool healthy, const QString &status) {
    const bool changed = healthy != m_healthy || status != m_status;
    m_healthy = healthy;
    m_status = status;
    if (changed) {
        emit healthChanged();
        emit statusChanged();
    }
}

void Kokoro::checkHealth() {
    if (m_healthReply)
        return;
    QNetworkRequest request(QUrl(m_endpoint + "/health"));
    request.setTransferTimeout(5000);
    m_healthReply = m_manager->get(request);
    connect(m_healthReply, &QNetworkReply::finished, this, [this] {
        QNetworkReply *reply = m_healthReply;
        m_healthReply = nullptr;
        if (!reply)
            return;
        const bool ok = reply->error() == QNetworkReply::NoError
            && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200;
        const QJsonObject object = QJsonDocument::fromJson(reply->readAll()).object();
        if (ok && object.value("status").toString() == "healthy")
            setHealthy(true, tr("Kokoro is ready"));
        else
            setHealthy(false, tr("Kokoro is not reachable at %1").arg(m_endpoint));
        reply->deleteLater();
    });
}

void Kokoro::refreshVoices() {
    if (m_voicesReply)
        return;
    QNetworkRequest request(QUrl(m_endpoint + "/v1/audio/voices"));
    request.setTransferTimeout(8000);
    m_voicesReply = m_manager->get(request);
    connect(m_voicesReply, &QNetworkReply::finished, this, [this] {
        QNetworkReply *reply = m_voicesReply;
        m_voicesReply = nullptr;
        if (!reply)
            return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        if (reply->error() == QNetworkReply::NoError && status == 200) {
            const QJsonArray array = QJsonDocument::fromJson(body).object().value("voices").toArray();
            if (!array.isEmpty()) {
                QVariantList voices;
                for (const QJsonValue &value : array) {
                    const QString id = value.toObject().value("id").toString();
                    if (!id.isEmpty())
                        voices << voiceInfo(id);
                }
                std::sort(voices.begin(), voices.end(), [](const QVariant &a, const QVariant &b) {
                    const QVariantMap left = a.toMap(), right = b.toMap();
                    auto key = [](const QVariantMap &voice) {
                        const QString code = voice.value("languageCode").toString();
                        const QString rank = code == "en-US" ? "0" : code == "en-GB" ? "1" : "2" + code;
                        return rank + " " + voice.value("id").toString();
                    };
                    return key(left).localeAwareCompare(key(right)) < 0;
                });
                m_voices = voices;
                emit voicesChanged();
            }
        }
        reply->deleteLater();
    });
}

QNetworkReply *Kokoro::stream(const QString &text, const QString &voice, double speed, ChunkCallback onChunk,
                              DoneCallback onDone) {
    QJsonObject body;
    body["model"] = "kokoro";
    body["input"] = text;
    body["voice"] = voice;
    body["response_format"] = "pcm";
    body["speed"] = speed;
    body["stream"] = true;

    QNetworkRequest request(QUrl(m_endpoint + "/v1/audio/speech"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", "Bearer not-needed");
    // The timeout counts silence between chunks. A cold server loading its
    // model can take a while before the first one.
    request.setTransferTimeout(120000);
    QNetworkReply *reply = m_manager->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));

    // A chunked body may split a sample across two reads; keep the odd byte
    // for the next one.
    auto carry = std::make_shared<QByteArray>();
    auto ok = [reply] {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        return reply->error() == QNetworkReply::NoError && status == 200;
    };
    connect(reply, &QNetworkReply::readyRead, this, [reply, carry, ok, onChunk] {
        if (!ok())
            return;
        carry->append(reply->readAll());
        const qsizetype even = carry->size() & ~qsizetype(1);
        if (even == 0)
            return;
        const QByteArray samples = carry->left(even);
        carry->remove(0, even);
        onChunk(samples);
    });
    connect(reply, &QNetworkReply::finished, this, [reply, ok, onDone = std::move(onDone)] {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::OperationCanceledError)
            return;
        if (ok()) {
            onDone({});
            return;
        }
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray detailBody = reply->readAll();
        QString error = reply->errorString();
        const QString detail = QJsonDocument::fromJson(detailBody).object().value("detail").toString();
        if (!detail.isEmpty())
            error = detail;
        else if (status != 0)
            error = QObject::tr("Kokoro returned HTTP %1").arg(status);
        onDone(error);
    });
    return reply;
}
