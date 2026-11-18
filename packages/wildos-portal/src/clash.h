#pragma once

#include <QDBusContext>
#include <QDBusMessage>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QUrl>
#include <QVariantMap>

class QQuickWindow;

// One name clash in a copy or move: what is being copied, what is already
// there, and the three ways out. ClashDialog.qml draws it.
class Clash : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Made by the clash service")

    Q_PROPERTY(QString windowTitle READ windowTitle CONSTANT)
    Q_PROPERTY(QString heading READ heading CONSTANT)
    Q_PROPERTY(QString summary READ summary CONSTANT)
    Q_PROPERTY(QString iconName READ iconName CONSTANT)
    Q_PROPERTY(bool folders READ folders CONSTANT)
    Q_PROPERTY(bool canReplace READ canReplace CONSTANT)
    Q_PROPERTY(bool canSkip READ canSkip CONSTANT)
    Q_PROPERTY(bool canKeepBoth READ canKeepBoth CONSTANT)
    Q_PROPERTY(bool multiple READ multiple CONSTANT)
    Q_PROPERTY(QString replaceTitle READ replaceTitle CONSTANT)
    Q_PROPERTY(QString replaceText READ replaceText CONSTANT)
    Q_PROPERTY(QString skipTitle READ skipTitle CONSTANT)
    Q_PROPERTY(QString skipText READ skipText CONSTANT)
    Q_PROPERTY(QString keepBothText READ keepBothText CONSTANT)

public:
    explicit Clash(const QVariantMap &info, QObject *parent = nullptr);

    QString windowTitle() const;
    QString heading() const;
    QString summary() const;
    QString iconName() const { return m_icon; }
    bool folders() const { return m_folders; }
    bool canReplace() const { return m_canReplace; }
    bool canSkip() const { return m_canSkip; }
    bool canKeepBoth() const { return m_canKeepBoth; }
    bool multiple() const { return m_multiple; }
    QString replaceTitle() const;
    QString replaceText() const;
    QString skipTitle() const;
    QString skipText() const;
    QString keepBothText() const;
    QString keepBothName() const { return m_keepBothName; }

    // "replace", "skip", "both" or "cancel", and whether the same goes for
    // every clash still to come in this copy.
    Q_INVOKABLE void answer(const QString &choice, bool all);
    void cancel() { answer(QStringLiteral("cancel"), false); }

Q_SIGNALS:
    void answered(const QString &choice, bool all, const QString &newName);

private:
    QString m_name;
    QUrl m_whereUrl;
    QString m_icon;
    QString m_keepBothName;
    qint64 m_srcSize = -1;
    qint64 m_destSize = -1;
    qint64 m_srcTime = 0;
    qint64 m_destTime = 0;
    qint64 m_items = 0;
    qint64 m_clashes = 0;
    bool m_folders = false;
    bool m_canReplace = true;
    bool m_canSkip = true;
    bool m_canKeepBoth = true;
    bool m_multiple = false;
    bool m_move = false;
    bool m_done = false;
};

// org.wildos.Dialogs: the name clash question for every app. KIO's own
// rename dialog is patched to ask here (build/patches/kio), and Files asks
// here too, so a clash looks the same wherever a copy started.
class ClashService : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.wildos.Dialogs")

public:
    explicit ClashService(QObject *parent = nullptr);

public Q_SLOTS:
    QString AskClash(const QVariantMap &info, bool &all, QString &newName);
    void CancelClash(const QString &token);

private:
    QHash<QString, QPointer<Clash>> m_open;
};
