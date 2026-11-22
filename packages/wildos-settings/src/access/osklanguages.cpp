// The on-screen keyboard's languages start from the keyboard layouts, as the
// Windows touch keyboard starts from its input languages. Maliit keeps its
// own list (English alone) and Plasma has nowhere to change it, so without
// this an Arabic layout never reached the on-screen keyboard. Once someone
// picks the keyboard's languages in Settings, a new layout only adds its
// language; theirs are never taken away.

#include "osklanguages.h"

#include "osklangs.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>

OskLanguages::OskLanguages(QObject *parent)
    : QObject(parent)
{
    m_layouts = OskLangs::fromLayouts();
    if (!OskLangs::chosen()) {
        OskLangs::setEnabled(m_layouts.isEmpty() ? QStringList{QStringLiteral("en")} : m_layouts);
    }
    QDBusConnection bus = QDBusConnection::sessionBus();
    const QString service = QStringLiteral("org.kde.keyboard");
    const QString path = QStringLiteral("/Layouts");
    const QString interface = QStringLiteral("org.kde.KeyboardLayouts");
    bus.connect(service, path, interface, QStringLiteral("layoutListChanged"), this, SLOT(sync()));
    bus.connect(service, path, interface, QStringLiteral("layoutChanged"), this, SLOT(follow(uint)));
    // The layout in use at sign-in, so the keyboard opens in the same one.
    const QDBusReply<uint> current = bus.call(QDBusMessage::createMethodCall(service, path, interface, QStringLiteral("getLayout")), QDBus::Block, 2000);
    if (current.isValid()) {
        follow(current.value());
    }
}

void OskLanguages::sync()
{
    const QStringList layouts = OskLangs::fromLayouts();
    if (!OskLangs::chosen()) {
        OskLangs::setEnabled(layouts.isEmpty() ? QStringList{QStringLiteral("en")} : layouts);
    } else {
        QStringList enabled = OskLangs::enabled();
        for (const QString &language : layouts) {
            if (!m_layouts.contains(language) && !enabled.contains(language)) {
                enabled.append(language);
            }
        }
        OskLangs::setEnabled(enabled);
    }
    m_layouts = layouts;
}

// Switching layout (Super Space) switches the on-screen keyboard with it,
// when the keyboard has that language.
void OskLanguages::follow(uint index)
{
    const QString language = OskLangs::byLayout().value(int(index));
    if (!language.isEmpty() && OskLangs::enabled().contains(language)) {
        OskLangs::setActive(language);
    }
}
