#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QUrl>

// What Quick Look shows for one item: which kind of preview fits it and,
// for text and code, the start of the file. Worked out on a worker thread,
// so a slow drive never holds up the window.
class PreviewSource : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QUrl url READ url WRITE setUrl NOTIFY urlChanged)
    // "", while it is being worked out, then "folder", "image", "animated",
    // "pdf", "video", "audio", "text" or "other".
    Q_PROPERTY(QString kind READ kind NOTIFY changed)
    Q_PROPERTY(QString mimeType READ mimeType NOTIFY changed)
    Q_PROPERTY(QString text READ text NOTIFY changed)
    // True when the file is longer than the part read for the preview.
    Q_PROPERTY(bool truncated READ truncated NOTIFY changed)

public:
    explicit PreviewSource(QObject *parent = nullptr);

    QUrl url() const { return m_url; }
    void setUrl(const QUrl &url);
    QString kind() const { return m_kind; }
    QString mimeType() const { return m_mimeType; }
    QString text() const { return m_text; }
    bool truncated() const { return m_truncated; }

    // Whether a file of this type gets the "image" preview, so a folder
    // listing can say without the file being opened.
    Q_INVOKABLE bool showsAsPicture(const QString &mimeType) const;

Q_SIGNALS:
    void urlChanged();
    void changed();

private:
    QUrl m_url;
    QString m_kind;
    QString m_mimeType;
    QString m_text;
    bool m_truncated = false;
    int m_generation = 0;
};
