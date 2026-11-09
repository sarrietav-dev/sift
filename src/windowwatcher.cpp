#include "windowwatcher.h"

#include <QEvent>
#include <QWindow>

WindowWatcher::WindowWatcher(QObject *parent)
    : QObject(parent) {}

void WindowWatcher::setWindow(QWindow *window) {
    if (m_window)
        m_window->removeEventFilter(this);
    m_window = window;
    if (!window)
        return;
    m_exposed = window->isExposed();
    window->installEventFilter(this);
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
