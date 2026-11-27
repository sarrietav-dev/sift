#pragma once

#include <QByteArray>
#include <QCoreApplication>
#include <QHash>
#include <QObject>
#include <QStringList>
#include <QTimer>

class Kokoro;
class QAudioSink;
class QIODevice;
class QNetworkReply;

// Sentence-by-sentence playback. Kokoro streams each sentence as PCM, which
// goes straight into one open audio sink, so speech starts as the server makes
// it and sentences follow each other without a gap. Audio is cached by voice,
// speed, and text.
//
// Kokoro runs concurrent requests together and every one of them slows down,
// so only one sentence is synthesized at a time, nearest first. A request for
// a sentence playback has moved away from is aborted.
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

    // Sentence indices to keep ready: the rest of this paragraph, then the
    // whole next one. Headings, figure captions, and list items are paragraphs
    // too, and most hold a single sentence, so the window keeps crossing
    // blocks until it holds enough sentences to cover the reader while the
    // next clips synthesize. They are synthesized one at a time in this order.
    // Kept here so the window can be tested without a server.
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
        constexpr int minimumAhead = 6;
        int targetBlock = currentBlock;
        bool nextParagraph = false;
        for (int i = current + 1; i < blocks.size() && i - current <= 48; ++i) {
            const int block = blocks.at(i);
            if (block >= 0 && block != targetBlock) {
                if (nextParagraph && indices.size() >= minimumAhead)
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

    // Citation markers removed, whitespace collapsed, Roman numerals after a
    // label spelled out. This text is what Kokoro hears and what the cache is
    // keyed by.
    static QString cleanForSpeech(const QString &text);

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
    // sentence indices keep matching the reader's spans.
    static constexpr int IntroIndex = -2;
    // Past the last sentence: nothing more to write to the sink.
    static constexpr int EndIndex = -3;
    static constexpr int NoIndex = -1;

    // A sentence's audio, complete or still arriving.
    struct Clip {
        QByteArray pcm;
        bool complete = false;
    };
    // Where a sentence sits in the audio written to the sink since it opened.
    struct Placed {
        int item;
        qint64 start;
        qint64 end; // -1 while the sentence is still being written
    };

    void setPlaying(bool playing);
    void setBuffering(bool buffering);
    void setStatus(const QString &status);
    void setCurrent(int index);

    QString itemText(int item) const;
    int itemAfter(int item) const;
    QString cachePath(const QString &text) const;
    void pruneCache();

    // Playback.
    void seek(int index);
    void invalidateAudio();
    void startOutput(int item);
    void resetOutput();
    void pump();
    bool writeAudio();
    qint64 playedBytes() const;

    // Synthesis.
    QList<int> wantedItems() const;
    void schedule();
    bool loadCached(int item);
    void request(int item);
    void abortRequest();
    void clearAudio();
    void failed(int item, const QString &message);

    Kokoro *m_kokoro = nullptr;
    QStringList m_sentences;
    QList<int> m_blocks;
    QString m_intro;
    bool m_introPending = false;

    QHash<int, Clip> m_clips;
    QHash<int, QString> m_errors;
    QNetworkReply *m_request = nullptr;
    int m_requestItem = NoIndex;

    QAudioSink *m_sink = nullptr;
    QIODevice *m_device = nullptr;
    QTimer m_pump;
    QList<Placed> m_timeline;
    qint64 m_written = 0;
    int m_feeding = NoIndex; // the item being written to the sink
    qint64 m_feedOffset = 0; // bytes of it written so far
    int m_heard = NoIndex;   // the item coming out of the speakers
    int m_drainedTicks = 0;
    int m_saves = 0;

    QString m_voice = "af_heart";
    double m_speed = 1.0;
    int m_current = 0;
    int m_cacheLimitMb = 1024;
    bool m_playing = false;
    bool m_buffering = false;
    QString m_status = tr("Ready");
};
