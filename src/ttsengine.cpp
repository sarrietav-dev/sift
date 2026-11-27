#include "ttsengine.h"

#include "kokoro.h"
#include "speech.h"

#include <QAudioFormat>
#include <QAudioSink>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFileInfoList>
#include <QMediaDevices>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>

namespace {

constexpr qint64 BytesPerSecond = qint64(Kokoro::SampleRate) * 2;

QString audioDirectory() {
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/audio";
}

} // namespace

TtsEngine::TtsEngine(Kokoro *kokoro, QObject *parent)
    : QObject(parent),
      m_kokoro(kokoro) {
    m_pump.setInterval(20);
    connect(&m_pump, &QTimer::timeout, this, &TtsEngine::pump);
    QDir().mkpath(audioDirectory());
    pruneCache();
}

void TtsEngine::setStatus(const QString &status) {
    if (status == m_status)
        return;
    m_status = status;
    emit statusChanged();
}

void TtsEngine::setPlaying(bool playing) {
    if (playing == m_playing)
        return;
    m_playing = playing;
    emit playingChanged();
}

void TtsEngine::setBuffering(bool buffering) {
    if (buffering == m_buffering)
        return;
    m_buffering = buffering;
    emit bufferingChanged();
}

void TtsEngine::setCurrent(int index) {
    if (index == m_current)
        return;
    m_current = index;
    emit currentChanged();
    emit sentenceChanged(m_current);
}

QString TtsEngine::cleanForSpeech(const QString &text) {
    QString value = text;
    value.replace(QRegularExpression("\\[\\s*\\d+\\s*\\]"), "");
    value.replace(QRegularExpression("\\s+"), " ");
    return speech::spokenNumerals(value.trimmed());
}

QString TtsEngine::cachePath(const QString &text) const {
    const QString key = QStringLiteral("%1|%2|%3|kokoro-pcm").arg(m_voice, QString::number(m_speed, 'f', 2), text);
    const QString hash = QString::fromLatin1(
        QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256).toHex());
    return audioDirectory() + "/" + hash.left(2) + "/" + hash + ".pcm";
}

QString TtsEngine::itemText(int item) const {
    if (item == IntroIndex)
        return m_intro;
    if (item >= 0 && item < m_sentences.size())
        return m_sentences.at(item);
    return {};
}

int TtsEngine::itemAfter(int item) const {
    const int next = item == IntroIndex ? 0 : item + 1;
    return next < m_sentences.size() ? next : EndIndex;
}

void TtsEngine::setSentences(const QStringList &sentences, const QList<int> &blocks, int startSentence,
                             const QString &intro) {
    abortRequest();
    resetOutput();
    m_clips.clear();
    m_errors.clear();
    m_sentences.clear();
    m_blocks.clear();
    for (int i = 0; i < sentences.size(); ++i) {
        const QString clean = cleanForSpeech(sentences.at(i));
        if (clean.isEmpty())
            continue;
        m_sentences << clean;
        m_blocks << blocks.value(i, -1);
    }
    m_current = m_sentences.isEmpty() ? 0 : qBound(0, startSentence, m_sentences.size() - 1);
    // Announcing the article only makes sense from its beginning; picking up
    // where playback left off goes straight to the sentence.
    m_intro = cleanForSpeech(intro);
    m_introPending = !m_intro.isEmpty() && m_current == 0;
    setPlaying(false);
    setBuffering(false);
    setStatus(m_sentences.isEmpty() ? tr("No readable text.") : tr("Ready"));
    emit countChanged();
    emit currentChanged();
    emit sentenceChanged(m_current);

    // Have the opening clip ready before playback is asked for, so pressing
    // play rarely waits on a cold server.
    schedule();
}

void TtsEngine::setStartSentence(int index) {
    if (m_sentences.isEmpty())
        return;
    const int target = qBound(0, index, m_sentences.size() - 1);
    if (target == m_current)
        return;
    if (target != 0)
        m_introPending = false;
    setCurrent(target);
    if (!m_playing) {
        resetOutput();
        schedule();
    }
}

void TtsEngine::setVoice(const QString &voice) {
    const QString clean = voice.trimmed();
    if (clean.isEmpty() || clean == m_voice)
        return;
    m_voice = clean;
    emit voiceChanged();
    invalidateAudio();
}

void TtsEngine::setSpeed(double speed) {
    const double clean = qBound(0.5, speed, 2.0);
    if (qFuzzyCompare(clean, m_speed))
        return;
    m_speed = clean;
    emit speedChanged();
    invalidateAudio();
}

void TtsEngine::setCacheLimitMb(int megabytes) {
    m_cacheLimitMb = qMax(64, megabytes);
    pruneCache();
}

// A new voice or speed makes every clip stale. Playback starts again at the
// sentence being heard, in the new voice.
void TtsEngine::invalidateAudio() {
    abortRequest();
    m_clips.clear();
    m_errors.clear();
    if (m_playing && m_device) {
        const int item = m_heard != NoIndex ? m_heard : (m_introPending ? IntroIndex : m_current);
        startOutput(item);
        return;
    }
    resetOutput();
    schedule();
}

void TtsEngine::play() {
    if (m_sentences.isEmpty()) {
        setStatus(tr("There is nothing to read yet."));
        return;
    }
    // Pressing play again is the way to retry a sentence that failed.
    m_errors.clear();
    setPlaying(true);
    if (m_sink && m_device && m_sink->state() == QAudio::SuspendedState) {
        m_sink->resume();
        m_pump.start();
        schedule();
        pump();
        return;
    }
    startOutput(m_introPending ? IntroIndex : m_current);
}

void TtsEngine::pause() {
    setPlaying(false);
    if (m_sink && m_device)
        m_sink->suspend();
    m_pump.stop();
    setBuffering(false);
    setStatus(tr("Paused"));
}

void TtsEngine::toggle() {
    if (m_playing)
        pause();
    else
        play();
}

void TtsEngine::stop() {
    setPlaying(false);
    m_introPending = false;
    abortRequest();
    resetOutput();
    setBuffering(false);
    setStatus(tr("Ready"));
}

void TtsEngine::next() {
    if (m_sentences.isEmpty())
        return;
    m_introPending = false;
    if (m_current + 1 >= m_sentences.size()) {
        resetOutput();
        setPlaying(false);
        setBuffering(false);
        setStatus(tr("Finished"));
        emit articleFinished();
        return;
    }
    seek(m_current + 1);
}

void TtsEngine::previous() {
    if (m_sentences.isEmpty())
        return;
    m_introPending = false;
    seek(qMax(0, m_current - 1));
}

void TtsEngine::playFrom(int index) {
    if (m_sentences.isEmpty())
        return;
    m_introPending = false;
    m_errors.clear();
    setPlaying(true);
    seek(qBound(0, index, m_sentences.size() - 1));
}

void TtsEngine::seek(int index) {
    setCurrent(index);
    if (m_playing) {
        startOutput(index);
        return;
    }
    resetOutput();
    schedule();
}

// ---------------------------------------------------------------- playback

void TtsEngine::startOutput(int item) {
    resetOutput();
    if (!m_sink) {
        QAudioFormat format;
        format.setSampleRate(Kokoro::SampleRate);
        format.setChannelCount(1);
        format.setSampleFormat(QAudioFormat::Int16);
        m_sink = new QAudioSink(QMediaDevices::defaultAudioOutput(), format, this);
        // A quarter second in the sink keeps the highlight close to the voice
        // and a jump quick, with room for the pump's 20 ms ticks.
        m_sink->setBufferSize(int(BytesPerSecond / 4));
    }
    m_device = m_sink->start();
    if (!m_device) {
        setPlaying(false);
        const QString message = tr("No audio output is available.");
        setStatus(message);
        emit error(message);
        return;
    }
    m_feeding = item;
    m_timeline << Placed{item, 0, -1};
    m_pump.start();
    schedule();
    pump();
}

void TtsEngine::resetOutput() {
    m_pump.stop();
    if (m_sink && m_sink->state() != QAudio::StoppedState)
        m_sink->stop();
    m_device = nullptr;
    m_timeline.clear();
    m_written = 0;
    m_feeding = NoIndex;
    m_feedOffset = 0;
    m_heard = NoIndex;
    m_drainedTicks = 0;
}

// Audio that has left the sink's buffer for the device.
qint64 TtsEngine::playedBytes() const {
    if (!m_sink || !m_device)
        return 0;
    const qint64 queued = qMax<qint64>(0, m_sink->bufferSize() - m_sink->bytesFree());
    return qMax<qint64>(0, m_written - queued);
}

// Writes what the sink has room for. True when at least one sentence was
// written in full and playback moved on to the next.
bool TtsEngine::writeAudio() {
    bool advanced = false;
    while (m_feeding != EndIndex && m_feeding != NoIndex) {
        const auto clip = m_clips.constFind(m_feeding);
        if (clip == m_clips.constEnd())
            break;
        const qint64 available = clip->pcm.size() - m_feedOffset;
        if (available > 0) {
            const qint64 room = m_sink->bytesFree() & ~qint64(1);
            if (room <= 0)
                break;
            const qint64 wrote = m_device->write(clip->pcm.constData() + m_feedOffset, qMin(available, room));
            if (wrote <= 0)
                break;
            m_feedOffset += wrote;
            m_written += wrote;
            continue;
        }
        if (!clip->complete)
            break;
        // The whole sentence is in the sink; the next one follows with no gap.
        m_timeline.last().end = m_written;
        m_feeding = itemAfter(m_feeding);
        m_feedOffset = 0;
        if (m_feeding != EndIndex)
            m_timeline << Placed{m_feeding, m_written, -1};
        advanced = true;
    }
    return advanced;
}

// Runs every 20 ms while playing and whenever audio arrives: writes audio,
// then works out which sentence is being heard.
void TtsEngine::pump() {
    if (!m_device)
        return;

    // Each sentence finished moves the window, and schedule() may then load
    // the next one from the cache, so write again until nothing moves.
    while (writeAudio())
        schedule();

    const qint64 played = playedBytes();
    int heard = NoIndex;
    for (const Placed &placed : std::as_const(m_timeline)) {
        if (placed.start > played)
            break;
        heard = placed.item;
    }
    if (heard != NoIndex && heard != m_heard) {
        m_heard = heard;
        if (heard >= 0) {
            m_introPending = false;
            setCurrent(heard);
        }
    }

    const bool drained = played >= m_written;
    m_drainedTicks = drained ? m_drainedTicks + 1 : 0;
    if (m_feeding == EndIndex) {
        // Let the device play out the last words before closing the stream.
        if (drained && (m_sink->state() == QAudio::IdleState || m_drainedTicks > 15)) {
            resetOutput();
            setPlaying(false);
            setBuffering(false);
            setStatus(tr("Finished"));
            emit articleFinished();
        }
        return;
    }
    if (drained && m_errors.contains(m_feeding)) {
        const QString message = m_errors.value(m_feeding);
        resetOutput();
        setPlaying(false);
        setBuffering(false);
        setStatus(message);
        emit error(message);
        return;
    }
    if (!m_playing)
        return;
    setBuffering(drained);
    if (drained)
        setStatus(m_feeding == IntroIndex ? tr("Preparing the title…")
                                          : tr("Preparing sentence %1…").arg(m_feeding + 1));
    else
        setStatus(tr("Reading"));
}

// ---------------------------------------------------------------- synthesis

// What playback needs, nearest first: the sentence being written to the sink
// and the window after it. Before playback starts, only the opening clip.
QList<int> TtsEngine::wantedItems() const {
    QList<int> items;
    if (m_sentences.isEmpty())
        return items;
    const bool started = m_feeding != NoIndex;
    const int base = started ? m_feeding : (m_introPending ? IntroIndex : m_current);
    if (base == EndIndex)
        return items;
    items << base;
    if (base == IntroIndex)
        items << 0;
    if (!started)
        return items;
    for (int index : prefetchIndices(m_blocks, base == IntroIndex ? 0 : base))
        items << index;
    return items;
}

// One request at a time, for the nearest sentence without audio. A request
// for something further away gives way to a nearer one, and a request for a
// sentence no longer wanted is dropped.
void TtsEngine::schedule() {
    const QList<int> wanted = wantedItems();
    for (auto it = m_clips.begin(); it != m_clips.end();) {
        if (!wanted.contains(it.key()) && it.key() != m_requestItem)
            it = m_clips.erase(it);
        else
            ++it;
    }
    if (m_request && !wanted.contains(m_requestItem))
        abortRequest();

    for (int item : wanted) {
        if (item == m_requestItem)
            break;
        if (m_clips.contains(item) || m_errors.contains(item) || loadCached(item))
            continue;
        abortRequest();
        request(item);
        break;
    }
}

bool TtsEngine::loadCached(int item) {
    QFile file(cachePath(itemText(item)));
    if (!file.open(QIODevice::ReadWrite) || file.size() <= 0)
        return false;
    m_clips.insert(item, Clip{file.readAll(), true});
    // The cache is pruned oldest first; a clip that is played again is new.
    file.setFileTime(QDateTime::currentDateTime(), QFileDevice::FileModificationTime);
    return true;
}

void TtsEngine::request(int item) {
    const QString text = itemText(item);
    const QString path = cachePath(text);
    m_clips.insert(item, Clip{});
    m_requestItem = item;
    m_request = m_kokoro->stream(
        text, m_voice, m_speed,
        [this, item](const QByteArray &pcm) {
            const auto clip = m_clips.find(item);
            if (clip == m_clips.end())
                return;
            clip->pcm.append(pcm);
            if (item == m_feeding)
                pump();
        },
        [this, item, path](const QString &error) {
            m_request = nullptr;
            m_requestItem = NoIndex;
            const auto clip = m_clips.find(item);
            if (!error.isEmpty()) {
                m_clips.remove(item);
                failed(item, error);
            } else if (clip != m_clips.end()) {
                clip->complete = true;
                if (!clip->pcm.isEmpty()) {
                    QDir().mkpath(QFileInfo(path).absolutePath());
                    QSaveFile file(path);
                    if (file.open(QIODevice::WriteOnly) && file.write(clip->pcm) == clip->pcm.size())
                        file.commit();
                    if (++m_saves % 40 == 0)
                        pruneCache();
                }
            }
            schedule();
            pump();
        });
}

void TtsEngine::abortRequest() {
    if (!m_request)
        return;
    QNetworkReply *reply = m_request;
    m_clips.remove(m_requestItem);
    m_request = nullptr;
    m_requestItem = NoIndex;
    reply->abort();
}

// A failed clip is not asked for again until play is pressed. Playback stops
// when it reaches the sentence, so the one already playing finishes first.
void TtsEngine::failed(int item, const QString &message) {
    if (item == IntroIndex) {
        // A title that cannot be spoken should not hold up the article.
        m_introPending = false;
        if (m_feeding == IntroIndex) {
            m_feeding = itemAfter(IntroIndex);
            m_feedOffset = 0;
            m_timeline.last().end = m_written;
            if (m_feeding != EndIndex)
                m_timeline << Placed{m_feeding, m_written, -1};
        }
        return;
    }
    m_errors.insert(item, message);
}

void TtsEngine::pruneCache() {
    const QString directory = audioDirectory();
    if (!QDir(directory).exists())
        return;
    QFileInfoList files;
    // MP3s are what earlier versions cached; they age out with the rest.
    QDirIterator iterator(directory, QStringList{"*.pcm", "*.mp3"}, QDir::Files, QDirIterator::Subdirectories);
    qint64 total = 0;
    while (iterator.hasNext()) {
        const QFileInfo info(iterator.next());
        total += info.size();
        files << info;
    }
    const qint64 limit = qint64(m_cacheLimitMb) * 1024 * 1024;
    if (total <= limit)
        return;
    std::sort(files.begin(), files.end(), [](const QFileInfo &a, const QFileInfo &b) {
        return a.lastModified() < b.lastModified();
    });
    for (const QFileInfo &info : std::as_const(files)) {
        if (total <= limit)
            break;
        total -= info.size();
        QFile::remove(info.absoluteFilePath());
    }
}
