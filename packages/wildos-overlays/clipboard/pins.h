#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QUrl>
#include <QVariantList>

// Pinned clipboard items. Clipboard history is wiped every time the
// computer starts (Klipper keeps nothing on disk), so pins live here on
// their own: ~/.local/share/wildos/clipboard/pins.json, with pinned
// pictures copied beside it. They stay until unpinned.
class Pins : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QVariantList items READ items NOTIFY changed)

public:
    explicit Pins(QObject *parent = nullptr);

    QVariantList items() const { return m_items; }

    // An item is the same pin when its text, or its picture's bytes, match.
    Q_INVOKABLE QString keyFor(const QString &kind, const QString &text, const QUrl &image) const;
    Q_INVOKABLE bool isPinned(const QString &key) const;
    Q_INVOKABLE void pin(const QString &kind, const QString &text, const QUrl &image, double time, const QString &source);
    Q_INVOKABLE void unpin(const QString &key);
    // Puts a pin back on the clipboard, for one that is no longer in the
    // history after a restart.
    Q_INVOKABLE void restore(const QString &key);
    // Every pin and its picture gone from disk (the panic key).
    Q_INVOKABLE void clear();

Q_SIGNALS:
    void changed();

private:
    void load();
    void save();
    QString folder() const;

    QVariantList m_items;
};
