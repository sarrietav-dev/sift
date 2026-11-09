#pragma once

#include <QColor>
#include <QFileSystemWatcher>
#include <QObject>
#include <QProcess>
#include <QTimer>

// Follows the Omarchy theme live: colors, light/dark mode, and Hyprland's
// window rounding. Every visible color in sift comes from here.
class Theme : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString accent READ accent NOTIFY changed)
    Q_PROPERTY(QString accentForeground READ accentForeground NOTIFY changed)
    Q_PROPERTY(QString background READ background NOTIFY changed)
    Q_PROPERTY(QString foreground READ foreground NOTIFY changed)
    Q_PROPERTY(QString muted READ muted NOTIFY changed)
    Q_PROPERTY(QString panel READ panel NOTIFY changed)
    Q_PROPERTY(QString border READ borderColor NOTIFY changed)
    Q_PROPERTY(QString selection READ selection NOTIFY changed)
    Q_PROPERTY(QString red READ red NOTIFY changed)
    Q_PROPERTY(QString green READ green NOTIFY changed)
    Q_PROPERTY(QString mode READ mode NOTIFY changed)
    Q_PROPERTY(bool dark READ dark NOTIFY changed)
    Q_PROPERTY(int radius READ radius NOTIFY changed)
    Q_PROPERTY(QString uiFont READ uiFont NOTIFY changed)
    Q_PROPERTY(QString serifFont READ serifFont NOTIFY changed)
    Q_PROPERTY(QString monoFont READ monoFont NOTIFY changed)

public:
    explicit Theme(const QString &currentDirectory = {}, QObject *parent = nullptr, const QString &hyprctl = {});
    ~Theme() override;

    QString accent() const { return m_accent; }
    QString accentForeground() const;
    QString background() const { return m_background; }
    QString foreground() const { return m_foreground; }
    QString muted() const { return m_muted; }
    QString panel() const { return m_panel; }
    QString borderColor() const { return m_border; }
    QString selection() const { return m_selection; }
    QString red() const { return m_red; }
    QString green() const { return m_green; }
    QString mode() const { return m_mode; }
    bool dark() const { return m_mode == "dark"; }
    int radius() const { return m_radius; }
    QString uiFont() const { return "Noto Sans"; }
    QString serifFont() const { return "IBM Plex Serif"; }
    QString monoFont() const { return "CaskaydiaCove Nerd Font"; }

    // A theme colour at the given opacity, for hover and selection tints.
    Q_INVOKABLE QColor withAlpha(const QString &color, double alpha) const;

    static QString readAccent(const QString &path);
    static QString readValue(const QString &path, const QString &key, const QString &fallback = {});

signals:
    void changed();

private:
    void reload();
    void refreshRounding();

    QString m_directory;
    QString m_accent = "#FFD60A";
    QString m_background = "#1a1a1a";
    QString m_foreground = "#f0f0f0";
    QString m_muted = "#888888";
    QString m_panel = "#222222";
    QString m_border = "#333333";
    QString m_selection = "#444444";
    QString m_red = "#D14D41";
    QString m_green = "#879A39";
    QString m_mode = "dark";
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QString m_hyprctl;
    int m_radius = 0;
    QProcess m_roundingProcess;
    QTimer m_roundingPoll, m_roundingTimeout;
};
