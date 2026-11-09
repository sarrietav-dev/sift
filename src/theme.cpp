// Theme watching follows Omacut (MIT, David Heinemeier Hansson).
#include "theme.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <cmath>

namespace {
QString colorOr(const QString &value, const QString &fallback)
{
    static const QRegularExpression valid("^#[0-9a-fA-F]{6}$");
    return valid.match(value).hasMatch() ? value.toLower() : fallback;
}

QString blend(const QString &background, const QString &foreground, double amount)
{
    const QColor bg(background), fg(foreground);
    return QColor::fromRgbF(bg.redF() * (1 - amount) + fg.redF() * amount,
                            bg.greenF() * (1 - amount) + fg.greenF() * amount,
                            bg.blueF() * (1 - amount) + fg.blueF() * amount).name();
}
}

Theme::Theme(const QString &directory, QObject *parent, const QString &hyprctl)
    : QObject(parent),
      m_directory(directory.isEmpty() ? QDir::homePath() + "/.local/state/omarchy/current" : directory) {
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(80);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_debounce.start(); });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { m_debounce.start(); });
    connect(&m_debounce, &QTimer::timeout, this, &Theme::reload);

    m_hyprctl = !hyprctl.isEmpty() ? hyprctl
              : directory.isEmpty() && !qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE")
                  ? QStandardPaths::findExecutable("hyprctl") : QString();
    m_roundingTimeout.setSingleShot(true);
    m_roundingTimeout.setInterval(500);
    connect(&m_roundingTimeout, &QTimer::timeout, &m_roundingProcess, &QProcess::kill);
    connect(&m_roundingProcess, &QProcess::errorOccurred, this, [this] { m_roundingTimeout.stop(); });
    connect(&m_roundingProcess, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        m_roundingTimeout.stop();
        const auto value = QJsonDocument::fromJson(m_roundingProcess.readAllStandardOutput()).object().value("int");
        if (code != 0 || status != QProcess::NormalExit || !value.isDouble() || value.toInt(-1) < 0)
            return;
        if (value.toInt() != m_radius) {
            m_radius = value.toInt();
            emit changed();
        }
    });
    m_roundingPoll.setInterval(1000);
    connect(&m_roundingPoll, &QTimer::timeout, this, &Theme::refreshRounding);
    if (!m_hyprctl.isEmpty())
        m_roundingPoll.start();
    reload();
}

Theme::~Theme() {
    if (m_roundingProcess.state() != QProcess::NotRunning) {
        m_roundingProcess.kill();
        m_roundingProcess.waitForFinished(1000);
    }
}

void Theme::refreshRounding() {
    if (m_hyprctl.isEmpty() || m_roundingProcess.state() != QProcess::NotRunning)
        return;
    m_roundingProcess.start(m_hyprctl, {"-j", "getoption", "decoration:rounding"});
    m_roundingTimeout.start();
}

QString Theme::readValue(const QString &path, const QString &key, const QString &fallback) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return fallback;
    const QString text = QString::fromUtf8(file.readAll());
    const QRegularExpression expression(
        QStringLiteral(R"(^\s*%1\s*=\s*["']([^"']+)["'])").arg(QRegularExpression::escape(key)),
        QRegularExpression::MultilineOption);
    const auto match = expression.match(text);
    return match.hasMatch() ? match.captured(1) : fallback;
}

QString Theme::readAccent(const QString &path) {
    return colorOr(readValue(path, "accent"), "#FFD60A");
}

// Black or white, whichever reads on the accent (WCAG relative luminance).
QString Theme::accentForeground() const {
    const QColor c(m_accent);
    auto linear = [](double v) { return v <= .04045 ? v / 12.92 : std::pow((v + .055) / 1.055, 2.4); };
    const double luminance = .2126 * linear(c.redF()) + .7152 * linear(c.greenF()) + .0722 * linear(c.blueF());
    return luminance > .179 ? "black" : "white";
}

void Theme::reload() {
    refreshRounding();
    const auto paths = m_watcher.files() + m_watcher.directories();
    if (!paths.isEmpty())
        m_watcher.removePaths(paths);

    QString ancestor = m_directory;
    while (!QFileInfo::exists(ancestor) && ancestor != "/")
        ancestor = QFileInfo(ancestor).absolutePath();
    QStringList candidates{ancestor, QFileInfo(ancestor).absolutePath(), m_directory,
                           m_directory + "/theme", m_directory + "/theme/colors.toml"};
    candidates.removeDuplicates();
    for (const auto &path : candidates)
        if (QFileInfo::exists(path))
            m_watcher.addPath(path);

    const QString colors = m_directory + "/theme/colors.toml";
    const QString mode = readValue(colors, "mode", "dark");
    const QString accent = readAccent(colors);
    const QString background = colorOr(readValue(colors, "background"), m_background);
    const QString foreground = colorOr(readValue(colors, "foreground"), m_foreground);
    const QString muted = colorOr(readValue(colors, "muted"), m_muted);
    const QString selection = colorOr(readValue(colors, "selection"), m_selection);
    const QString red = colorOr(readValue(colors, "red"), m_red);
    const QString green = colorOr(readValue(colors, "green"), m_green);
    const QString darkBackground = colorOr(readValue(colors, "dark_background"), blend(background, foreground, 0.08));
    const QString lighterBackground = colorOr(readValue(colors, "lighter_background"), blend(background, foreground, 0.08));
    const QString darkerBackground = colorOr(readValue(colors, "darker_background"), blend(background, foreground, 0.2));
    const bool isDark = mode == "dark";
    const QString panel = isDark ? lighterBackground : darkBackground;
    const QString border = isDark ? blend(background, foreground, 0.2) : darkerBackground;

    const bool colorChanged = accent != m_accent || background != m_background || foreground != m_foreground
        || muted != m_muted || selection != m_selection || panel != m_panel || border != m_border
        || red != m_red || green != m_green || mode != m_mode;
    if (!colorChanged)
        return;

    m_accent = accent;
    m_background = background;
    m_foreground = foreground;
    m_muted = muted;
    m_selection = selection;
    m_panel = panel;
    m_border = border;
    m_red = red;
    m_green = green;
    m_mode = mode;
    emit changed();
}
