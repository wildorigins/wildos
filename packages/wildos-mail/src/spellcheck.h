#pragma once

#include <QColor>
#include <QPointer>
#include <QQuickTextDocument>
#include <QSet>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <Sonnet/Speller>

// Spell checking for the message being written. Misspelt words get a
// coloured underline that is drawn over the text and never becomes part of
// it, so nothing of it is sent. What to check comes from Settings > Typing,
// which keeps it where KDE's Sonnet does (~/.config/KDE/Sonnet.conf):
// whether to check at all, the language, capitals and your own words.
class SpellCheck : public QSyntaxHighlighter
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QQuickTextDocument *document READ quickDocument WRITE setQuickDocument NOTIFY documentChanged)
    // Where the person is typing: the word that ends there isn't finished,
    // so it isn't marked yet.
    Q_PROPERTY(int cursorPosition READ cursorPosition WRITE setCursorPosition NOTIFY cursorPositionChanged)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)

public:
    explicit SpellCheck(QObject *parent = nullptr);

    QQuickTextDocument *quickDocument() const
    {
        return m_quick;
    }
    void setQuickDocument(QQuickTextDocument *document);
    int cursorPosition() const
    {
        return m_cursor;
    }
    void setCursorPosition(int position);
    QColor color() const
    {
        return m_color;
    }
    void setColor(const QColor &color);

    // Reads Settings > Typing again. Sonnet has no way to tell another app
    // its settings changed, so Mail looks each time a message is started.
    Q_INVOKABLE void reload();
    // The misspelt word at a place in the text, as {word, start, end,
    // suggestions}; empty when the word there is fine.
    Q_INVOKABLE QVariantMap wordAt(int position) const;
    Q_INVOKABLE void replace(int start, int end, const QString &with);
    // "Add to dictionary": kept with the words Settings > Typing lists.
    Q_INVOKABLE void learn(const QString &word);
    // Left alone until Mail closes.
    Q_INVOKABLE void ignore(const QString &word);

Q_SIGNALS:
    void documentChanged();
    void cursorPositionChanged();
    void colorChanged();

protected:
    void highlightBlock(const QString &text) override;

private:
    bool wrong(const QString &word) const;
    void recheckAroundCursor();
    void recheckAll();

    QPointer<QQuickTextDocument> m_quick;
    Sonnet::Speller m_speller;
    QTextCharFormat m_format;
    QColor m_color;
    QSet<QString> m_own;
    QSet<QString> m_session;
    bool m_active = false;
    bool m_skipUppercase = false;
    int m_cursor = 0;
    // Where the cursor was at the last recheck, and whether one is queued.
    int m_checkedAt = 0;
    bool m_recheckQueued = false;
    // True while rechecking with the cursor where it has come to rest.
    bool m_cursorSettled = false;
};
