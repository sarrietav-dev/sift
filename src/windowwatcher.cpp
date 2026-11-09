#include "windowwatcher.h"

#include <QDir>
#include <QEvent>
#include <QFile>
#include <QWindow>

WindowWatcher::WindowWatcher(QObject *parent)
    : QObject(parent) {
    m_reconnect.setSingleShot(true);
    m_reconnect.setInterval(3000);
    connect(&m_reconnect, &QTimer::timeout, this, &WindowWatcher::connectHyprland);
    connect(&m_hyprland, &QLocalSocket::readyRead, this, &WindowWatcher::readHyprland);
    connect(&m_hyprland, &QLocalSocket::disconnected, &m_reconnect, qOverload<>(&QTimer::start));
    connect(&m_hyprland, &QLocalSocket::errorOccurred, this, [this] {
        if (m_hyprland.state() == QLocalSocket::UnconnectedState)
            m_reconnect.start();
    });
    connectHyprland();
}

void WindowWatcher::setWindow(QWindow *window) {
    if (m_window)
        m_window->removeEventFilter(this);
    m_window = window;
    if (!window)
        return;
    m_exposed = window->isExposed();
    window->installEventFilter(this);
    connect(window, &QWindow::visibilityChanged, this, [this](QWindow::Visibility visibility) {
        if (visibility != QWindow::Hidden && visibility != QWindow::Minimized)
            emit shown();
    });
    connect(window, &QWindow::activeChanged, this, &WindowWatcher::shown);
}

bool WindowWatcher::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_window && event->type() == QEvent::Expose) {
        const bool exposed = m_window->isExposed();
        if (exposed && !m_exposed)
            emit shown();
        m_exposed = exposed;
    }
    return QObject::eventFilter(watched, event);
}

void WindowWatcher::connectHyprland() {
    const QString signature = qEnvironmentVariable("HYPRLAND_INSTANCE_SIGNATURE");
    const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (signature.isEmpty() || runtime.isEmpty())
        return;
    const QString path = runtime + "/hypr/" + signature + "/.socket2.sock";
    if (!QFile::exists(path))
        return;
    m_buffer.clear();
    m_hyprland.abort();
    m_hyprland.connectToServer(path, QIODevice::ReadOnly);
}

void WindowWatcher::readHyprland() {
    m_buffer += m_hyprland.readAll();
    bool changed = false;
    qsizetype newline;
    while ((newline = m_buffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_buffer.left(newline);
        m_buffer.remove(0, newline + 1);
        if (line.startsWith("workspace>>") || line.startsWith("workspacev2>>")
            || line.startsWith("activespecial>>") || line.startsWith("focusedmon>>")
            || line.startsWith("movewindow>>") || line.startsWith("fullscreen>>"))
            changed = true;
    }
    if (changed)
        emit shown();
}
