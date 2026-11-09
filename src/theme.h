#pragma once

#include <QColor>
#include <QFileSystemWatcher>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QVariantAnimation>
#include <QVariantMap>

// Every visible color in sift comes from here. By default it follows the
// Omarchy theme live: colors, light/dark mode, and Hyprland's window
// rounding. Like Spotifast, it can instead follow the system's light or dark
// preference, or stay light or dark. A change crossfades rather than snaps.
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
    Q_PROPERTY(QString appearance READ appearance WRITE setAppearance NOTIFY appearanceChanged)
    // The palette a transition is heading to. The reader takes it at once and
    // animates there with CSS, instead of repainting on every step.
    Q_PROPERTY(QVariantMap target READ target NOTIFY targetChanged)

public:
    explicit Theme(const QString &currentDirectory = {}, QObject *parent = nullptr, const QString &hyprctl = {});
    ~Theme() override;

    struct Palette {
        QString accent, background, foreground, muted, panel, border, selection, red, green, mode;
        bool operator==(const Palette &o) const {
            return accent == o.accent && background == o.background && foreground == o.foreground
                && muted == o.muted && panel == o.panel && border == o.border && selection == o.selection
                && red == o.red && green == o.green && mode == o.mode;
        }
    };

    QString accent() const { return m_shown.accent; }
    QString accentForeground() const;
    QString background() const { return m_shown.background; }
    QString foreground() const { return m_shown.foreground; }
    QString muted() const { return m_shown.muted; }
    QString panel() const { return m_shown.panel; }
    QString borderColor() const { return m_shown.border; }
    QString selection() const { return m_shown.selection; }
    QString red() const { return m_shown.red; }
    QString green() const { return m_shown.green; }
    QString mode() const { return m_shown.mode; }
    bool dark() const { return m_shown.mode == "dark"; }
    int radius() const { return m_radius; }
    QString uiFont() const { return "Noto Sans"; }
    QString serifFont() const { return "IBM Plex Serif"; }
    QString monoFont() const { return "CaskaydiaCove Nerd Font"; }
    QString appearance() const { return m_appearance; }
    QVariantMap target() const;

    // "omarchy", "system", "light", or "dark".
    void setAppearance(const QString &appearance);
    // Crossfades are off until the window is up, so startup never animates.
    void setAnimated(bool animated) { m_animated = animated; }

    // A theme colour at the given opacity, for hover and selection tints.
    Q_INVOKABLE QColor withAlpha(const QString &color, double alpha) const;

    static QString readAccent(const QString &path);
    static QString readValue(const QString &path, const QString &key, const QString &fallback = {});
    static Palette builtIn(bool dark);

signals:
    void changed();
    void appearanceChanged();
    void targetChanged();

private:
    void reload();
    void refreshRounding();
    void apply();
    Palette effective() const;

    QString m_directory;
    QString m_appearance = "omarchy";
    Palette m_omarchy;
    Palette m_from;
    Palette m_to;
    Palette m_shown;
    bool m_animated = false;
    QVariantAnimation m_fade;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QString m_hyprctl;
    int m_radius = 0;
    QProcess m_roundingProcess;
    QTimer m_roundingPoll, m_roundingTimeout;
};
