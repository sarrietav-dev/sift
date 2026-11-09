#pragma once

#include <QObject>
#include <QVariantList>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

// Small client for the local Kokoro-FastAPI server. It knows how to check the
// service, list voices, and turn one sentence into an MP3 file.
class Kokoro : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString endpoint READ endpoint WRITE setEndpoint NOTIFY endpointChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool healthy READ healthy NOTIFY healthChanged)
    Q_PROPERTY(QVariantList voices READ voices NOTIFY voicesChanged)

public:
    using SynthCallback = std::function<void(const QString &path, const QString &error)>;

    explicit Kokoro(QObject *parent = nullptr);

    QString endpoint() const { return m_endpoint; }
    void setEndpoint(const QString &endpoint);
    QString status() const { return m_status; }
    bool healthy() const { return m_healthy; }
    QVariantList voices() const { return m_voices; }

    Q_INVOKABLE void checkHealth();
    Q_INVOKABLE void refreshVoices();

    void synthesize(const QString &text, const QString &voice, double speed, const QString &path,
                    SynthCallback callback);

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
