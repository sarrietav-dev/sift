#pragma once

#include <QCoreApplication>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QStringList>

class Kokoro;
class QAudioOutput;
class QMediaPlayer;

// Sentence-by-sentence playback. Synthesizes through Kokoro, stores each MP3
// under a content-addressed cache, and prefetches the next two sentences so
// playback can move without waiting.
class TtsEngine : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(bool buffering READ buffering NOTIFY bufferingChanged)
    Q_PROPERTY(int current READ current NOTIFY currentChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(QString voice READ voice WRITE setVoice NOTIFY voiceChanged)
    Q_PROPERTY(double speed READ speed WRITE setSpeed NOTIFY speedChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)

public:
    explicit TtsEngine(Kokoro *kokoro, QObject *parent = nullptr);

    bool playing() const { return m_playing; }
    bool buffering() const { return m_buffering; }
    int current() const { return m_current; }
    int count() const { return m_sentences.size(); }
    QString voice() const { return m_voice; }
    double speed() const { return m_speed; }
    QString status() const { return m_status; }

    // Sentence indices to preload: the rest of this paragraph, then the whole
    // next one. Kept here so the window can be tested without a server.
    static QList<int> prefetchIndices(const QList<int> &blocks, int current) {
        QList<int> indices;
        if (current < 0 || current >= blocks.size())
            return indices;
        const int currentBlock = blocks.at(current);
        if (currentBlock < 0) {
            for (int i = current + 1; i <= current + 8 && i < blocks.size(); ++i)
                indices << i;
            return indices;
        }
        int targetBlock = currentBlock;
        bool nextParagraph = false;
        for (int i = current + 1; i < blocks.size() && i - current <= 48; ++i) {
            const int block = blocks.at(i);
            if (block >= 0 && block != targetBlock) {
                if (nextParagraph)
                    break;
                nextParagraph = true;
                targetBlock = block;
            }
            indices << i;
        }
        return indices;
    }

    // The way an article introduces itself before its first sentence. The
    // sentence break is deliberate: it gives the title a pause before the
    // byline instead of running the two together.
    static QString announcement(const QString &title, const QString &author) {
        const QString cleanTitle = title.simplified();
        if (cleanTitle.isEmpty())
            return {};
        const QString cleanAuthor = author.simplified();
        if (cleanAuthor.isEmpty())
            return QCoreApplication::translate("TtsEngine", "%1.").arg(cleanTitle);
        return QCoreApplication::translate("TtsEngine", "%1. By %2.").arg(cleanTitle, cleanAuthor);
    }

    // `intro` is spoken once before the first sentence when playback starts at
    // the top of an article; resuming halfway through skips it.
    void setSentences(const QStringList &sentences, const QList<int> &blocks = {}, int startSentence = 0,
                      const QString &intro = {});
    void setStartSentence(int index);
    void setVoice(const QString &voice);
    void setSpeed(double speed);
    void setCacheLimitMb(int megabytes);

    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void toggle();
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    Q_INVOKABLE void playFrom(int index);
    Q_INVOKABLE void stop();

signals:
    void playingChanged();
    void bufferingChanged();
    void currentChanged();
    void countChanged();
    void voiceChanged();
    void speedChanged();
    void statusChanged();
    void sentenceChanged(int index);
    void articleFinished();
    void error(const QString &message);

private:
    // The intro is spoken before sentence 0 without becoming a sentence, so
    // sentence indices keep matching the reader's spans. It uses its own
    // source index because the player has no sentence to report while it runs.
    static constexpr int IntroIndex = -2;

    void setPlaying(bool playing);
    void setBuffering(bool buffering);
    void setStatus(const QString &status);
    void ensureAudio(int index);
    void ensureIntroAudio();
    void playCurrentIfReady();
    void playIntro();
    void playFile(const QString &path, int index);
    void prefetch();
    void invalidateAudio();
    QString cachePath(const QString &text) const;
    void pruneCache();
    QString cleanForSpeech(const QString &text) const;

    Kokoro *m_kokoro = nullptr;
    QMediaPlayer *m_player = nullptr;
    QAudioOutput *m_audio = nullptr;
    QStringList m_sentences;
    QList<int> m_blocks;
    QSet<int> m_ready;
    QSet<int> m_pending;
    QHash<int, QString> m_errors;
    QString m_intro;
    bool m_introPending = false;
    bool m_introSynthesizing = false;
    QString m_voice = "af_heart";
    double m_speed = 1.0;
    int m_current = 0;
    int m_sourceIndex = -1;
    int m_generation = 0;
    int m_cacheLimitMb = 1024;
    bool m_playing = false;
    bool m_buffering = false;
    QString m_status = tr("Ready");
};
