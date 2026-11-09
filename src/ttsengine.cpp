#include "ttsengine.h"

#include "kokoro.h"

#include <QAudioOutput>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFileInfoList>
#include <QMediaPlayer>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>
#include <QVariant>

TtsEngine::TtsEngine(Kokoro *kokoro, QObject *parent)
    : QObject(parent),
      m_kokoro(kokoro),
      m_player(new QMediaPlayer(this)),
      m_audio(new QAudioOutput(this)) {
    m_player->setAudioOutput(m_audio);
    m_audio->setVolume(1.0);

    connect(m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::EndOfMedia) {
            if (m_playing)
                next();
            else
                setBuffering(false);
        } else if (status == QMediaPlayer::InvalidMedia) {
            setBuffering(false);
            setPlaying(false);
            setStatus(tr("This audio could not be played."));
        }
    });
    connect(m_player, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState state) {
        if (state == QMediaPlayer::PlayingState) {
            setBuffering(false);
            setStatus(tr("Reading"));
        } else if (state == QMediaPlayer::PausedState) {
            setStatus(tr("Paused"));
        }
    });
    connect(m_player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString &message) {
        setBuffering(false);
        setStatus(message.isEmpty() ? tr("Audio playback failed.") : message);
    });

    QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/audio");
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

QString TtsEngine::cleanForSpeech(const QString &text) const {
    QString value = text;
    value.replace(QRegularExpression("\\[\\s*\\d+\\s*\\]"), "");
    value.replace(QRegularExpression("\\s+"), " ");
    return value.trimmed();
}

QString TtsEngine::cachePath(const QString &text) const {
    const QString key = QStringLiteral("%1|%2|%3|kokoro").arg(m_voice, QString::number(m_speed, 'f', 2), text);
    const QString hash = QString::fromLatin1(
        QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256).toHex());
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
        + "/audio/" + hash.left(2);
    QDir().mkpath(directory);
    return directory + "/" + hash + ".mp3";
}

void TtsEngine::setSentences(const QStringList &sentences, int startSentence) {
    ++m_generation;
    m_player->stop();
    m_sentences.clear();
    for (const QString &sentence : sentences) {
        const QString clean = cleanForSpeech(sentence);
        if (!clean.isEmpty())
            m_sentences << clean;
    }
    m_ready.clear();
    m_pending.clear();
    m_errors.clear();
    m_sourceIndex = -1;
    m_current = m_sentences.isEmpty() ? 0 : qBound(0, startSentence, m_sentences.size() - 1);
    setPlaying(false);
    setBuffering(false);
    setStatus(m_sentences.isEmpty() ? tr("No readable text.") : tr("Ready"));
    emit countChanged();
    emit currentChanged();
    emit sentenceChanged(m_current);
}

void TtsEngine::setStartSentence(int index) {
    if (m_sentences.isEmpty())
        return;
    const int target = qBound(0, index, m_sentences.size() - 1);
    if (target == m_current)
        return;
    m_current = target;
    emit currentChanged();
    emit sentenceChanged(m_current);
}

void TtsEngine::setVoice(const QString &voice) {
    const QString clean = voice.trimmed();
    if (clean.isEmpty() || clean == m_voice)
        return;
    ++m_generation;
    m_voice = clean;
    emit voiceChanged();
    invalidateAudio();
}

void TtsEngine::setSpeed(double speed) {
    const double clean = qBound(0.5, speed, 2.0);
    if (qFuzzyCompare(clean, m_speed))
        return;
    ++m_generation;
    m_speed = clean;
    emit speedChanged();
    invalidateAudio();
}

void TtsEngine::setCacheLimitMb(int megabytes) {
    m_cacheLimitMb = qMax(64, megabytes);
    pruneCache();
}

void TtsEngine::invalidateAudio() {
    m_player->stop();
    m_ready.clear();
    m_pending.clear();
    m_errors.clear();
    setBuffering(false);
    if (m_playing)
        playCurrentIfReady();
}

void TtsEngine::play() {
    if (m_sentences.isEmpty()) {
        setStatus(tr("There is nothing to read yet."));
        return;
    }
    if (m_player->playbackState() == QMediaPlayer::PausedState
        && m_sourceIndex == m_current && !m_player->source().isEmpty()) {
        setPlaying(true);
        m_player->play();
        setStatus(tr("Reading"));
        return;
    }
    setPlaying(true);
    playCurrentIfReady();
    prefetch();
}

void TtsEngine::pause() {
    setPlaying(false);
    m_player->pause();
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
    m_sourceIndex = -1;
    m_player->stop();
    setBuffering(false);
    setStatus(tr("Ready"));
}

void TtsEngine::next() {
    if (m_sentences.isEmpty())
        return;
    if (m_current + 1 >= m_sentences.size()) {
        setPlaying(false);
        m_player->stop();
        setBuffering(false);
        setStatus(tr("Finished"));
        emit articleFinished();
        return;
    }
    ++m_current;
    emit currentChanged();
    emit sentenceChanged(m_current);
    if (m_playing)
        playCurrentIfReady();
}

void TtsEngine::previous() {
    if (m_sentences.isEmpty())
        return;
    if (m_current == 0) {
        m_player->stop();
        setBuffering(false);
        if (m_playing)
            playCurrentIfReady();
        return;
    }
    --m_current;
    emit currentChanged();
    emit sentenceChanged(m_current);
    if (m_playing)
        playCurrentIfReady();
}

void TtsEngine::playFrom(int index) {
    if (m_sentences.isEmpty())
        return;
    const int target = qBound(0, index, m_sentences.size() - 1);
    ++m_generation;
    m_pending.clear();
    m_errors.clear();
    m_sourceIndex = -1;
    m_player->stop();
    m_current = target;
    setPlaying(true);
    emit currentChanged();
    emit sentenceChanged(m_current);
    playCurrentIfReady();
    prefetch();
}

void TtsEngine::playCurrentIfReady() {
    if (m_sentences.isEmpty())
        return;
    const QString path = cachePath(m_sentences.at(m_current));
    if (QFileInfo::exists(path) && QFileInfo(path).size() > 0) {
        playFile(path);
        return;
    }
    setBuffering(true);
    setStatus(tr("Preparing sentence %1…").arg(m_current + 1));
    ensureAudio(m_current);
    prefetch();
}

void TtsEngine::playFile(const QString &path) {
    setBuffering(false);
    setStatus(tr("Reading"));
    m_sourceIndex = m_current;
    m_player->setSource(QUrl::fromLocalFile(path));
    m_player->play();
}

void TtsEngine::ensureAudio(int index) {
    if (index < 0 || index >= m_sentences.size())
        return;
    if (m_ready.contains(index) || m_pending.contains(index))
        return;
    const QString text = m_sentences.at(index);
    const QString path = cachePath(text);
    if (QFileInfo::exists(path) && QFileInfo(path).size() > 0) {
        m_ready.insert(index);
        if (index == m_current && m_playing)
            playFile(path);
        return;
    }
    m_pending.insert(index);
    const int generation = m_generation;
    m_kokoro->synthesize(text, m_voice, m_speed, path, [this, index, generation, path](const QString &saved,
                                                                                        const QString &error) {
        if (generation != m_generation)
            return;
        m_pending.remove(index);
        if (!error.isEmpty()) {
            m_errors.insert(index, error);
            if (index == m_current) {
                setBuffering(false);
                setPlaying(false);
                setStatus(error);
                emit this->error(error);
            }
            return;
        }
        m_ready.insert(index);
        if (index == m_current && m_playing)
            playFile(saved.isEmpty() ? path : saved);
    });
}

void TtsEngine::prefetch() {
    if (m_sentences.isEmpty())
        return;
    for (int i = m_current + 1; i <= m_current + 2 && i < m_sentences.size(); ++i)
        ensureAudio(i);
}

void TtsEngine::pruneCache() {
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/audio";
    QDir root(directory);
    if (!root.exists())
        return;
    QFileInfoList files;
    QDirIterator iterator(directory, QStringList{"*.mp3"}, QDir::Files, QDirIterator::Subdirectories);
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
    for (const QFileInfo &info : files) {
        if (total <= limit)
            break;
        total -= info.size();
        QFile::remove(info.absoluteFilePath());
    }
}
