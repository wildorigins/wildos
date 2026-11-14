#include "nopaste.h"

#include <QKeyEvent>
#include <QMouseEvent>

NoPaste::NoPaste(QObject *parent)
    : QObject(parent)
{
}

void NoPaste::componentComplete()
{
    m_field = qobject_cast<QQuickItem *>(parent());
    if (m_field) {
        m_field->installEventFilter(this);
    }
}

bool NoPaste::eventFilter(QObject *watched, QEvent *event)
{
    if (!m_enabled || watched != m_field) {
        return false;
    }
    switch (event->type()) {
    case QEvent::ShortcutOverride:
    case QEvent::KeyPress: {
        auto key = static_cast<QKeyEvent *>(event);
        const bool shiftInsert = key->key() == Qt::Key_Insert && (key->modifiers() & Qt::ShiftModifier);
        if (key->matches(QKeySequence::Paste) || shiftInsert) {
            // Taken for the field so it reaches no shortcut either, then
            // dropped.
            event->accept();
            return event->type() == QEvent::KeyPress;
        }
        return false;
    }
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseButtonDblClick:
        return static_cast<QMouseEvent *>(event)->button() == Qt::MiddleButton;
    case QEvent::DragEnter:
    case QEvent::DragMove:
    case QEvent::Drop:
        event->ignore();
        return true;
    default:
        return false;
    }
}
