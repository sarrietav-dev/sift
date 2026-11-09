#pragma once

#include <QLocalSocket>
#include <QObject>
#include <QPointer>
#include <QTimer>

class QWindow;

// Tells the reader when its window comes back into view. WebEngine can lose
// its last frame while a window is hidden on another workspace and shows
// black until the page draws again, which otherwise only happens on focus.
// Qt reports some of these as expose events; Hyprland's event socket reports
// every workspace change, including ones Qt never hears about.
class WindowWatcher : public QObject {
    Q_OBJECT

public:
    explicit WindowWatcher(QObject *parent = nullptr);
    void setWindow(QWindow *window);

signals:
    void shown();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void connectHyprland();
    void readHyprland();

    QPointer<QWindow> m_window;
    bool m_exposed = true;
    QLocalSocket m_hyprland;
    QTimer m_reconnect;
    QByteArray m_buffer;
};
