#pragma once

#include <QObject>
#include <QPointer>

class QWindow;

// Tells the reader when its window is shown again. Hyprland suspends a window
// on a hidden workspace, which Qt reports as losing exposure. WebEngine then
// drops its frame and stays black until it is told the view is shown again,
// which otherwise only happens when the window is focused.
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
    QPointer<QWindow> m_window;
    bool m_exposed = true;
};
