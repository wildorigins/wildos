#pragma once

#include <QObject>
#include <QPointer>
#include <QQuickTextDocument>
#include <QTextCharFormat>
#include <QTextFormat>
#include <QtQml/qqmlregistration.h>

// Bold, italic, lists and links for the message being written: QML's
// TextArea edits rich text but has no buttons' worth of API, so this works
// on its document through a text cursor.
class Formatter : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QQuickTextDocument *document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(int selectionStart MEMBER m_start NOTIFY selectionChanged)
    Q_PROPERTY(int selectionEnd MEMBER m_end NOTIFY selectionChanged)
    Q_PROPERTY(int cursorPosition MEMBER m_cursor NOTIFY selectionChanged)
    Q_PROPERTY(bool bold READ bold NOTIFY formatChanged)
    Q_PROPERTY(bool italic READ italic NOTIFY formatChanged)
    Q_PROPERTY(bool underline READ underline NOTIFY formatChanged)

public:
    using QObject::QObject;

    QQuickTextDocument *document() const
    {
        return m_document;
    }
    void setDocument(QQuickTextDocument *document);
    bool bold() const;
    bool italic() const;
    bool underline() const;

    Q_INVOKABLE void toggleBold();
    Q_INVOKABLE void toggleItalic();
    Q_INVOKABLE void toggleUnderline();
    Q_INVOKABLE void setColour(const QColor &colour);
    Q_INVOKABLE void setPointSize(int size);
    Q_INVOKABLE void bullets();
    Q_INVOKABLE void numbering();
    Q_INVOKABLE void indent(int step);
    Q_INVOKABLE void insertLink(const QString &url, const QString &text);
    Q_INVOKABLE void insertText(const QString &text);
    // A picture from a file, kept in the text itself (a signature's logo),
    // at most 320px wide.
    Q_INVOKABLE void insertPicture(const QUrl &file);
    // The words as HTML for sending, without Qt's document boilerplate
    // and with Albert Sans as the font.
    Q_INVOKABLE QString html() const;
    Q_INVOKABLE QString plainText() const;

Q_SIGNALS:
    void documentChanged();
    void selectionChanged();
    void formatChanged();

private:
    QTextCursor cursor() const;
    void merge(const QTextCharFormat &format);
    QTextCharFormat current() const;
    void list(QTextListFormat::Style style);

    QPointer<QQuickTextDocument> m_document;
    int m_start = 0;
    int m_end = 0;
    int m_cursor = 0;
};
