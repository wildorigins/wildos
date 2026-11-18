#pragma once

#include "notefile.h"

#include <QDateTime>
#include <QObject>
#include <QQmlEngine>
#include <QTimer>
#include <QUrl>

// The note open in a window. The editor shows the text under the title;
// the title, pin and colour have controls of their own. Everything is
// written back a moment after the last change, and when the note is left
// or the window closes.
class NoteDocument : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString path READ path NOTIFY pathChanged)
    Q_PROPERTY(QString notebook READ notebook NOTIFY pathChanged)
    Q_PROPERTY(QString noteDir READ noteDir NOTIFY pathChanged)
    Q_PROPERTY(bool open READ isOpen NOTIFY pathChanged)
    Q_PROPERTY(QString title READ title WRITE setTitle NOTIFY titleChanged)
    Q_PROPERTY(bool pinned READ pinned WRITE setPinned NOTIFY metaChanged)
    Q_PROPERTY(QString colour READ colour WRITE setColour NOTIFY metaChanged)
    Q_PROPERTY(QStringList tags READ tags NOTIFY tagsChanged)
    Q_PROPERTY(bool saved READ saved NOTIFY savedChanged)
    Q_PROPERTY(QString editedText READ editedText NOTIFY savedChanged)
    Q_PROPERTY(bool readOnly READ readOnly NOTIFY pathChanged)

public:
    explicit NoteDocument(QObject *parent = nullptr);
    ~NoteDocument() override;

    QString path() const;
    QString notebook() const;
    QString noteDir() const;
    bool isOpen() const;
    QString title() const;
    void setTitle(const QString &title);
    bool pinned() const;
    void setPinned(bool pinned);
    QString colour() const;
    void setColour(const QString &colour);
    QStringList tags() const;
    bool saved() const;
    QString editedText() const;
    bool readOnly() const;

    Q_INVOKABLE bool load(const QString &path);
    // A new note in the notebook, open and empty.
    Q_INVOKABLE bool create(const QString &notebook);
    Q_INVOKABLE QString body() const;
    Q_INVOKABLE void setBody(const QString &body);
    Q_INVOKABLE void save();
    // Saves and lets go of the note. A new note left empty is removed.
    Q_INVOKABLE void close();
    Q_INVOKABLE QString markdown() const;
    // On paper the note is laid out from its Markdown: headings, lists,
    // tables and pictures, black on white. False when Print was cancelled.
    Q_INVOKABLE bool print();
    Q_INVOKABLE bool exportPdf(const QUrl &url);
    // Where Export as PDF starts: Documents, under the note's title.
    Q_INVOKABLE QUrl pdfUrl() const;

Q_SIGNALS:
    void pathChanged();
    void titleChanged();
    void metaChanged();
    void tagsChanged();
    void savedChanged();
    // The file was changed outside Notes; the editor shows body() again.
    void bodyReplaced();

private:
    void changed();
    void reloadFromDisk(const QString &path);
    void updateTags();
    QString printable() const;

    QString m_path;
    NoteParts m_parts;
    QByteArray m_written;
    QDateTime m_modified;
    QStringList m_tags;
    bool m_dirty = false;
    bool m_renamePending = false;
    // Made here and not typed in yet: its title is empty rather than
    // "Untitled", and it goes if it is left empty.
    bool m_fresh = false;
    bool m_createdHere = false;
    // The title of a note without a heading: its file name, until renamed.
    QString m_fileTitle;
    QTimer m_timer;
};
