#include "secretcopy.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMimeData>

SecretCopy::SecretCopy(QObject *parent)
    : QObject(parent)
{
}

void SecretCopy::componentComplete()
{
    m_field = qobject_cast<QQuickItem *>(parent());
    if (m_field) {
        m_field->installEventFilter(this);
    }
}

void SecretCopy::copy()
{
    if (!m_field) {
        return;
    }
    const QString text = m_field->property("selectedText").toString();
    if (text.isEmpty()) {
        return;
    }
    auto *mime = new QMimeData;
    mime->setText(text);
    mime->setData(QStringLiteral("x-kde-passwordManagerHint"), QByteArrayLiteral("secret"));
    QGuiApplication::clipboard()->setMimeData(mime, QClipboard::Clipboard);
}

void SecretCopy::cut()
{
    if (!m_field || m_field->property("readOnly").toBool() || m_field->property("selectedText").toString().isEmpty()) {
        return;
    }
    copy();
    QMetaObject::invokeMethod(m_field, "remove", Q_ARG(int, m_field->property("selectionStart").toInt()),
                              Q_ARG(int, m_field->property("selectionEnd").toInt()));
}

bool SecretCopy::eventFilter(QObject *watched, QEvent *event)
{
    if (!m_enabled || watched != m_field || (event->type() != QEvent::ShortcutOverride && event->type() != QEvent::KeyPress)) {
        return false;
    }
    auto key = static_cast<QKeyEvent *>(event);
    const bool isCopy = key->matches(QKeySequence::Copy);
    const bool isCut = key->matches(QKeySequence::Cut);
    if (!isCopy && !isCut) {
        return false;
    }
    // Taken for the field so it reaches no shortcut, then done here on the
    // key press instead of by the field.
    event->accept();
    if (event->type() == QEvent::KeyPress) {
        // A hidden password can't be copied at all, as before.
        if (m_field->property("echoMode").toInt() == 0) {
            isCut ? cut() : copy();
        }
        return true;
    }
    return false;
}
