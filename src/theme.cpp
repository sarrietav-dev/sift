// Theme watching follows Omacut (MIT, David Heinemeier Hansson).
#include "theme.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QStyleHints>
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
            emit targetChanged();
        }
    });
    m_roundingPoll.setInterval(1000);
    connect(&m_roundingPoll, &QTimer::timeout, this, &Theme::refreshRounding);
    if (!m_hyprctl.isEmpty())
        m_roundingPoll.start();

    m_omarchy = Palette{"#FFD60A", "#1a1a1a", "#f0f0f0", "#888888", "#222222", "#333333",
                        "#444444", "#D14D41", "#879A39", "dark"};
    m_shown = m_from = m_to = m_omarchy;
    m_fade.setDuration(240);
    m_fade.setEasingCurve(QEasingCurve::OutCubic);
    m_fade.setStartValue(0.0);
    m_fade.setEndValue(1.0);
    connect(&m_fade, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        const double t = value.toDouble();
        m_shown = Palette{blend(m_from.accent, m_to.accent, t), blend(m_from.background, m_to.background, t),
                          blend(m_from.foreground, m_to.foreground, t), blend(m_from.muted, m_to.muted, t),
                          blend(m_from.panel, m_to.panel, t), blend(m_from.border, m_to.border, t),
                          blend(m_from.selection, m_to.selection, t), blend(m_from.red, m_to.red, t),
                          blend(m_from.green, m_to.green, t), m_to.mode};
        emit changed();
    });
    connect(&m_fade, &QVariantAnimation::finished, this, [this] {
        m_shown = m_to;
        emit changed();
    });
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this] {
        if (m_appearance == "system")
            apply();
    });
    reload();
}

Theme::Palette Theme::builtIn(bool dark) {
    if (dark)
        return Palette{"#a68af9", "#1e1e1e", "#dadada", "#a3a3a3", "#262626", "#363636",
                       "#3f3a5c", "#fb464c", "#44cf6e", "dark"};
    return Palette{"#7852ee", "#ffffff", "#222222", "#6b6b6b", "#f5f5f5", "#e3e3e3",
                   "#e0dafd", "#e93147", "#08b94e", "light"};
}

Theme::Palette Theme::effective() const {
    if (m_appearance == "light")
        return builtIn(false);
    if (m_appearance == "dark")
        return builtIn(true);
    if (m_appearance == "system") {
        const Qt::ColorScheme scheme = QGuiApplication::styleHints()->colorScheme();
        if (scheme == Qt::ColorScheme::Unknown)
            return builtIn(m_omarchy.mode == "dark");
        return builtIn(scheme == Qt::ColorScheme::Dark);
    }
    return m_omarchy;
}

void Theme::setAppearance(const QString &appearance) {
    static const QStringList known{"omarchy", "system", "light", "dark"};
    const QString clean = known.contains(appearance) ? appearance : QStringLiteral("omarchy");
    if (clean == m_appearance)
        return;
    m_appearance = clean;
    emit appearanceChanged();
    apply();
}

// Moves the shown palette to the effective one, with a crossfade once the
// window is up.
void Theme::apply() {
    const Palette next = effective();
    if (next == m_to)
        return;
    m_fade.stop();
    m_from = m_shown;
    m_to = next;
    emit targetChanged();
    if (m_animated) {
        m_fade.start();
    } else {
        m_shown = next;
        emit changed();
    }
}

QVariantMap Theme::target() const {
    return QVariantMap{{"accent", m_to.accent}, {"background", m_to.background},
                       {"foreground", m_to.foreground}, {"muted", m_to.muted},
                       {"panel", m_to.panel}, {"border", m_to.border},
                       {"selection", m_to.selection}, {"dark", m_to.mode == "dark"},
                       {"radius", m_radius}};
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

QColor Theme::withAlpha(const QString &color, double alpha) const {
    QColor value(color);
    value.setAlphaF(qBound(0.0, alpha, 1.0));
    return value;
}

QString Theme::readAccent(const QString &path) {
    return colorOr(readValue(path, "accent"), "#FFD60A");
}

// Black or white, whichever reads on the accent (WCAG relative luminance).
QString Theme::accentForeground() const {
    const QColor c(m_shown.accent);
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

    // A theme switch deletes the theme folder before moving the next one in.
    // Reading in between would flash the fallback colors, so wait for it.
    const QString colors = m_directory + "/theme/colors.toml";
    if (!QFileInfo::exists(colors))
        return;
    const Palette fallback = m_omarchy;
    const QString mode = readValue(colors, "mode", "dark");
    const QString accent = readAccent(colors);
    const QString background = colorOr(readValue(colors, "background"), fallback.background);
    const QString foreground = colorOr(readValue(colors, "foreground"), fallback.foreground);
    const QString muted = colorOr(readValue(colors, "muted"), fallback.muted);
    const QString selection = colorOr(readValue(colors, "selection"), fallback.selection);
    const QString red = colorOr(readValue(colors, "red"), fallback.red);
    const QString green = colorOr(readValue(colors, "green"), fallback.green);
    const QString darkBackground = colorOr(readValue(colors, "dark_background"), blend(background, foreground, 0.08));
    const QString lighterBackground = colorOr(readValue(colors, "lighter_background"), blend(background, foreground, 0.08));
    const QString darkerBackground = colorOr(readValue(colors, "darker_background"), blend(background, foreground, 0.2));
    const bool isDark = mode == "dark";
    const QString panel = isDark ? lighterBackground : darkBackground;
    const QString border = isDark ? blend(background, foreground, 0.2) : darkerBackground;

    m_omarchy = Palette{accent, background, foreground, muted, panel, border, selection, red, green, mode};
    apply();
}
