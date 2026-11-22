#pragma once

#include <QHash>
#include <QImage>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

class QJSEngine;
class QNetworkAccessManager;
class QQmlEngine;
class LogoFetch;

// Pictures for senders in place of their initials: a contact's own photo
// first (PeopleEngine hands them over as it reads the address books), else
// the company's logo, from the sender domain's BIMI record or its website's
// icon. Nothing else is asked: no logo or avatar service, no Gravatar.
//
// A domain is fetched once, when one of its senders first shows in a
// message list, and kept in ~/.cache/wildos-mail/logos with an index of
// what was found when, so a domain is asked again only after a month.
// Free mail domains are never fetched: a gmail.com logo says nothing about
// the person.
class SenderImages : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // Goes up whenever a picture arrives; badges bind to it.
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    // Settings, Privacy and security: Show company logos. Off means no
    // request at all; contact photos still show, they're on this PC.
    Q_PROPERTY(bool logos READ logos WRITE setLogos NOTIFY logosChanged)

public:
    static SenderImages *instance();
    static SenderImages *create(QQmlEngine *, QJSEngine *);

    int revision() const
    {
        return m_revision;
    }
    bool logos() const
    {
        return m_logos;
    }
    void setLogos(bool on);

    // A file:// URL for the address's picture, or "" for initials. With
    // fetch, an unknown domain's logo is asked for; an opened message
    // passes false so that opening it never makes a request.
    Q_INVOKABLE QString imageFor(const QString &address, bool fetch = true);

    // One address book's photos, address to file; empty once it's gone.
    void setContactPhotos(const QString &book, const QHash<QString, QString> &photos);

    static QString photoDir();
    static QString logoPath(const QString &domain);
    // The finished badges, round with clear corners so QML needs no mask:
    // a photo fills the circle; a logo fills it when it has its own square
    // background, else sits inside a light (or, for a white logo, dark) one.
    static QImage photoBadge(const QImage &source);
    static QImage logoBadge(const QImage &source);

Q_SIGNALS:
    void changed();
    void logosChanged();

private:
    explicit SenderImages(QObject *parent = nullptr);

    struct Entry {
        qint64 time = 0;
        // hit, none, or alias: a subdomain without its own BIMI logo,
        // which shows its organisational domain's.
        QString state;
    };

    QString logoFor(const QString &domain, bool fetch, bool followAlias);
    void enqueue(const QString &domain);
    void pump();
    void finished(LogoFetch *fetch, const QString &domain, const QString &state, bool transient);
    void loadIndex();
    void saveIndex();
    void bump();

    int m_revision = 0;
    bool m_logos = true;
    bool m_peopleStarted = false;
    QNetworkAccessManager *m_net = nullptr;
    QHash<QString, Entry> m_index;
    QHash<QString, QHash<QString, QString>> m_photosByBook;
    QHash<QString, QString> m_photos;
    QStringList m_queue;
    QSet<QString> m_busy;
    // Asked this run and failed for a reason that may pass (offline): not
    // written down, not asked again until Mail starts again.
    QSet<QString> m_tried;
    QList<QPointer<LogoFetch>> m_running;
    QTimer m_saveTimer;
};
