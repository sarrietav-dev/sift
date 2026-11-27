#pragma once

#include <QObject>
#include <QVariantList>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

// Small client for the local Kokoro-FastAPI server. It knows how to check the
// service, list voices, and stream one sentence as raw PCM.
class Kokoro : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString endpoint READ endpoint WRITE setEndpoint NOTIFY endpointChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool healthy READ healthy NOTIFY healthChanged)
    Q_PROPERTY(QVariantList voices READ voices NOTIFY voicesChanged)

public:
    using ChunkCallback = std::function<void(const QByteArray &pcm)>;
    using DoneCallback = std::function<void(const QString &error)>;

    // Kokoro speaks 16-bit mono PCM at this rate.
    static constexpr int SampleRate = 24000;

    explicit Kokoro(QObject *parent = nullptr);

    QString endpoint() const { return m_endpoint; }
    void setEndpoint(const QString &endpoint);
    QString status() const { return m_status; }
    bool healthy() const { return m_healthy; }
    QVariantList voices() const { return m_voices; }

    Q_INVOKABLE void checkHealth();
    Q_INVOKABLE void refreshVoices();

    // Ask Kokoro to speak `text` and hand each piece of audio to `onChunk` as
    // the server makes it, always a whole number of samples. `onDone` runs
    // once at the end with an empty error on success. Aborting the returned
    // reply calls neither callback again, and the server stops after the
    // segment it is working on.
    QNetworkReply *stream(const QString &text, const QString &voice, double speed, ChunkCallback onChunk,
                          DoneCallback onDone);

    static QVariantMap voiceInfo(const QString &id);

signals:
    void endpointChanged();
    void statusChanged();
    void healthChanged();
    void voicesChanged();

private:
    void setHealthy(bool healthy, const QString &status);
    void loadDefaultVoices();

    QNetworkAccessManager *m_manager = nullptr;
    QString m_endpoint = "http://127.0.0.1:8880";
    QString m_status = "Checking Kokoro…";
    bool m_healthy = false;
    QVariantList m_voices;
    QNetworkReply *m_healthReply = nullptr;
    QNetworkReply *m_voicesReply = nullptr;
};
